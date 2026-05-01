---
name: Orchestrator handoff at end of Tier 4
description: Handoff artifact for the next orchestrator. Reads with the existing on-disk governance docs (BACKLOG.md, docs/known-failing-tests.md, docs/deviations.md, docs/invariants.md, docs/decisions/parser-errors.md, docs/decisions/expected-api.md, the in-cycle playbook at ~/.claude/projects/.../memory/) to resume operation at the same calibration level the cycle reached.
type: project
---

# Orchestrator handoff — end of Tier 4

**Authored.** 2026-05-01 by the outgoing orchestrator at session end. Main HEAD `9fea7ae`.

**Reading order.** This artifact is additive — it does not restructure or supersede any existing governance doc. Read it after the on-disk inventory (`BACKLOG.md`, `docs/known-failing-tests.md`, `docs/deviations.md`, `docs/invariants.md`, `docs/decisions/parser-errors.md`, `docs/decisions/expected-api.md`, `docs/m14-scope.md`, `docs/m-i-18-followups.md`, `docs/tier2-rebase-plan.md`) and after the in-cycle playbook (the feedback-memory directory at `~/.claude/projects/-Users-mwoolly-projects-mwAudioAutoChop-C-/memory/`, indexed by `MEMORY.md` in that directory).

The artifact's job is the **why** and **calibration** layers — the things a fresh orchestrator cannot infer from rule statements and resolved-marker lists.

---

## 1. Cycle state at handoff

### Main HEAD

`9fea7ae` (2026-04-30) — `Mi-1 paperwork close-out — RESOLVED in #37`.

### Landed by epic, with merge SHAs

**Phase 0 — Foundation.** Items `F-1` through `F-5` (build harness, sanitizer/Werror infrastructure, CI matrix, AIFF test re-enabling, CHECK(true) neutralization). Pre-dates this orchestrator's cycle.

**Tier 1 — Test fixtures.** `FIXTURE-REF` (PR #23, `c904d95`), `FIXTURE-RF64` (PR #24, `a4e9b83`), `FIXTURE-WAVEEXT` (PR #25, `d29e6b5`), `FIXTURE-MALFORMED` (PR #26, `0036350`).

**Tier 2 — Critical correctness.** `C-1` (PR #27, `0bf13a1` — encode_float80 + AIFF-INLINE-SCOPE inline), `M-16` (PR #28, `82a774a` — atomic write), `C-2` (PR #29, `083431c` — Expected precondition + main.cpp guards). `M-15` was subsumed by C-2 (no separate dispatch). **`C-3`, `C-4`, `C-5`, `M-2` remain pending in Tier 2** — see §2 below; these are not Tier-4 leftovers, they are earlier-tier items that were never dispatched.

**Tier 3 — Contract unification.** `M-14` (PR #31, `f052f89` — variant-backed `Expected<T,E>`, `LoadResult` removed). M-14 close-out paperwork (PR #32, `29c9665` — minor doc fixes in `audio_file.hpp`).

**Tier 9 spike — Mi-3.** `Mi-3` (PR #33, `8af5793`) — structural cure (`Expected<AudioBuffer, AudioError>`, `ResampleError` given real producers). Deviation from original "early return `{}`" spec recorded in `docs/deviations.md`.

**Tier 4 — Parser hardening (the cycle this handoff closes).**
- `M-3` — WAVE_FORMAT_EXTENSIBLE PCM/IEEE-float SubFormats accepted (PR #34, `70a7745`).
- `INT-728-FIXTURE-MISMATCH` — option (c) drop of `tests/test_integration.cpp` `"Lossless end-to-end: verify exported file formats"` TEST_CASE (drop commit `3a86871`, three-doc reconciliation `dd2b879`).
- `M-4` — `parse_wav_header` recovers `data_size` for ds64-after-data RF64 via `AudioFile::open` head+tail splice + tail-scan (PR #35, `039347e`).
- `M-5` — `parse_aiff_header` honors SSND offset; OOB → `InvalidFormat` (PR #36, `a1654a1`).
- `Mi-1` — `parse_aiff_header` decodes sampleRate from float80; reads `bits_per_sample` at correct COMM offset `+14` (was `+18`) (PR #37, `b1e9edd`).
- `AIFF-INLINE-SCOPE` — already closed during C-1 (audit-1 + audit-2 evaluated and accepted the inline scope expansion). BACKLOG entry shows `STATUS: [x] closed`.

Paperwork-only commits on main (no PR) during the cycle: `2c72ff4` (parser-errors.md landing), `2e6b1fa` and `cf719c3` and `5144fa9` and `0d9d43c` (Mi-3 sub-commits before merge — these are visible in `git log` but were squashed/merged via PR #33), `772dc77` (post-M-3 paperwork including `KNOWN-FAILING-CURE-ATTRIBUTION-V1` deviation), `ca3d200` (M-3 deviation hash fill-in), `5b99a17` (stale `[!shouldfail]` comment + dead `slurp_full_file` tidy), `dd2b879` (INT-728 three-doc reconciliation), `8f41d86` (M-4 paperwork close-out — stale comment + FU items + dual-ds64 deviation), `12fd898` (M-5 paperwork — RESOLVED + Mi-1 scope expansion), `1bd452e` (Mi-1 pre-dispatch correction — libsndfile bits_per_sample claim corrected before dispatch), `9fea7ae` (Mi-1 paperwork close-out).

### Filed but not dispatched

- **`M-4-FU-TAILSCAN`** — RF64 tail-scan false-match window narrowing. One-line correctness change at `src/core/audio_file.cpp:493` (start scan at head/tail boundary, not `data_chunk_payload_start`); regression fixture exercises in-head false `ds64` match. Filed at M-4 paperwork close-out (`8f41d86`).
- **`M-4-FU-COVERAGE`** — RF64 ds64-after-data via `AudioFile::open` splice (production path, not the helper-direct path the cure-attribution test exercised). New TEST_CASE only; no production code change expected. Filed at M-4 paperwork close-out (`8f41d86`).

### Mi-18 — partial; tracked separately

`Mi-18` (mechanical `-Werror` cleanup, one PR per TU) is in flight. `audio_buffer.cpp` pass landed (PR #30, `b9f9508`). Remaining TUs listed in `BACKLOG.md` Mi-18 block. Follow-up `MI18-FOLLOWUP-BLIND-ITER` filed.

### Deferred housekeeping (not blocking)

- 11 stale git worktrees on disk under `/private/tmp/mwaac-*` and `.claude/worktrees/agent-*`. Some are `prunable` per `git worktree list`. The `.claude/worktrees/agent-*` paths are locked to branches that the merge-branch-delete couldn't remove because the worktree held the branch. Not blocking; safe to clean at any cycle boundary.
- `docs/known-failing-tests.md:96` — C-1 AIFF stack-smash cluster Resolved entry has `<sha>` placeholder where the merging commit (`0bf13a1`) should be. Cosmetic; cleanest to fix during whatever paperwork pass next opens that doc.

---

## 2. Active backlog with explicit next-step ordering

### Immediate block (per outgoing user direction at handoff)

**Dispatch as a small block before Tier 5 opens:**

1. **`M-4-FU-TAILSCAN`** — Tier 4 follow-up; correctness-class. Single-audit per the one-item-one-PR-one-audit treatment the user specified at M-4 close-out. Pre-dispatch verifications already known to be needed:
   - Confirm the splice's head/tail boundary is exposed cleanly enough for `parse_wav_header` to compute it without re-reading the splice metadata (audit-2 of M-4 noted the production splice and the test helper are independently transcribed; the boundary value lives in both).
   - The new regression fixture must be added under `tests/fixtures/rf64/` or generated inline; either is acceptable. If a new fixture file lands, manifest entry must be added atomically.
2. **`M-4-FU-COVERAGE`** — Tier 4 follow-up; coverage-class. Single-audit. New TEST_CASE only; no production code change. Pre-dispatch verifications already known to be needed:
   - The cure-attribution test for M-4 (`"parse_wav_header: RF64 with ds64 after data"` at `tests/test_lossless.cpp:744`) calls the helper directly. The new TEST_CASE must call `AudioFile::open` to exercise the production splice path, and then read `audio_file.value().info()` for the manifest assertion. Verify this delta is real (not "the existing test already covers it via some path I missed") before dispatching.

### Next epic — Tier 5 (Algorithmic correctness)

**The user has signalled the governing prompt will be refreshed for Tier 5's audit shape before dispatch begins.** Do not dispatch Tier 5 items autonomously without that refresh — the audit pattern for algorithmic correctness is materially different from parser hardening (see §5).

**Tier 5 items in BACKLOG.md (as of `9fea7ae`):**

- **`M-9`** — `std::clamp` with `hi < lo` when vinyl is empty (`src/modes/reference_mode.cpp:994`). Invariant: `align_per_track` skips against empty vinyl. Files: `src/modes/reference_mode.cpp`, `tests/test_reference_mode.cpp`. New TEST_CASE: `align_per_track: empty vinyl returns empty offsets, no UB`.
- **`M-10`** — `compute_zero_crossing_rate` divides by zero when `end - start == 1` (`src/core/analysis.cpp:68`). Invariant: ZCR = 0 for frames < 2 samples. Files: `src/core/analysis.cpp`, `tests/test_analysis.cpp`. New TEST_CASE: `compute_zero_crossing_rate: single-sample frame returns 0, not NaN`.
- **`Mi-4`** — naive `cross_correlate` normalization documentation. Header doc-only fix; no production code change. Files: `src/core/correlation.hpp`, `src/core/correlation.cpp`. Re-verify existing `cross_correlate and cross_correlate_fft agree on lag` test post-comment.
- **`M-REF-ALIGN-UNIT`** — un-SKIP `tests/test_reference_mode.cpp:14` (per-track alignment unit test). Tolerance constant must be named (`kRefFixtureToleranceSamples` or similar) and match PR #23's integration-test tolerance. **Combined with `Mi-17`, this flips the `test_reference_mode` binary's exit status from `Failed` to `Passed`.**

**Tier 2 latent items (still pending, not Tier 4 leftovers):**

These are Tier 2 by classification but were never dispatched. Surface to the user before deciding whether they fold into Tier 5 or get their own block:

- **`C-3`** — RF64 output silently truncated above 4 GiB. `build_wav_header` writes `file_size`/`data_size` as `uint32_t`. write_track never emits RF64. Depends on FIXTURE-RF64 (landed). Removes the `[!shouldfail]` tag on `RF64 round-trip: sample region byte-identical` (`tests/test_lossless.cpp:801`). **Active known-failing today** via that `[!shouldfail]` tag.
- **`C-4`** — Reference-mode rate conversion truncates instead of rounds (`src/modes/reference_mode.cpp:1109, 1111`). Invariant: boundaries within one native-rate sample of analysis-rate result. README "sample-accurate" claim depends on this (DOC-1).
- **`C-5`** — `compute_spectral_flatness` unsigned wrap + stub returns. Either real FFT-based impl (using vendored pocketfft) or remove from public header.
- **`M-2`** — `AudioFile::open` does not cross-check parser vs libsndfile. Depends on FIXTURE-MALFORMED (landed). Will flip `data_size_overflows_file.wav` in `tests/fixtures/malformed/manifest.txt` from `InvalidFormat-pending-M-2` to `InvalidFormat`.

### Tier 6 — API hygiene

- **`M-6`** — `score_gap` units ambiguous (`src/modes/blind_mode.cpp:57-93`). Sample-index vs frame-index types non-disjoint.
- **`M-7`** — `score_gap` ignores `sample_rate` parameter (`[[maybe_unused]]`). Either use it or remove it.
- **`M-8`** — Blind mode returns error on single-track rips (`NoGapsFound` is a legitimate outcome).
- **`M-11`** — `LoadResult` default-ctor ambiguous. **Closed as duplicate of M-14** (M-14 removed `LoadResult` entirely). Mark `[x]` in BACKLOG when next paperwork pass touches the file.
- **`Mi-7`** — duplicate of `M-7`. Same resolution.
- **`NEW-BLIND-GAP`** — Blind mode returns only 1 split on clear 2-track fixture (`test_integration.cpp:479` and `:762` — both in the Active known-failing set). Likely fix in `src/modes/blind_mode.cpp` or `src/core/music_detection.cpp`. **This is what cures the two NEW-BLIND-GAP entries in `docs/known-failing-tests.md` Active set.**

### Tier 7 — TUI invariants

- **`Mi-8`** — TUI marker nudge breaks `start ≤ end` invariant (`src/tui/app.cpp:191-204`). Needs headless TUI test harness; absent today.
- **`Mi-9`** — TUI view bounds can invert (`src/tui/app.cpp:208-241`).
- **`Mi-10`** — `run_tui` exit-code documentation/behavior mismatch (`src/tui/app.cpp:271`).

### Tier 8 — Documentation, attribution, hygiene

- **`M-12`** — FFTW3 dead. BACKLOG says "already resolved in Phase 0.3 (CMake + CI). Close on audit-agent verification." Trivial close-out.
- **`M-13`** — pocketfft attribution missing. Add `THIRD_PARTY_LICENSES.md` + README acknowledgment.
- **`Mi-5`** — Magic threshold soup in `src/modes/reference_mode.cpp:584-585` and ~12 other sites.
- **`DOC-1`** — README "sample-accurate" claim. Depends on `C-4`'s tolerance.
- **`DOC-2`** — `PROJECT_SPEC.md` reconciliation with CMakeLists.txt.
- **`DOC-3`** — `docs/invariants.md` is the living document. Currently maintained by orchestrator paperwork; per BACKLOG, by invariant-agent every 3-5 completed items. The doc exists and is up-to-date through Tier 4.

### Tier 9 — Cleanup

- **`Mi-2`** — `compute_rms_energy` guard order fragility.
- **`Mi-6`** — `min` identifier shadows `std::min`.
- **`Mi-11`, `Mi-12`, `Mi-13`** — dead-code deletions (`tests/test_deps.cpp`, `src/core/core.hpp`, `verbose.hpp g_timer_start`).
- **`Mi-14`** — verbose globals not thread-safe; promote to `std::atomic<bool>` or pass `Logger&`.
- **`Mi-15`** — explicit ctors audit. **Resolved by M-14.** Mark `[x]` next paperwork pass.
- **`Mi-16`** — `encode_float80` NaN/over-/under-flow handling. Documentation/assert work; not user-visible. Surfaced by C-1 audit-2.
- **`Mi-17`** — `std::stoll` in `natural_less` can throw on >18-digit runs. Combined with `M-REF-ALIGN-UNIT`, flips `test_reference_mode` to `Passed`.
- **`F-AUDIT2-1`** — C-2 integration test exercises actual guard end-to-end (uses FIXTURE-WAVEEXT). Single-PR.
- **`F-AUDIT2-3`** — Move `MWAAC_ASSERT_PRECONDITION` to shared header. Deferred until second consumer.
- **`F-AUDIT2-DT`** — Death-test harness extraction. Deferred until second consumer.
- **`Mi-18`** — TU-by-TU `-Werror` cleanup. `audio_buffer.cpp` landed (PR #30); other TUs pending. Each TU is a ≤30-line PR with one audit.
- **`MI18-FOLLOWUP-BLIND-ITER`** — defensive cast on `src/modes/blind_mode.cpp:73-74` iterator+size_t arithmetic. ≤5-line diff, no audit.
- **`N-1` through `N-12`** — Nits ride along with their enclosing TU's Mi-18 pass.

### Active known-failing on main (per `docs/known-failing-tests.md`)

- `test_integration` — `"Blind mode pipeline: gap detection"` `[integration][blind]`. Cure: `NEW-BLIND-GAP` (Tier 6).
- `test_integration` — `"Combined workflow: reference then blind analysis"` `[integration][combined]`. Cure: `NEW-BLIND-GAP` (Tier 6).
- `test_reference_mode` — standalone binary returns non-zero (both TEST_CASEs are `SKIP()`). Cure: `M-REF-ALIGN-UNIT` (Tier 5) + `Mi-17` (Tier 9).
- `clang-tidy` job — documented out-of-scope (Mi-18-FU-* tracked).

INT-728-FIXTURE-MISMATCH is in the Resolved section.

The post-Mi-18 baseline gate ("compile green + tests red iff documented + no regression") reads against this strictly-smaller set. Use it on every Tier 5+ PR per the same rule.

---

## 3. Earned playbook rules with provenance

The in-cycle playbook is the feedback-memory directory at `~/.claude/projects/-Users-mwoolly-projects-mwAudioAutoChop-C-/memory/`, indexed by `MEMORY.md` in that directory. It is not a single on-disk doc despite occasional "playbook v2" framing in user messages — that framing refers to the collected memories, not a separate file.

12 memories total, 4 promoted during this Tier 4 cycle. Listed in the order they were promoted within this cycle, with provenance.

### Pre-cycle (load-bearing through this cycle)

1. **`feedback_halt_on_red_baseline.md`** — Halt and surface on red CI baseline.
2. **`feedback_pending_ci.md`** — Pending CI is not actionable; treat as red for merge-gate purposes.
3. **`feedback_pre_staged_docs_need_audit.md`** — Pre-staged docs need an audit pass before being declared gate-authoritative. Three catches before this cycle (`KNOWN-FAILING-COMPLETENESS-V1`, `KNOWN-FAILING-SCHEMA-V2`, the conflation in PR #23).
4. **`feedback_stop_at_epic_boundary.md`** — Stop at epic boundary even when CI is clean. The human checkpoint at the boundary adds signal.
5. **`feedback_halt_discipline_is_the_strength.md`** — Halt-on-uncertainty is the source of strength. Don't read a velocity streak as permission to relax gates. Price false halts that improve gate design, don't penalize them.
6. **`feedback_close_followups_before_next_epic.md`** — Sweep should-fix leftovers into a small close-out PR before opening the next epic. This rule fired at the M-3 → M-4 boundary (close-out for the stale `[!shouldfail]` comment) and at the M-4 close (paperwork commit covered the stale `:787-789` comment + dual-ds64 deviation + filed FU items).
7. **`feedback_cross_reference_existing_backlog.md`** — Before calling something "latent bug nearby", grep BACKLOG.md for an existing ID. Without the cross-reference, scope creep hides as discovery.
8. **`feedback_verify_scope_claims_pre_dispatch.md`** — Before dispatching, grep concrete scope claims in the mandate against the working tree. Applies to user-supplied claims as much as orchestrator-pre-staged ones — user explicitly endorsed surfacing scope errors back. Origin: Mi-3 dispatch where the user said "one caller" but `tests/test_audio_buffer.cpp:19` was a second caller.

### Promoted during this cycle (M-3 → INT-728 → M-4 → M-5 → Mi-1)

9. **`feedback_audit_pre_staged_docs_along_every_axis.md`** — Pre-staged orchestrator/governance docs need audits along every axis they assert claims on, not just the most visible axis.
   - **Earning catch.** M-3 fix-agent halt on `test_integration:728`. Three earlier audits of `docs/known-failing-tests.md` had swept test-identity, line-shift schema, and AIFF cluster axes; nobody had swept the cure-attribution axis. The cited cure (`M-3` for `:728`) was wrong — `:728` failed for a test-side arithmetic bug. Recorded in `docs/deviations.md` as `KNOWN-FAILING-CURE-ATTRIBUTION-V1`.
   - **Formulation that landed.** For each axis the doc asserts on, the audit reads the local check and reasons whether the mechanism actually addresses the failure. "Same area of code" is not enough; "same file format" is not enough; the semantic chain must connect.
10. **`feedback_pre_dispatch_checklist.md`** — Standard pre-dispatch checklist: scope-claim verification, test-identity verification, cure-attribution sweep on `docs/known-failing-tests.md`, adjacent-entry sweep. All four fire on every fix-agent dispatch.
    - **Earning catch.** Promoted at M-4 dispatch as "make the M-3 axis-coverage rule mechanical for every Tier 4+ dispatch." Cure-attribution was elevated from per-item special case to standard.
    - **Formulation that landed.** Four checks, dispatch-blocking. A contradicting grep result is a halt signal.
    - **Note on rule-count ambiguity.** This memory could be read as a packaging-up of #9 (cure-attribution axis as one of four standard checks) rather than a distinct rule. The user's catch-count framing ("three earned playbook rules") may treat #9 + #10 as one rule. The orchestrator's framing treats them as two — #9 is the *axis-coverage principle*, #10 is the *operational checklist that includes one such axis*. See §6 below.
11. **`feedback_audit_halts_eval_against_gate.md`** — When an audit-agent raises a halt-class finding based on a signal that has a project-side gate, the orchestrator's first step is to evaluate against the gate definition before escalating. Escalate only if gate-eval is uncertain or contradicts the gate.
    - **Earning catch.** M-4 audit-2 raised `feedback_halt_on_red_baseline.md` based on GitHub Actions UI showing `FAILURE` on every lane. Step-level inspection showed `Build=success` everywhere; `Test=failure` on the documented-expected-red set; `clang-tidy=failure` documented out-of-scope. The gate was met. Audit-agents reason from raw observable surface; orchestrator has the gate-doc context.
    - **Formulation that landed (broader than the original CI-state framing per user direction).** Covers CI-state, deviations, follow-up items, out-of-scope clang-tidy, and any future signal with a project-side gate definition. Specific catch teaches general rule, same shape as #9.
12. **`feedback_cross_doc_reconciliation.md`** — When a decision touches an item referenced in multiple governance docs, run a cross-doc reconciliation pass at decision time before the close-out commit lands.
    - **Earning catch.** INT-728 close-out. `BACKLOG.md`'s `INT-728-FIXTURE-MISMATCH` entry listed three architectural options (a, b, drop). `docs/known-failing-tests.md`'s INT-728 entry listed only (a) and (b); option (c) was never propagated. Caught by the coverage-audit agent at decision time, not by within-doc audit. Recorded as `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1` in `docs/deviations.md`.
    - **Formulation that landed.** Different gap-family from #9 (within-doc axis coverage). Together they form a track-record signal: governance-doc drift is a real, recurring failure mode, not theoretical.

### Catches that did not promote rules

These were halt-discipline moments handled within the existing playbook and recorded in commit messages or PR bodies, not promoted to memory. Listed because the next orchestrator should know what *kinds* of catches stayed within rules vs earned new ones.

- **M-5 fix-agent's self-catch on the too-defensive OOB check.** Initially wrote `data_offset + data_size > data.size()` in `parse_aiff_header`'s SSND path; realized it would falsely reject all AIFFs >64 KiB because `data` is the 64 KiB header window, not the full file. Caught and removed pre-PR. No rule-promotion needed; this is the existing halt-discipline-is-the-strength rule applied to one's own code.
- **M-5 fix-agent's surface of a Mi-1-territory bug without scope-expansion.** While writing the new `non-zero SSND offset is honored` test, attempted to assert `bits_per_sample == 16` and got the wrong value (`bits_per_sample` was being read at the wrong COMM offset). Identified as Mi-1's territory, did NOT silently expand M-5's scope, surfaced in PR body and final report. The orchestrator added the bug to Mi-1's BACKLOG entry as a second exit criterion at M-5 paperwork close-out.
- **Mi-1 fix-agent's correction of orchestrator dispatch-envelope arithmetic.** The dispatch envelope claimed pre-Mi-1's `read_be_u16(+18)` returns 0 for 48 kHz/16-bit; actually returns `0xBB80` (the mantissa MSBs of float80(48000)). Fix-agent re-derived independently, corrected the test comment, judged the discrepancy comment-only and did NOT halt-and-surface, surfaced in final report instead. Audit re-derived all 6 PROJECT_SPEC rates and confirmed the discriminator argument is robust regardless.
- **Orchestrator self-catch on the libsndfile-overrides-bits_per_sample claim before Mi-1 dispatch.** The M-5 paperwork close-out (`12fd898`) had stated "libsndfile re-validates `bits_per_sample` at `AudioFile::open` time and overrides the parser's value." That claim was wrong — `src/core/audio_file.cpp:262-269` overrides only `sample_rate`, `channels`, `frames`, and `format`; `bits_per_sample` is *not* in that list. Caught at Mi-1 pre-dispatch by re-reading the override block; corrected `BACKLOG.md` in `1bd452e` before dispatching. The verify-scope-claims rule (#8) applied symmetrically to the orchestrator's own claim.

---

## 4. Track record

### Catch count (this cycle: M-3 → INT-728 → M-4 → M-5 → Mi-1)

User-counted: **8 catches across the cycle**. Orchestrator-enumerated catches: **7**. The discrepancy is itself a live ambiguity; see §6.

Enumerated catches:

1. **M-3 fix-agent halt on cure-attribution axis** (`test_integration:728` cited as `M-3`-cured; actual failure unrelated to EXTENSIBLE). Promoted rule #9.
2. **INT-728 coverage-audit catch on cross-doc option-set drift** (`BACKLOG.md` listed three options; `known-failing-tests.md` listed two). Promoted rule #12.
3. **M-4 audit-2 raised CI-state halt** (resolved by gate-eval). Promoted rule #11.
4. **M-5 fix-agent self-catch on too-defensive OOB check** (existing halt-discipline rule).
5. **M-5 fix-agent surface of Mi-1-territory bug** without scope expansion (existing halt-discipline rule).
6. **Mi-1 fix-agent correction of orchestrator dispatch-envelope arithmetic** (`0xBB80` vs `0`).
7. **Orchestrator self-catch on libsndfile-overrides-bits_per_sample claim** before Mi-1 dispatch.

The user's "8" likely includes the Mi-1 audit's non-blocking advisory on `decode_float80_to_u32`'s naming (returns uint32 but caps at INT32_MAX; doc comment states intent), or counts something earlier in the cycle differently. **Defer to the user's number; orchestrator-enumerated is for transparency.**

### Baseline shrink count

User-counted: **2 (M-3, INT-728)**.

- **M-3** — flipped 4 `[!shouldfail]` tags (3 in `tests/test_audio_file.cpp:151,168,185` + 1 in `tests/test_lossless.cpp:434`). These were not in the Active known-failing set per the gate (the gate uses Catch2 visible-fail, not `[!shouldfail]` reports), but the user counts the `[!shouldfail]`-set shrink as a baseline shrink. Orchestrator should match.
- **INT-728** — removed one entry from `docs/known-failing-tests.md` Active set. Strict Active-set shrink.

`M-4` flipped 1 `[!shouldfail]` (`tests/test_lossless.cpp:744` — `parse_wav_header: RF64 with ds64 after data`); arguable as a third shrink. `M-5` flipped 1 FIXTURE-MALFORMED pending entry (`aiff_ssnd_offset_nonzero.aiff` from `InvalidFormat-pending-M-5` to `InvalidFormat`) — also arguable. **Defer to user count.**

### Halt-shape distribution

The qualitative shift the user named at handoff: halts moved from mid-flight to pre-dispatch self-catches and within-PR halt-discipline moments. This is the proactive form, not decay.

- **Mid-flight halts (fix-agent halts during dispatch).** 1: M-3 (cure-attribution).
- **Pre-dispatch self-catches (orchestrator caught before envelope went out).** 1: libsndfile-overrides-bits_per_sample claim corrected before Mi-1 dispatch.
- **Within-PR halt-discipline (fix-agent caught their own work or related concerns without halting).** 3: M-5 too-defensive OOB; M-5 Mi-1-territory bug; Mi-1 dispatch-arithmetic correction.
- **Audit-raised halts (audit-agent flagged at PR review).** 2: M-4 audit-2 CI-state; INT-728 coverage-audit cross-doc drift.

Total halts ~1 per epic (matches the cycle's baseline rate).

### Self-report depth instances

User's calibration target: cure-attribution-style depth — name the discipline failure rather than papering over it.

- **M-3 paperwork close-out (`772dc77`).** Self-recorded the orchestrator's prior axis-blindness in `docs/deviations.md` `KNOWN-FAILING-CURE-ATTRIBUTION-V1` rather than treating the catch as a one-off correction. Meta-noted "fourth time `docs/known-failing-tests.md` has been wrong at dispatch/merge time."
- **INT-728 close-out (`dd2b879`).** Self-recorded the cross-doc drift in `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1` as a *second* governance-doc-drift catch in the cycle, explicitly framing the track-record signal: "two catches now form a track-record signal: governance-doc drift is a real, recurring failure mode, not theoretical."
- **M-4 paperwork close-out (`8f41d86`).** Filed `M-4-FU-TAILSCAN` and `M-4-FU-COVERAGE` as separate items rather than bundling into a "while-we're-here" follow-up commit. Recorded dual-ds64 deviation explicitly rather than letting the malformation case slide.
- **Mi-1 pre-dispatch correction (`1bd452e`).** Self-caught the libsndfile-overrides-bits_per_sample claim from M-5 paperwork close-out. Commit message names the verify-scope-claims rule applied symmetrically: "Same shape as the Mi-3 'one caller' catch applied symmetrically: the rule fires on the orchestrator's claims too, not just the user's."

User feedback at end of cycle: "Honest ambiguity flagging ('does this count as a halt?') is better than performative certainty — that's maturity, not decay. Continue."

---

## 5. Open architectural questions

### INT-728 was an instance of a class — other latent calls may exist

`INT-728-FIXTURE-MISMATCH` was a misattributed cure caught at dispatch. The cure-attribution sweep is now standard pre-dispatch (rule #10), so future dispatches will catch the same shape automatically. **However:** the only Active known-failing entries today are `NEW-BLIND-GAP` × 2 (cured by `NEW-BLIND-GAP` itself, a Tier 6 item) and the `test_reference_mode` SKIP-cluster (cured by `M-REF-ALIGN-UNIT` + `Mi-17`). The cure-attribution chains for these have NOT been audited along the cure-attribution axis — they were grandfathered into Active when the axis-coverage rule landed.

**When `NEW-BLIND-GAP`, `M-REF-ALIGN-UNIT`, or `Mi-17` dispatches:** apply the standard pre-dispatch checklist (rule #10). Cure-attribution sweep specifically: read the cited cure's mechanism and the test's failure mode and reason whether the mechanism actually addresses the failure. If the chain doesn't connect, halt before dispatch — same shape as the M-3 catch.

### `parser-errors.md` taxonomy completeness for Tier 5

`docs/decisions/parser-errors.md` defines the seven `AudioError` values (`FileNotFound`, `InvalidFormat`, `UnsupportedFormat`, `ReadError`, `WriteError`, `InvalidRange`, `ResampleError`). The local-view disambiguation rule was load-bearing for M-3 / M-4 / M-5 / Mi-1 — every new `return AudioError::*` site was checked against it.

**Tier 5 work is mostly NOT parser-rejection.** `M-9` (clamp guard), `M-10` (ZCR domain), `Mi-4` (correlation doc), `M-REF-ALIGN-UNIT` (alignment unit test), `NEW-BLIND-GAP` (blind-mode threshold) are algorithmic-correctness items operating on already-loaded `AudioBuffer` data. They don't return `AudioError`. The taxonomy is not directly applicable.

**Exception:** `C-3` (RF64 writer, Tier 2 leftover) and `C-5` (`compute_spectral_flatness` either delivers or is removed) may add new error paths. `C-3` would emit RF64 headers and could `return AudioError::WriteError` on size-overflow. `C-5` may add a precondition path. If either dispatches, sweep the new error sites against parser-errors.md.

**No new error-taxonomy doc is needed for Tier 5 by default.** If algorithmic items grow their own contract surface (e.g., a `ReferenceModeError` enum, a `BlindModeError` enum), file a parallel decision doc at that point.

### Tier 5 audit shape — different from parser hardening

**Parser hardening audits (Tier 4 shape) checked:**
- Does this byte get rejected with the right `AudioError` value?
- Does the local-view rule classify the structural inconsistency correctly?
- Does the FIXTURE-MALFORMED corpus regress?
- Are `[!shouldfail]` tags removed in the same PR as the cure?

**Algorithmic correctness audits (Tier 5 shape) will check:**
- Does this numerical output match the spec within stated tolerance?
- Is the tolerance constant named (`kRefFixtureToleranceSamples`-style) and matching across tests?
- Does the ground-truth signal exercise the failure mode the cure addresses?
- For threshold tuning (`NEW-BLIND-GAP`): does the cure produce ≥2 splits on the named fixture, AND not break single-track inputs (`M-8`'s domain)?
- For the unit-level invariants (`M-9`, `M-10`): does the cure handle the named edge case AND not change behavior on the common-case input?

**Pre-dispatch verification for Tier 5** should additionally check:
- Tolerance constants exist where the test claims them.
- Fixtures expected by the test (FIXTURE-REF for `M-REF-ALIGN-UNIT`) are actually wired into the test binary's CMake.
- Adjacency: does the cure incidentally affect any other algorithm-level test? (For `NEW-BLIND-GAP`: does threshold-tuning affect the existing single-track tests? For `C-4`: does rate-conversion rounding affect any other reference-mode boundary?)

The user signalled the **governing prompt will be refreshed** for this audit shape before Tier 5 dispatches begin. Do not pre-stage Tier 5 work until that refresh lands.

### Worktree hygiene as a recurring concern

11 stale worktrees on disk at handoff. The branch-delete-on-merge step in `gh pr merge` fails with `cannot delete branch X used by worktree at Y` for every dispatched fix-agent. This is a known cycle property, not a defect. When the next paperwork pass touches dispatch/merge tooling, a `git worktree remove --force` sweep at merge time is a candidate cleanup. Not blocking.

---

## 6. Live ambiguities

These are calls the outgoing orchestrator held two readings on without forcing certainty. Each is recorded so the next orchestrator inherits the ambiguity rather than a false resolution.

### Did `M-4` audit-2's CI-state finding count as a "halt"?

Audit-2 raised `feedback_halt_on_red_baseline.md` on GitHub Actions UI showing `FAILURE` on every lane. The orchestrator gate-evaluated and found the gate met (Build=success; tests red on documented entries; clang-tidy out-of-scope). Two readings:

- **As substantive halt.** Audit applied an existing playbook rule based on raw observable surface. Catching the misclassification is itself a discipline check, and it earned rule #11 (audit-halts → gate-eval). Logged as a halt under this reading.
- **As false positive.** Gate was met; audit lacked gate-doc context; resolution was mechanical, not corrective. Could be logged as audit noise, not a halt.

**Outgoing orchestrator's reading:** logged as substantive. The earned rule justifies the log.

### Did the Mi-1 fix-agent's "comment-only error in dispatch envelope" call meet the halt threshold?

The dispatch envelope claimed pre-Mi-1's `read_be_u16(+18)` returns 0 for 48 kHz; actual is `0xBB80`. Fix-agent corrected the test comment, judged the error comment-only (test discriminator robust regardless), did not halt, surfaced in final report. Audit endorsed.

- **Right-call reading.** The discriminator argument is robust. Halting would have been gate-design overhead for no signal gain; surfacing in the report preserves the audit trail.
- **Borderline reading.** Halt-on-uncertainty is the source of strength (rule #5). When in doubt, halt. Fix-agent could have halted, taken one orchestrator round-trip, and proceeded. Cost is one round-trip; benefit is reinforcing the proactive halt shape.

**Outgoing orchestrator's reading:** right call, but acknowledge the borderline. The audit's explicit endorsement is what tipped the balance.

### Three rules vs four rules promoted in the cycle?

User said "three earned playbook rules" at the handoff pause. Orchestrator counts four (#9, #10, #11, #12 in §3 above). The discrepancy depends on whether the cure-attribution-sweep promotion (#10) is treated as a sub-aspect of the axis-coverage rule (#9) or as its own rule. Both readings are defensible. Orchestrator-enumerated is for transparency.

### Whose claim was wrong about libsndfile overriding `bits_per_sample`?

The Mi-1 audit's report stated: "Caveat: production safe today because libsndfile re-validates at AudioFile::open time, which is why no round-trip has surfaced it." That claim itself was inaccurate (the override block at `src/core/audio_file.cpp:262-269` doesn't include `bits_per_sample`). The orchestrator's M-5 paperwork close-out paraphrased the audit and inherited the inaccuracy. The orchestrator caught it at Mi-1 pre-dispatch.

- **Cleaner if the original audit had been right.** Yes. Audit-agents working in isolation can be wrong about implementation details that aren't load-bearing for their evaluation; the audit's verdict was correct, the ancillary explanation was wrong.
- **Still-systemic concern?** Probably not — the verify-scope-claims rule (#8) caught it. But a fresh orchestrator should know that audit reports' explanatory claims are not gospel; the verdict is, the surrounding reasoning may not be.

---

## 7. Specific calibration warnings for the next orchestrator

Written for an orchestrator who has not lived through this cycle.

### What fatigue / decay look like in this codebase

- **Halt rate dropping to zero** is the primary decay signal. The cycle's baseline is ~1 halt per epic (mix of mid-flight, pre-dispatch self-catch, audit-raised). If two consecutive epics close without a single catch on either side, that is suspicious — the system is either internalizing discipline (good) or relaxing it (bad). The qualitative shape distinguishes them: are halts moving to *earlier* in the dispatch cycle (pre-dispatch self-catch, fix-agent within-PR catch) or disappearing entirely?
- **Self-reports getting briefer or less self-critical** is the secondary signal. The cycle's calibration target is "name the discipline failure rather than papering over it" — recorded explicitly in `docs/deviations.md` entries (`KNOWN-FAILING-CURE-ATTRIBUTION-V1`, `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1`). If close-out paperwork starts looking like "M-X RESOLVED in #Y, exit criteria flipped" with no narrative around catches or near-misses, that's the decay shape.
- **Performative certainty.** User explicit: "Honest ambiguity flagging is better than performative certainty — that's maturity, not decay." If the orchestrator starts producing crisp summaries that erase uncertainty, that's the shape.

### Halt patterns that matter

- **Cross-doc disagreement.** If `BACKLOG.md` says X about an item and another governance doc says Y, that is a halt signal (rule #12). Reconcile atomically.
- **Mandate scope claim that doesn't grep.** If the dispatch envelope says "single caller" or "only this file" and a 30-second grep shows otherwise, halt and correct before dispatching (rule #8).
- **Audit-raised CI / test-state finding.** Gate-eval first against `docs/known-failing-tests.md`'s rules (rule #11). The gate is the project-side refinement; surface CI is not.
- **Cure mechanism that doesn't connect to failure mode.** Read the cited cure and the test's failure mode and reason whether the chain holds (rule #9). Don't accept "same area of code" or "same file format" as evidence.

### Rules most likely to be forgotten on fresh context

In rough order of "most likely to slip without explicit attention":

1. **Cross-doc reconciliation at decision time** (#12). Easy to update one governance doc and forget another. The recurring instance is a Resolved-marker on `BACKLOG.md` without the matching entry-move on `docs/known-failing-tests.md`. Two catches before this rule landed; expect more catches if the rule slips.
2. **Audit-raised halts → gate-eval** (#11). Audit-agents can't be retrained mid-cycle; they will keep applying the raw-surface reading. The orchestrator must do the gate-eval each time.
3. **Cure-attribution sweep as standard** (rule #10's load-bearing component). If the orchestrator skips it on a "this is obvious" item, the next misattribution surfaces during dispatch — costing a round-trip — instead of pre-dispatch. The M-3 catch was preceded by three earlier audits that swept other axes and didn't catch it.
4. **Verify-scope-claims symmetrically** (#8 applied to the orchestrator's own claims). The Mi-1 self-catch on the libsndfile-bits_per_sample claim shows the rule fires on orchestrator paraphrases too, not just user-supplied claims.

### Specific to this codebase

- **Pre-staged orchestrator docs are the recurring failure surface.** Four `docs/known-failing-tests.md` catches over the cycle, two `BACKLOG.md`-vs-other catches. Treat any pre-staged doc the same way you treat a fixture: audit it with the same blast-radius weight as the dispatches it gates.
- **Line numbers drift; TEST_CASE names don't.** `KNOWN-FAILING-SCHEMA-V2` made TEST_CASE name canonical. Fixture-cited `audio_file.cpp:NNN` line numbers in BACKLOG entries should be re-verified at pre-dispatch — they shifted significantly across M-3 (EXTENSIBLE branch added) and M-4 (head+tail splice added) and Mi-1 (decode_float80 helpers added). Don't trust the BACKLOG line cite as authoritative — grep for the function/symbol name instead.
- **`AudioError` taxonomy is established.** Don't propose new values without going through the same backlog discipline (`Mi-3` added `ResampleError` with proper deviation tracking). Use the local-view rule from `docs/decisions/parser-errors.md`.
- **`Expected<T,E>` is variant-backed post-M-14.** No more `reinterpret_cast`-into-aligned-storage UB. Move-from semantics documented at `docs/decisions/expected-api.md`. Death-test scaffolding lives inline in `tests/test_audio_file.cpp` for now (`F-AUDIT2-DT` extracts it when a second consumer arrives).
- **The `[!shouldfail]` machinery is load-bearing.** Catch2's `[!shouldfail]` reports failures as expected-failures, which Catch2 does NOT count as visible-fails. Tests tagged this way are NOT in the Active known-failing set per the gate. When a cure lands, the tag is removed in the same PR, and the test passes via the strict assertion. If a `[!shouldfail]` test starts unexpectedly *passing*, that is a positive signal (the cure landed) — investigate before assuming regression.
- **`AudioFile::open` overrides only four fields from libsndfile**: `sample_rate`, `channels`, `frames`, `format`. `bits_per_sample` is parser-set, not libsndfile-validated. This was a load-bearing detail at Mi-1 and is documented in `BACKLOG.md`'s Mi-1 entry; future AIFF work may need to know it.

### What this cycle did NOT establish

- **No Tier 5 audit pattern.** The user signalled the governing prompt refresh would precede Tier 5 dispatch. Don't infer a Tier 5 pattern from Tier 4 unless you ground it in the algorithmic-correctness shape.
- **No fixture-shape decision for ground-truth signals.** `M-9`, `M-10`, `Mi-4`, `M-REF-ALIGN-UNIT`, `NEW-BLIND-GAP` will need fixtures or test-pattern decisions; the cycle did not pre-decide.
- **No close-out paperwork for `M-11`, `Mi-15`, `Mi-18-FU-BLIND-ITER`.** These are deferred-to-paperwork-pass items (M-11 and Mi-15 subsumed by M-14 but BACKLOG entries still show open). The next orchestrator can fold them into whatever paperwork pass next opens those lines.

---

## 8. Self-audit pass on this artifact

Per the audit-pass-discipline rule (#9), this artifact is itself a governance doc and gets axis coverage before commit.

### Axis 1 — State identity (canonical IDs throughout)

Every item is named by ID: `M-3`, `M-4`, `M-5`, `Mi-1`, `INT-728-FIXTURE-MISMATCH`, `M-4-FU-TAILSCAN`, `M-4-FU-COVERAGE`, `NEW-BLIND-GAP`, `M-REF-ALIGN-UNIT`, `Mi-17`, `KNOWN-FAILING-CURE-ATTRIBUTION-V1`, `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1`, etc. Test cases named by TEST_CASE name where cited. PRs named by `#NN` and merge SHA. Memory files by full filename. **Pass.**

### Axis 2 — Claim accuracy

Spot-checks performed against the working tree at HEAD `9fea7ae`:

- Main HEAD `9fea7ae` confirmed via `git log`.
- Tier 4 RESOLVED markers (`M-3 #34 70a7745`, `M-4 #35 039347e`, `M-5 #36 a1654a1`, `Mi-1 #37 b1e9edd`) confirmed via `BACKLOG.md` grep.
- INT-728 drop SHA `3a86871` and reconciliation SHA `dd2b879` confirmed via `git log`.
- Active known-failing entries (NEW-BLIND-GAP × 2, test_reference_mode SKIP cluster, clang-tidy) confirmed against `docs/known-failing-tests.md`.
- 12 feedback memories confirmed via `ls ~/.claude/projects/.../memory/`.
- `docs/decisions/expected-api.md` and `docs/decisions/parser-errors.md` confirmed present.
- C-3 still pending confirmed via `[!shouldfail]` tag still on `tests/test_lossless.cpp:801` (RF64 round-trip).
- M-2 still pending confirmed via `tests/fixtures/malformed/manifest.txt:21` (`data_size_overflows_file.wav   InvalidFormat-pending-M-2`).
- C-4, C-5 pending confirmed via no PR for those IDs in `git log --oneline | grep -i "C-[45]"` (returns empty).

**Pass.**

### Axis 3 — Cross-doc consistency

The artifact references several governance docs. For each, verify the artifact's claims agree with the cited doc:

- **`BACKLOG.md`** — RESOLVED markers, tier structure, and M-4-FU-* entries match. INT-728 RESOLVED-via-`3a86871` matches.
- **`docs/known-failing-tests.md`** — Active set (NEW-BLIND-GAP × 2 + test_reference_mode + clang-tidy) matches. INT-728 in Resolved section matches. The `<sha>` placeholder at line 96 (C-1 cluster) is flagged in §1 deferred housekeeping; consistent.
- **`docs/deviations.md`** — Six entries: FIXTURE-MALFORMED, M-4 dual-ds64, KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1, KNOWN-FAILING-CURE-ATTRIBUTION-V1, KNOWN-FAILING-COMPLETENESS-V1, KNOWN-FAILING-SCHEMA-V2, C-1 inline scope, Mi-3 deviation. Cited correctly throughout.
- **`docs/invariants.md`** — Parser invariants closed by Tier 4 (INV-RF64-2, INV-WAVEEXT-1/2, INV-AIFF-SSND-OFFSET, INV-AIFF-SAMPLERATE) match. Tier 5+ pending invariants (INV-RATECONV-ROUNDED for C-4, INV-ALIGN-EMPTY-VINYL for M-9, INV-ZCR-SHORT-FRAME for M-10, INV-BLIND-CLEAN-2TRACK for NEW-BLIND-GAP) match.
- **`docs/decisions/parser-errors.md`** and **`docs/decisions/expected-api.md`** — Cited at appropriate points; no detail claims that could conflict.
- **`docs/m14-scope.md`** — Marked archived in its own banner; consistent with M-14 RESOLVED marker.
- **In-cycle playbook (`~/.claude/projects/.../memory/`)** — 12 memory files enumerated; each cited rule references the exact memory filename.

**No drift detected.** Pass.

### Audit verdict

APPROVE. Commit.

---

## Cross-references

- `BACKLOG.md` — primary source of truth for items and their states.
- `docs/known-failing-tests.md` — gate definition + Active/Resolved set.
- `docs/deviations.md` — every decision that departed from the original review.
- `docs/invariants.md` — every named invariant + its enforcement site.
- `docs/decisions/parser-errors.md` — `AudioError` taxonomy with local-view rule.
- `docs/decisions/expected-api.md` — `Expected<T,E>` API contract post-M-14.
- `docs/m14-scope.md` — archived M-14 scope inventory.
- `docs/m-i-18-followups.md` — Mi-18 follow-up stub list.
- `docs/tier2-rebase-plan.md` — Tier 2 rebase walk plan (uses known-failing-tests.md as the post-rebase gate).
- `~/.claude/projects/-Users-mwoolly-projects-mwAudioAutoChop-C-/memory/` — in-cycle playbook (the 12 feedback memories indexed by `MEMORY.md`).
