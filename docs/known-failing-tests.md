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

**(empty — no Active known-failing entries on `main` as of `7c0bc4a`. First fully-clean Active list of the remediation cycle.)**

## Informational — SKIP-to-PASS transitions (not "Resolved" because they were never failing)

PR #23 advances the following from `SKIP()` to passing assertions; they were never in the Active set above (SKIPs aren't failures), but record the transition here so Phase 4 reconciliation can cite it:

- `tests/test_integration.cpp:274` — `"Reference mode pipeline: basic detection" [integration][reference]` — was `SKIP("TODO(test-fixtures): FIXTURE-REF...")`; now asserts against the synthetic vinyl rip.
- `tests/test_integration.cpp:345` — `"Reference mode pipeline: track positions within tolerance" [integration][reference]` — same.
- `tests/test_integration.cpp:397` — `"Reference mode pipeline: lossless export verification" [integration][reference][lossless]` — same.

Aggregate post-#23: 41 assertions across 3 test cases, all passing locally per the rebase fix-agent's verification. Did **not** flip the `test_integration` binary's status at the time — the binary remained `Failed` because of `:479` and `:762` (NEW-BLIND-GAP). Post-NEW-BLIND-GAP merge `7c0bc4a` (PR #48), all `test_integration` assertions pass and the binary exits 0 on every CI variant.

## Resolved entries

### `test_integration` — TEST_CASE `"Blind mode pipeline: gap detection"` (RESOLVED)

- **File.** `tests/test_integration.cpp:492` for the TEST_CASE; failing assertion was at `:525` on main pre-cure.
- **Assertion.** `CHECK(analysis.split_points.size() >= 2)` — "≥1 gap => ≥2 tracks".
- **Cured by.** **NEW-BLIND-GAP**, PR #48 (merge `7c0bc4a`). Root cause was a parameter-semantic mismatch in `score_gap`: the 5th parameter `noise_floor_rms` was misnamed and `analyze_blind_mode` was passing the noise-floor estimate, but the formula `1 - gap_rms / ref` only yields meaningful confidence when `ref` is a SIGNAL reference level. On a fixture where silence dominates the signal duration (~42% in this fixture), the noise-floor estimate equals the gap RMS by construction and the formula degenerates to 0, rejecting every detected gap. Cure renamed the parameter to `signal_reference_rms` and added a caller-side estimator (p90 of frame RMS) in `analyze_blind_mode`. Empirical post-cure: `test_integration` passes 11/11 cases / 73 assertions on every CI variant.
- **Cure-attribution catch (informational).** Original BACKLOG NEW-BLIND-GAP exit criterion 2 named the gating tests as "(`clear silence detection`, `combined workflow`)", but `clear silence detection` (`test_integration.cpp:528`) uses soft `if`-conditional checks rather than hard CHECKs and never failed the binary even pre-cure. The actual hard-gating test alongside `combined workflow` was `gap detection` (this entry). Cross-doc reconciliation slip caught by audit-1; tightened in the close-out paperwork commit.

### `test_integration` — TEST_CASE `"Combined workflow: reference then blind analysis"` (RESOLVED)

- **File.** `tests/test_integration.cpp:712` for the TEST_CASE; failing assertion was at `:769` on main pre-cure.
- **Assertion.** `CHECK(blind_result.value().split_points.size() >= 2)` — same `≥2 split points` invariant as the gap-detection case.
- **Cured by.** **NEW-BLIND-GAP**, PR #48 (merge `7c0bc4a`). Same defect as the gap-detection case above; both test cases bottomed out on the same `score_gap` parameter-semantic mismatch and both pass post-cure.

### `test_reference_mode` — standalone binary returns non-zero (RESOLVED)

- **Cured by.** **Two cure axes across three items, all landed:**
  - **Binary-exit-code axis (Catch2 semantic).** Cured by **C-4** (PR #41, merge `e519bf6`). C-4 added a passing TEST_CASE `"Reference mode: native-rate boundary is rounded not truncated"` to `tests/test_reference_mode.cpp` to verify its rate-conversion rounding helper. Catch2 returns 0 when at least one case passes alongside SKIPs, so the binary's exit code flipped Failed→Passed at C-4's merge — independently of whether the SKIP cluster had been resolved. Empirically verified post-C-4 audit-1: `test_reference_mode` reported `1 passed | 2 skipped, exit=0`. The cure mechanism is structural (TEST_CASE count > 0 with at least one pass), not semantic (the SKIPs themselves still skipped at that point).
  - **SKIP-cluster axis (placeholders becoming real assertions).** Cured by **M-REF-ALIGN-UNIT + Mi-17 paired** (PR #46, merge `4d542d3`). The two SKIPs at `tests/test_reference_mode.cpp:18` (per-track-alignment placeholder, post-Mi-4 line drift; was `:14`) and `:24` (natural-filename-sort placeholder; was `:20`) were replaced with real assertions:
    - M-REF-ALIGN-UNIT replaced `:18` with an `align_per_track`-direct assertion against the FIXTURE-REF v1 manifest at `tests/fixtures/ref_v1/manifest.txt`, with tolerance `kRefFixtureToleranceSamples = 2205` matching the integration-test counterpart and `static_assert`'d at compile time.
    - Mi-17 replaced `:24` with primary-invariant assertion (`natural_less("Track 2.wav", "Track 10.wav")` plus decade-boundary cases) and added a separate TEST_CASE for the overflow-doesn't-throw axis on 25-digit, 21-digit, and mixed pathological inputs. The natural_less function was hardened from std::stoll to length-then-lex compare on zero-stripped digit strings.
  - Post-paired-merge: 5 cases / 47 assertions, all pass; 0 SKIPs remain. Binary continues to exit 0 (denser pass count than C-4 era; same exit signal).
- **Cure-attribution history (worth preserving — informed the cycle's "audit pre-staged docs along every axis" discipline).**
  1. **First catch (pre-staged version, PR #23 era).** The doc claimed PR #23 cures `test_reference_mode`. PR #23's BACKLOG-stated scope was the three `[integration][reference]` cases in `test_integration.cpp` (which were SKIP'd before #23 and PASS after #23: 41 assertions in 3 test cases). Neither the doc nor the BACKLOG ever had PR #23 covering the standalone binary's two SKIPs. Recorded in `feedback_pre_staged_docs_need_audit.md`.
  2. **Second catch (C-4 dispatch, 2026-05-04).** The post-first-catch attribution to M-REF-ALIGN-UNIT + Mi-17 was correct for the SKIP cluster but missed that the binary-exit-code claim cures inadvertently on any earlier item that adds a passing TEST_CASE to the binary. C-4 surfaced this because its new TEST_CASE landed in `tests/test_reference_mode.cpp` and flipped the binary's exit code before either M-REF-ALIGN-UNIT or Mi-17 dispatched. Cure-attribution sweep rule (`feedback_pre_dispatch_checklist.md`) caught the drift via the C-4 fix-agent's observation; orchestrator gate-evaluated and audit-1 confirmed empirically. Third reinforcing instance of cure-attribution sweep earning rent in the cycle (M-3/`:728`, M-4-FU coverage, this); promoted cure-attribution sweep from per-item special case to standard pre-dispatch checklist item per `feedback_pre_dispatch_checklist.md`.

### `test_integration` — TEST_CASE `"Lossless end-to-end: verify exported file formats"` (RESOLVED)

- **Cured by.** **`INT-728-FIXTURE-MISMATCH` option (c)** — TEST_CASE dropped in commit `3a86871` (one-line file change to `tests/test_integration.cpp`, removing former lines 699–736). The local `create_test_wav` helper at `:101` was retained because seven other TEST_CASEs call it.
- **Why option (c).** Coverage was fully subsumed by `tests/test_lossless.cpp:416–474` (`"Lossless: 24-bit 2-ch extensible WAV round-trip preserves bytes"` — opens FIXTURE-WAVEEXT 24-bit / 2-ch / 48 kHz EXTENSIBLE artifact, calls `write_track` over a deterministic mid-file region, re-opens, CHECKs `channels` / `sample_rate` / `bits_per_sample` / `frames`, and `REQUIRE`s data-byte identity). Plain-PCM bit-depth round-trip is independently in `tests/test_lossless.cpp:357` (`"Lossless export with different bit depths"` — 16/24/32) and `:223` (`"Lossless round-trip preserves exact bytes"` — stereo 24-bit byte-identity). The dropped TEST_CASE name (`"verify exported file formats"`) was always broader than the body (single fixture, plain PCM, no format-identity assertion). Subsumption verified line-by-line by independent coverage-audit agent and user before merge.
- **Why options (a) and (b) were ruled out.** (a) would have left the TEST_CASE name lying about what it tested (plain PCM, despite the EXTENSIBLE-implying name). (b) required filing a `M-3-EMIT` write-side EXTENSIBLE item that does not yet exist; deferring (c) on speculation about a future item was the wrong cost/benefit.
- **Doc-drift note.** Prior to the (c) decision, this Active entry listed only options (a) and (b); the third option (drop) was present in `BACKLOG.md` but never propagated here. The drift was caught by the coverage-audit agent at decision time and is recorded as `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1` in `docs/deviations.md`. The orchestrator playbook now requires a cross-doc reconciliation pass at decision time when two governance docs reference the same item.

### `test_lossless` — C-1 AIFF stack-smash cluster (RESOLVED)

- **Cured by.** PR #27 (C-1), merge commit `<sha>` (fill in after merge).
- **Active entry archived.** Both `"AIFF header has correct structure"` and `"AIFF header has correct parameters"` now pass; `encode_float80` writes 10 bytes correctly per IEEE 754; `build_aiff_header`'s `numSampleFrames` field is u32 per AIFF 1.3 spec; libsndfile reads the output across 6 sample rates (44.1/48/88.2/96/176.4/192 kHz) per the new `"AIFF sample-rate round-trip via libsndfile"` round-trip test.
- **CI evidence.** Run `24971898962` on `058cd7e` (PR #27 head): every job's `Test` step shows `6/10 Test #6: test_lossless ........ Passed`. Linux Release/Debug, macOS Release/Debug, and sanitizers (asan+ubsan) all confirm `test_lossless` no longer aborts and the AIFF TEST_CASEs pass; ASan does not trip on `encode_float80`.

## Job-level expectations

Post-NEW-BLIND-GAP merge `7c0bc4a` (PR #48), the Active known-failing list is empty for the first time in the remediation cycle. All build/test/sanitizer jobs are expected to pass; only `clang-tidy` remains expected-red on style nits.

**Update 2026-06-07 (post-#73).** The `clang-tidy` row in the table below is **superseded**. PR #73 (Mi-11) cleared the clang-tidy baseline; the job has been green on every merge since — verified all-six-green on main HEAD `d650c3c` (`clang-tidy` = success), and #73/#74/#75/#76 each merged on a full six-green run including `clang-tidy`. The merge gate is now **all six jobs green, including `clang-tidy`** — it is no longer out-of-scope, and the "Do not wait for clang-tidy green" guidance from the Mi-18 cycle no longer applies. The "red on style nits" status in the table reflects the historical #48 snapshot only.

| Job | Build | Tests | Comments |
|---|---|---|---|
| `build / ubuntu-latest / Release` | green | green (12/12 binaries pass) | test_integration fully green post-#48 |
| `build / ubuntu-latest / Debug` | green | green (12/12 binaries pass) | same |
| `build / macos-latest / Release` | green | green (12/12 binaries pass) | same |
| `build / macos-latest / Debug` | green | green (12/12 binaries pass) | same |
| `sanitizers (asan+ubsan)` | green | green (12/12 binaries pass) | ASan no longer trips on encode_float80 (post-#27); blind-mode confidence path UBSan-clean post-#48 |
| `clang-tidy` | green since #73 | n/a | **Superseded — see the 2026-06-07 update above.** Baseline cleared in #73; gate is now all-six-green incl. `clang-tidy`. (Was red / out-of-scope through the Mi-18 cycle.) |

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
