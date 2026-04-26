---
name: Known failing tests on main
description: The expected-red test set on main (post-Mi-18). Each entry maps a failing test/case to the in-flight PR or backlog item that will fix it. The Tier 2 rebase plan's "CI green" gate is defined relative to this list.
type: project
---

# Known failing tests on main

**Baseline as of:** Post-Mi-18 merge (`d925176`+). Mi-18 made the build compile cleanly under `-Werror` on Linux GCC and macOS Apple Clang for the first time in this remediation cycle. With the build now green, the test runner reaches its first execution; the failures listed below are the **expected-red set** that the in-flight PRs and backlog items will progressively eliminate.

**Why this doc exists.** Without an authoritative known-failing list, the natural human heuristic during a rebase walk is "CI red ⇒ regression," which would halt every merge unnecessarily. The orchestrator's halt rules need a finer signal: **test red is acceptable iff every failing test name + line is on this list, and no new failures appear.** This file is that authoritative list.

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

### `test_lossless` — `AIFF header has correct structure` (`[lossless]`)

- **Location.** `tests/test_lossless.cpp:143`.
- **Assertion family.** `REQUIRE(header[i] == std::byte{...})` for FORM/AIFF chunk magic bytes, plus body assertions.
- **Why failing.** The test calls `build_aiff_header(2, 44100, 24, num_frames, data_size)`, which today still routes through pre-C-1 `encode_float80` for the sampleRate field. Pre-C-1 `encode_float80` writes 11 bytes into a 10-byte stack buffer and stack-smashes; sanitizer builds also flag this as ASan stack-buffer-overflow. The test was historically `[!shouldfail]`-tagged with a comment "temporarily disabled — investigate stack smash issue"; F-4 re-enabled it so the smash surfaces.
- **Cured by.** **PR #27 (C-1)**. C-1's commit rewrites `encode_float80` to the correct 10-byte IEEE 754 layout AND fixes `build_aiff_header`'s `numSampleFrames` field type from float80 to u32 per the AIFF 1.3 spec. Both audit passes verified the 6-rate AIFF round-trip; `test_lossless:143` should pass after #27 merges.
- **Build job impact.** Linux Release / Linux Debug / Linux sanitizers / macOS Release / macOS Debug — all show this failure when Mi-18-equivalent build flags compile cleanly. ASan additionally trips on the stack overflow under sanitizer build.

### `test_integration` — `Blind mode pipeline: integrated detection`

- **Location.** `tests/test_integration.cpp:479` — `CHECK(analysis.split_points.size() >= 2)`.
- **Assertion intent.** A clean 2-track synthetic vinyl rip should produce ≥1 gap and therefore ≥2 split points. Comment in source explicitly says: "if fixture noise is genuinely too high, the fix is to regenerate with a lower noise floor — not to silently accept the error path."
- **Why failing.** Blind-mode pipeline returns only 1 split on the current synthetic 2-track fixture. The defect is documented as **NEW-BLIND-GAP** in BACKLOG.md (Tier 6 — Algorithmic correctness / blind-mode tuning). The score-gap thresholding currently misclassifies the inter-track silence as below-threshold for some fixture configurations.
- **Cured by.** **NEW-BLIND-GAP** (BACKLOG.md, Tier 6). Not in the current Tier 1+2 queue. Will remain failing after Tier 1+2 lands.

### `test_integration` — `WAVE_FORMAT_EXTENSIBLE 24-bit 2ch round-trip`

- **Location.** `tests/test_integration.cpp:691` — `REQUIRE(export_result.has_value())`.
- **Assertion intent.** Round-trip a 48 kHz, 2-channel, 24-bit WAVE_FORMAT_EXTENSIBLE file: open → write_track → re-open. The export must succeed.
- **Why failing.** `write_track` does not currently support WAVE_FORMAT_EXTENSIBLE (`0xFFFE`) output. The defect is documented as **NEW-WAVEEXT-WRITE** in BACKLOG.md, anticipated to be subsumed by **M-3** (Tier 4 — Parser hardening, WAVE_FORMAT_EXTENSIBLE support).
- **Cured by.** **M-3** (BACKLOG.md, Tier 4). Not in the current Tier 1+2 queue. Will remain failing after Tier 1+2 lands.
- **Note.** PR #25 (FIXTURE-WAVEEXT) lands the *fixture* but not the *write* path; this test will continue failing in the same way after #25 merges. M-3's PR is the cure.

### `test_integration` — `Blind mode end-to-end with reference cross-check`

- **Location.** `tests/test_integration.cpp:762` — `CHECK(blind_result.value().split_points.size() >= 2)`.
- **Assertion intent.** Same `≥2 split points` invariant as line 479, in a different test case that also calls `analyze_reference_mode` for a side-channel sanity check.
- **Why failing.** Same defect as line 479 — NEW-BLIND-GAP. The two test cases exercise different fixture variants but both bottom out on the same blind-mode threshold flaw.
- **Cured by.** **NEW-BLIND-GAP** (BACKLOG.md, Tier 6). Same as line 479. Will remain failing after Tier 1+2 lands.

### `test_reference_mode` — multiple cases failing collectively

- **Location.** `tests/test_reference_mode.cpp` (whole binary returns non-zero).
- **Assertion intent.** Reference-mode alignment of synthetic vinyl rip against ground-truth track boundaries.
- **Why failing.** Existing reference fixtures use tones-in-noise, which reference mode cannot reliably align against (no distinctive envelope shape, rhythmic tones produce ambiguous correlation peaks). Tests that depend on this currently `SKIP()` or assert into the no-result path.
- **Cured by.** **PR #23 (FIXTURE-REF)**. The fixture-agent generates a synthetic vinyl rip with distinctive per-track envelopes; once #23 merges, the previously-SKIP'd cases run with real expectations. The whole-binary failure should drop to PASS or the residual SKIPs documented per case.

## Resolved entries

*(Empty as of post-Mi-18; populate when Tier 1+2 PRs merge.)*

## Job-level expectations

| Job | Build | Tests | Comments |
|---|---|---|---|
| `build / ubuntu-latest / Release` | green | red on entries above | C-1 fixes test_lossless:143 |
| `build / ubuntu-latest / Debug` | green | red on entries above | same |
| `build / macos-latest / Release` | green | red on entries above (no test_lossless on macOS — see note) | same |
| `build / macos-latest / Debug` | green | red on entries above | same |
| `sanitizers (asan+ubsan)` | green | red on entries above + ASan trip on test_lossless:143 | The ASan trip is part of the same stack-smash; cured by C-1 |
| `clang-tidy` | red on style nits | n/a (job stops at clang-tidy) | Out of Mi-18 scope per mandate; tracked under N-1..N-12 / Mi-18-FU-* |

**Note on test_lossless on macOS.** The audit-agent's pass-2 macOS check reported only `test_reference_mode` and `test_integration` as failing on macOS Debug, not `test_lossless`. This is because `[lossless]` AIFF tests time out under macOS Catch2 differently or the runner picks them up differently — investigate-and-confirm during the C-1 rebase. If `test_lossless:143` does NOT fail on macOS, the C-1 round-trip verification was incomplete on the lenient platform and post-#27 should explicitly re-confirm 6-rate AIFF round-trip on macOS, not just Linux.

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
