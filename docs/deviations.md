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

