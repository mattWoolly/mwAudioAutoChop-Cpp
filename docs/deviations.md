# Review Deviations

Every decision that departs from the Knuth-level review's recommendation is
recorded here, with finding ID, what was done instead, reasoning, and the
reviewer who signed off.

Format:

```
## <finding-id> — short title

- **Commit.** <hash>
- **Reviewer.** <name / agent>
- **Review said.** <one line summary>
- **We did.** <one line summary>
- **Reasoning.** <why the deviation is justified>
- **Consequence.** <what a future reader should know>
```

## FIXTURE-MALFORMED — `fmt_size=0xFFFFFFFF` returns `UnsupportedFormat`, not `InvalidFormat`

- **Commit.** 4e8abfd (on `remediation/fixture-malformed`).
- **Reviewer.** orchestrator (self-recorded; to be cross-checked by invariant-agent).
- **Review said.** The FIXTURE-MALFORMED spec listed "fmt chunk claims `chunk_size = 0xFFFFFFFF`" as malformation #3 and implied the expected outcome was `InvalidFormat`.
- **We did.** The manifest for this blob records the observed outcome, `UnsupportedFormat`, and the `README.md` entry for the blob cites *why*: the chunk walker's integer arithmetic terminates cleanly on `chunk_size = 0xFFFFFFFF` (size_t promotion means no wrap on 64-bit), the inner fmt body is then parsed from the padded zeros, `audio_format` reads as `0`, and the parser returns `UnsupportedFormat` at `audio_file.cpp:285`.
- **Reasoning.** The invariant this fixture exercises is **INV-PARSER-REJECT**: "every malformed header returns either `InvalidFormat` or `UnsupportedFormat` in bounded time, with no sanitizer findings." Both return codes satisfy it. Forcing the parser to prefer `InvalidFormat` for this specific malformation would require extra special-case code whose only purpose is to make the test match a stronger specification than the invariant actually demands. That's a worse trade than accepting the observed behaviour as within-contract.
- **Consequence.** If a future change to `parse_wav_header` decides that `chunk_size > remaining-file-bytes` should be treated as `InvalidFormat` outright, this manifest entry flips to `InvalidFormat` and the test still passes. No call-site implications.
- **Re-evaluate.** When the parser-hardening epic owner picks up M-3 / M-4 / M-5, treat this entry as in-scope for that epic. Error-taxonomy consistency across the parser group matters more than any single call: if M-3/M-4/M-5 settle on a stronger "any chunk_size > remaining-bytes is `InvalidFormat`" convention, fold this case under it; if they don't, formalize the current "walker-survives → propagated through to whatever the body resolves to" rule and reference it from this entry.

## KNOWN-FAILING-COMPLETENESS-V1 — environment-dependent failure clusters

- **Commit.** Will be the orchestrator-paperwork commit landing alongside this entry, ahead of the PR #24 push.
- **Reviewer.** orchestrator (self-recorded after PR #24 rebase fix-agent halt).
- **Original doc said.** `docs/known-failing-tests.md` listed only `"AIFF header has correct structure"` as the C-1 stack-smash failure. The implicit assumption was that one TEST_CASE name uniquely identifies the failure pattern.
- **We did.** Added `"AIFF header has correct parameters"` as a sibling Active entry under the same C-1 cure, structured the cluster as a single section with two named cases, and recorded the environment-dependent visibility (Linux/macOS CI abort the runner at the first case's SIGABRT; macOS-local often runs through to the second case's REQUIRE failure).
- **Reasoning.** A stack-smash failure can corrupt enough state that subsequent test cases also fail at REQUIRE points downstream of the corruption. Whether the runner aborts immediately or limps through to the next failure depends on the OS's signal handling, libc layout, and Catch2 build configuration — not on the codebase itself. A doc that lists only the first-cited TEST_CASE will false-halt when an environment lets the runner continue. The fix is to enumerate the *failure cluster* and treat any subset of its TEST_CASEs surfacing in CI as the documented failure.
- **Schema decision.** Cluster entries: a single section enumerates all TEST_CASEs with a shared root cause, flags visibility as environment-dependent, and ties them to a single cure. Gate logic: "any TEST_CASE in the cluster surfacing in CI satisfies the gate; absence of *all* of them means the cure has landed (or something else regressed; check carefully)."
- **Consequence.** Future C-1-pre-merge runs won't false-halt on environment variance. After C-1 merges, both AIFF TEST_CASEs should pass; the cluster entry moves to Resolved as a single unit.
- **Meta-note.** This is the **third** time `docs/known-failing-tests.md` has been wrong at PR-merge time, surfaced by halt-and-surface protocol:
  1. **PR #23** — conflation between standalone `test_reference_mode` binary and `[integration][reference]` cases (filed as M-REF-ALIGN-UNIT + Mi-17 extension).
  2. **PR #23** — identification schema (file:line vs TEST_CASE name; filed as `KNOWN-FAILING-SCHEMA-V2` above).
  3. **PR #24** — completeness (failure clusters with environment-dependent visibility; this entry).
  Each surfaced under "do this while waiting for CI" pressure on a doc that was pre-staged without an audit pass. The user-affirmed lesson — pre-staged orchestrator gate docs need an explicit audit-agent pass before being declared authoritative — applies. The next pre-stage doc (`docs/m14-scope.md` when M-14 dispatches) gets an explicit audit pass before its dispatch reads it as the gate. Memory entry `feedback_pre_staged_docs_need_audit.md` is updated with this third instance and the new audit-completeness check ("are there failure clusters where one TEST_CASE name hides others?").

## KNOWN-FAILING-SCHEMA-V2 — identify by TEST_CASE name, not file:line

- **Commit.** Will be the gate-correction commit landing alongside this entry.
- **Reviewer.** orchestrator (self-recorded after PR #23 false halt).
- **Original rule said.** `docs/known-failing-tests.md` matched failures by "same test name + same line + same failure mode." Lines were treated as load-bearing identifiers.
- **We did.** Revised the matching rule to `TEST_CASE name (+ SECTION where applicable)` as the canonical identifier. Line numbers became drift-tolerant hints; the gate matches on TEST_CASE name. The doc's "Identification rule" preamble records the new schema; each entry was rewritten to lead with TEST_CASE name and to flag the current line as a navigation hint that drifts.
- **Reasoning.** The PR #23 (FIXTURE-REF) rebase produced a deterministic +37-line shift on three known-failing assertions inside `tests/test_integration.cpp` because PR #23 added new test bodies above them. Same TEST_CASE names, same assertion shapes, same failure modes — but every cited line was now wrong. The original "same line" rule classified this as a regression and would have halted every clean PR that adds code above a known failure. A gate that triggers on its own subject's normal behavior is misdesigned. TEST_CASE names are stable across additive PRs; line numbers are not.
- **Schema decision (Catch2 SECTION check).** All three failing TEST_CASEs in `test_integration.cpp` and the failing TEST_CASE in `test_lossless.cpp` are flat — no SECTION nesting. The schema is therefore `TEST_CASE` only today, with `TEST_CASE → SECTION` reserved for forward compatibility (added to the schema preamble as a fallback identifier).
- **Consequence.** Future PRs that add code above a known-failing TEST_CASE no longer trip a false halt. The gate doc's hint-line drifts naturally as files grow; orchestrator paperwork updates the hint when a PR's CI shows the new line, but only as documentation hygiene, not as a gate criterion.
- **Meta-note.** This is the second time `docs/known-failing-tests.md` has been wrong at PR-merge time (the first was the conflation between the standalone `test_reference_mode` binary and the `[integration][reference]` cases inside `test_integration.cpp`). Both errors were caught at the cost of a halt; the lesson — pre-staged orchestrator docs need an explicit audit-agent pass before being declared gate-authoritative — is recorded in the orchestrator memory (`feedback_pre_staged_docs_need_audit.md`). Apply this rule to the next pre-stage doc when its dispatch comes up; `docs/m14-scope.md` is the next likely candidate.

## C-1 inline scope expansion: `build_aiff_header` `numSampleFrames` field type

> **Promoted to backlog item `AIFF-INLINE-SCOPE` (Tier 4 — Parser hardening,
> AIFF write path).** This deviation entry remains for provenance, but the
> ongoing tracking lives in `BACKLOG.md` so it surfaces in Phase 4
> reconciliation rather than being buried here.

- **Commit.** `2668b68` on `remediation/c-1-encode-float80` (PR #27).
- **Reviewer.** orchestrator after two-pass C-1 audit (audit-1 + audit-2 both APPROVED-WITH-FOLLOWUP).
- **Review said.** C-1's stated scope was `encode_float80`'s buffer overrun. A strict reading would not include changes to `build_aiff_header`.
- **We did.** The C-1 fix-agent corrected `build_aiff_header`'s `numSampleFrames` field, which had been emitted as a 10-byte float80, to the spec-required 4-byte big-endian unsigned long.
- **Reasoning.** The original code declared `comm_size = 18` (which arithmetic balances only when `numSampleFrames` is u32: 2 channels + 4 numSampleFrames + 2 bits + 10 sampleRate = 18). Emitting `numSampleFrames` as float80 produced an internally-inconsistent COMM chunk that libsndfile and every standards-conformant AIFF reader rejected. The C-1 audit mandate required end-to-end round-trip verification through libsndfile across six sample rates; without a libsndfile-readable AIFF, that round-trip cannot be written honestly. Fixing the field type is therefore a hard precondition for C-1 verification, not an opportunistic refactor.
- **Consequence.** AIFF output is now spec-conformant. Audit-2 verified `write_track` (`src/core/audio_file.cpp:816`) is the sole production caller of `build_aiff_header` and that no path was previously emitting an AIFF that downstream tools accepted — every prior AIFF emission was already broken. No user-visible byte stream changes from "valid AIFF before" to "valid-but-different AIFF after"; the change is from "rejected by libsndfile" to "accepted by libsndfile."
- **Audit verdicts.** Both audit passes (audit-1 and audit-2) explicitly evaluated this scope expansion and accepted it. A stricter alternative — splitting into a separate item M-3.5 — was considered and rejected: the cost (waiting on a second item to merge before C-1 can be verified) outweighs the documentation benefit, given that the deviation is small, fully verified, and recorded here.

## Mi-3 — `resample_linear` fallibility, structural cure (deviation from "early return {}")

- **Commit.** `<sha>` (will be the merge commit for the `remediation/mi-3-resample-fallible` PR; orchestrator paperwork fills in the hash post-merge).
- **Reviewer.** orchestrator (self-recorded, with audit-agent confirmation).
- **Review said.** Mi-3 in `BACKLOG.md` (Tier 9 — Cleanup, line ~706) specifies a minimal cure: "early return `{}`" when `input.sample_rate == 0` inside `resample_linear`, treating the precondition violation as a silently-defaulted output.
- **We did.** Made `resample_linear` fallible — return type is now `Expected<AudioBuffer, AudioError>`. Two error producers, both yielding `AudioError::ResampleError`: (1) `input.sample_rate <= 0` (the Mi-3 div-by-zero precondition), and (2) `output_size` overflow during the `double → size_t` conversion at the buffer-sizing line, guarded with an `std::isfinite` / non-negative / `<= numeric_limits<size_t>::max()` check before the cast. Propagated through the one non-test caller (`load_audio_mono` in `src/core/audio_buffer.cpp`), whose return type was already `Expected<AudioBuffer, AudioError>` post-M-14. Updated the one test caller (`tests/test_audio_buffer.cpp:14`) and added a regression `TEST_CASE("resample_linear: sample_rate == 0 returns ResampleError", "[audio][error_path]")` adjacent to it.
- **Reasoning.** Three reasons, all enabled by M-14's contract unification (which post-dates the original Mi-3 spec):
  1. Vestigial `AudioError::ResampleError` is a contract lie post-M-14 — an enum value claiming a failure category that no code can emit. Path (a) early-return leaves the lie in place; this cure gives it substance.
  2. M-14's purpose was unifying error vocabulary. Leaving `resample_linear` infallible (swallowing a precondition violation into a default-constructed buffer) preserves the pre-M-14 pattern in miniature. Path (b) eliminates it.
  3. Blast radius is small — one production caller (`load_audio_mono`), already returning `Expected<AudioBuffer, AudioError>`, already under the unified taxonomy. Plus one test caller, mechanically updated.
- **Acknowledgement.** The original review specified "early return `{}`" deliberately. The deviation is enabled by M-14's unification work, which didn't exist when the review was written. Different project state, different right answer — not a correction of the original review, an updated one.
- **Consequence.** Future readers of `resample_linear` see an `Expected` return type and a documented failure-mode list. `AudioError::ResampleError` has a real producer and a real test. The structural pattern matches every other fallible operation in `audio_file.hpp`.
- **Re-evaluate.** N/A — Mi-3 is closed by this entry.

