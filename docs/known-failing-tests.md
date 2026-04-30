---
name: Known failing tests on main
description: The expected-red test set on main (post-Mi-18). Each entry maps a failing test/case to the in-flight PR or backlog item that will fix it. The Tier 2 rebase plan's "CI green" gate is defined relative to this list.
type: project
---

# Known failing tests on main

**Baseline as of:** Post-Mi-18 merge (`d925176`+). Mi-18 made the build compile cleanly under `-Werror` on Linux GCC and macOS Apple Clang for the first time in this remediation cycle. With the build now green, the test runner reaches its first execution; the failures listed below are the **expected-red set** that the in-flight PRs and backlog items will progressively eliminate.

**Why this doc exists.** Without an authoritative known-failing list, the natural human heuristic during a rebase walk is "CI red ⇒ regression," which would halt every merge unnecessarily. The orchestrator's halt rules need a finer signal: **test red is acceptable iff every failing test is on this list (matched by TEST_CASE name), and no new failures appear.** This file is that authoritative list.

## Identification rule

Failures are identified by **TEST_CASE name** (with file path), NOT by `file:line`. Line numbers are current-location hints that drift as PRs add code above the failure point. Identification schema, in priority order:

1. **TEST_CASE name + file** — canonical. Example: `"Blind mode pipeline: gap detection"` in `tests/test_integration.cpp`.
2. **TEST_CASE name → SECTION name + file** — when a failure is SECTION-granular within a TEST_CASE. (No current entry uses this; reserved for forward compatibility.)
3. **file:line** — fallback only when neither (1) nor (2) suffices, with explicit justification. (No current entry should need this.)

This rule was revised after PR #23 surfaced a **false halt**: PR #23 added new test bodies above three failing cases, shifting their line numbers by exactly +37 (`:479 → :516`, `:691 → :728`, `:762 → :799`) while keeping TEST_CASE name and assertion shape identical. The original `same name + same line` rule incorrectly classified that drift as a regression. The TEST_CASE-name rule correctly classifies it as the same documented failure. See `docs/deviations.md` → `KNOWN-FAILING-SCHEMA-V2` for the full rationale.

"Currently at line X" hints below are diagnostic — use them to navigate, but the gate matches on TEST_CASE name.

**How to read this doc.**
- Each entry is a single failing site: file, line, test case, what it asserts, why it currently fails, and which item in the merge queue (or longer-term backlog) cures it.
- "Cured by" cites a PR number when the fix is in flight, or a backlog ID otherwise.
- When a PR merges, the corresponding entries here move to the **Resolved** section at the bottom (with the merging commit hash) and the same regression check rolls forward.
- If a test starts passing without an obvious upstream merge, that's a signal — investigate before deleting the entry.

## How the rebase walk uses this list

Per `docs/tier2-rebase-plan.md`, the post-rebase CI gate now reads:

> **Build green on both Linux and macOS, AND the set of failing tests is a strict subset of `docs/known-failing-tests.md`, AND no test fails that is not on the list.**

That is, a PR's CI is "green for merge purposes" when:
1. Every `build / *` and `sanitizers (asan+ubsan)` job's compile step succeeds.
2. Every test failure on the PR is also a documented known-failing entry (same test name, same line, same failure mode).
3. No test passes on main and fails on the PR.

A test that fails on the PR but is NOT on this list is a regression — halt and surface.

A test that passes on the PR but is on this list is progress — update this file as part of the merging commit (move to Resolved).

## Active known-failing entries

### `test_integration` — TEST_CASE `"Blind mode pipeline: gap detection"` (`[integration][blind]`)

- **File.** `tests/test_integration.cpp` (currently line 446 for the TEST_CASE; failing assertion currently at line 479 on main / line 516 after PR #23's additions; drifts).
- **Assertion.** `CHECK(analysis.split_points.size() >= 2)` — "≥1 gap => ≥2 tracks".
- **Assertion intent.** A clean 2-track synthetic vinyl rip should produce ≥1 gap and therefore ≥2 split points. Comment in source explicitly says: "if fixture noise is genuinely too high, the fix is to regenerate with a lower noise floor — not to silently accept the error path."
- **Why failing.** Blind-mode pipeline returns only 1 split on the current synthetic 2-track fixture. The defect is documented as **NEW-BLIND-GAP** in BACKLOG.md (Tier 6 — Algorithmic correctness / blind-mode tuning). The score-gap thresholding currently misclassifies the inter-track silence as below-threshold for some fixture configurations.
- **Cured by.** **NEW-BLIND-GAP** (BACKLOG.md, Tier 6). Not in the current Tier 1+2 queue. Will remain failing after Tier 1+2 lands.

### `test_integration` — TEST_CASE `"Lossless end-to-end: verify exported file formats"` (`[integration][e2e]`)

- **File.** `tests/test_integration.cpp` (currently line 699 for the TEST_CASE; failing assertion at line 728; drifts).
- **Assertion.** `REQUIRE(export_result.has_value())` against the `mwaac::write_track(...)` call.
- **Assertion intent (as written).** Round-trip a 48 kHz, 2-channel, 24-bit WAV file: open → `write_track` → re-open. The export must succeed.
- **Why failing.** **Test-side arithmetic bug, not a parser/write defect.** The local `create_test_wav` overload at `tests/test_integration.cpp:101` takes an optional `audio_data` vector; the call at `:710` passes `std::vector<float>(48000, 0.5f)` for a 2-channel/48000-frame request. libsndfile interprets 48000 floats as 24000 stereo frames, so the resulting source file has `info.frames = 24000`. `write_track(..., 0, 47999)` then trips `end_sample (47999) >= info.frames (24000)` and returns `AudioError::InvalidRange`. The source file is `SF_FORMAT_WAV | SF_FORMAT_PCM_24` — plain PCM, not `WAVE_FORMAT_EXTENSIBLE`.
- **Cured by.** **`INT-728-FIXTURE-MISMATCH`** (BACKLOG.md). Two architectural options on the table for that item: (a) fix the arithmetic so the test round-trips plain 24-bit PCM, or (b) rewrite to use the `tests/fixtures/waveext/` artifact (now-parseable post-M-3) so the test exercises the genuine 24-bit 2ch EXTENSIBLE round-trip the comment describes.
- **Audit-pass-discipline note.** This entry's prior `Cured by: M-3` claim was an unaudited cure-attribution that survived three doc audit passes (test-identity, line-shift schema, AIFF cluster) because each of those swept different axes. M-3's fix-agent caught it during dispatch on 2026-04-29 (the 4th catch on this doc, first on the cure-attribution axis). See `feedback_audit_pre_staged_docs_along_every_axis.md`. M-3 itself landed clean as PR #34 (`70a7745`) — its real cure scope was the four `[waveext]`-tagged tests in `test_audio_file.cpp` / `test_lossless.cpp`, none of which are in this Active set (they were `[!shouldfail]`-tagged and visible only in the regression-by-name check).

### `test_integration` — TEST_CASE `"Combined workflow: reference then blind analysis"` (`[integration][combined]`)

- **File.** `tests/test_integration.cpp` (currently line 705 for the TEST_CASE; failing assertion currently at line 762 on main / line 799 after PR #23; drifts).
- **Assertion.** `CHECK(blind_result.value().split_points.size() >= 2)` — same `≥2 split points` invariant as the gap-detection case.
- **Assertion intent.** Different test case that also calls `analyze_reference_mode` for a side-channel sanity check (which is `(void)`'d pending FIXTURE-REF coverage of that path).
- **Why failing.** Same defect as line 479 — NEW-BLIND-GAP. The two test cases exercise different fixture variants but both bottom out on the same blind-mode threshold flaw.
- **Cured by.** **NEW-BLIND-GAP** (BACKLOG.md, Tier 6). Same as line 479. Will remain failing after Tier 1+2 lands.

### `test_reference_mode` — standalone binary returns non-zero

- **Location.** `tests/test_reference_mode.cpp` (whole binary; ctest reports `Failed`).
- **Why failing.** The binary contains exactly two `TEST_CASE`s, and both are unconditional `SKIP()`s. Catch2 returns a non-zero exit when every case skips and none pass, so ctest tags the binary `Failed` even though no assertion failed. The two cases are:
  - `test_reference_mode.cpp:14` — `"Reference mode: per-track alignment to synthetic vinyl"` — `SKIP("TODO(test-fixtures): FIXTURE-REF — synthetic vinyl rip with known track boundaries is not yet in tests/fixtures/. Will assert that align_per_track lands each track within ±N samples of truth.")`.
  - `test_reference_mode.cpp:20` — `"Reference mode: natural filename sort ordering"` — `SKIP("TODO(test-fixtures): FIXTURE-REF — relies on filesystem fixture that doesn't exist yet. Will verify 'Track 2.wav' < 'Track 10.wav' at the public API level.")`.
- **Cured by.** **Two separate items, neither in PR #23's scope** (per BACKLOG.md FIXTURE-REF exit criteria, which lists three `[integration][reference]` cases inside `test_integration.cpp`, not these two):
  - **M-REF-ALIGN-UNIT** (BACKLOG.md, Tier 5 — Algorithmic correctness) cures the per-track-alignment SKIP. The fixture this case needs (FIXTURE-REF) is now landed via PR #23, but the test body itself was never written. M-REF-ALIGN-UNIT is the work to author it.
  - **Mi-17** (BACKLOG.md, Tier 9 — extended) cures the natural-filename-sort SKIP, alongside its original mandate to harden `natural_less` against `std::stoll` overflow.
- **Why this entry was misattributed previously.** The pre-staged version of this doc claimed PR #23 cures `test_reference_mode`. That was an audit-pass-required failure: PR #23's BACKLOG-stated scope is the three `[integration][reference]` cases in `test_integration.cpp`, which were SKIP'd before #23 and PASS after #23 (41 assertions in 3 test cases). Neither the doc nor the BACKLOG ever had PR #23 covering the standalone binary's two SKIPs. The mistake is recorded in `feedback_pre_staged_docs_need_audit.md` and prompted this audit pass.

## Informational — SKIP-to-PASS transitions (not "Resolved" because they were never failing)

PR #23 advances the following from `SKIP()` to passing assertions; they were never in the Active set above (SKIPs aren't failures), but record the transition here so Phase 4 reconciliation can cite it:

- `tests/test_integration.cpp:274` — `"Reference mode pipeline: basic detection" [integration][reference]` — was `SKIP("TODO(test-fixtures): FIXTURE-REF...")`; now asserts against the synthetic vinyl rip.
- `tests/test_integration.cpp:345` — `"Reference mode pipeline: track positions within tolerance" [integration][reference]` — same.
- `tests/test_integration.cpp:397` — `"Reference mode pipeline: lossless export verification" [integration][reference][lossless]` — same.

Aggregate post-#23: 41 assertions across 3 test cases, all passing locally per the rebase fix-agent's verification. Does **not** flip the `test_integration` binary's status — the binary is still `Failed` because of `:479`, `:691`, and `:762` (NEW-BLIND-GAP and NEW-WAVEEXT-WRITE).

## Resolved entries

### `test_lossless` — C-1 AIFF stack-smash cluster (RESOLVED)

- **Cured by.** PR #27 (C-1), merge commit `<sha>` (fill in after merge).
- **Active entry archived.** Both `"AIFF header has correct structure"` and `"AIFF header has correct parameters"` now pass; `encode_float80` writes 10 bytes correctly per IEEE 754; `build_aiff_header`'s `numSampleFrames` field is u32 per AIFF 1.3 spec; libsndfile reads the output across 6 sample rates (44.1/48/88.2/96/176.4/192 kHz) per the new `"AIFF sample-rate round-trip via libsndfile"` round-trip test.
- **CI evidence.** Run `24971898962` on `058cd7e` (PR #27 head): every job's `Test` step shows `6/10 Test #6: test_lossless ........ Passed`. Linux Release/Debug, macOS Release/Debug, and sanitizers (asan+ubsan) all confirm `test_lossless` no longer aborts and the AIFF TEST_CASEs pass; ASan does not trip on `encode_float80`.

## Job-level expectations

| Job | Build | Tests | Comments |
|---|---|---|---|
| `build / ubuntu-latest / Release` | green | red on entries above | test_lossless now Passed (post-#27) |
| `build / ubuntu-latest / Debug` | green | red on entries above | same |
| `build / macos-latest / Release` | green | red on entries above | test_lossless now Passed (post-#27) |
| `build / macos-latest / Debug` | green | red on entries above | same |
| `sanitizers (asan+ubsan)` | green | red on entries above | ASan no longer trips on encode_float80 (post-#27) |
| `clang-tidy` | red on style nits | n/a (job stops at clang-tidy) | Out of Mi-18 scope per mandate; tracked under N-1..N-12 / Mi-18-FU-* |

## Update protocol

When a PR merges that cures one or more entries:
1. Verify the cured tests now pass on post-merge main CI.
2. Move the entry from "Active known-failing" to "Resolved" with the merging commit hash and PR number.
3. Add a one-line entry under "Resolved" naming the test, the cure, and the merging commit.
4. Commit the doc update on main as part of the orchestrator paperwork.

When a new known-failing test surfaces (e.g. a Tier 1 fixture lands and reveals a new test that fails for an already-tracked reason):
1. Confirm the failure has a backlog item or in-flight PR. If neither, file a backlog item before adding the entry — never let a known-failing entry exist without a fix-path.
2. Add a new "Active known-failing" subsection.
3. Note in the commit: "expected red after PR #X merge; tracked here pending fix from <item>".

## Cross-references

- `BACKLOG.md` — primary source of truth for the items that fix these failures.
- `docs/tier2-rebase-plan.md` — uses this doc as the post-rebase CI gate definition.
- `docs/m14-scope.md` — separate, but same pre-staging pattern.
- `docs/deviations.md` — different concern (parser-output deviations, not test failures).
