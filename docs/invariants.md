# Invariants

Living document listing every invariant named in `BACKLOG.md` or established
by a fixture / PR. Updated by the invariant-agent every 3–5 completed items
(per backlog item DOC-3).

For each entry:

- **ID** and one-line statement.
- **Owner.** Function(s) or fixture that establish the invariant.
- **Enforcement.** Test case name, assert location, or type-level constraint
  that catches a regression. `pending` if no enforcement exists yet (and the
  backlog item that will add it is named).
- **Dependent backlog items.**

Tags used below: `pending` = no enforcement yet; `[!shouldfail]` = test
exists, currently expected to fail until the named backlog item lands.

---

## Fixture invariants

### INV-REF-1 — Reference-mode position fidelity

For every reference track in the `ref_v1` fixture,
`mwaac::analyze_reference_mode` reports a `split_points[i].start_sample`
within ±N native-rate samples of the manifest's
`track<i>_start_sample`. N is declared at the top of
`tests/test_integration.cpp` as `kRefFixtureToleranceSamples = 2205`
(50 ms at 44 100 Hz, the worst-case envelope-frame quantum).

- **Owner.** Fixture `tests/fixtures/ref_v1/` (FIXTURE-REF) +
  `analyze_reference_mode` and `align_per_track` in
  `src/modes/reference_mode.cpp`.
- **Enforcement.**
  - **Integration surface (full pipeline).** `Reference mode pipeline:
    track positions within tolerance` in `tests/test_integration.cpp`
    (and the structural pre-check in `Reference mode pipeline: basic
    detection`).
  - **Unit surface (algorithm only).** `Reference mode: per-track
    alignment to synthetic vinyl` in `tests/test_reference_mode.cpp:28-108`
    (added by M-REF-ALIGN-UNIT, PR #46) — calls `align_per_track`
    directly (skipping the resampling and gap-detection stages of
    `analyze_reference_mode`) so an alignment-precision regression
    that still passes the integration tests' gap-detection path shows
    up here as a localized failure on the alignment-algorithm surface.
    `kRefFixtureToleranceSamples` is `static_assert`'d to match the
    integration-test value verbatim so the two enforcement sites
    cannot drift apart silently.
- **Dependents.** FIXTURE-REF (closes), C-4 (tightens tolerance to one
  native sample once rate-conversion truncation is fixed —
  `kRefFixtureToleranceSamples` is the upper bound, both tests continue
  to assert against it after C-4), M-REF-ALIGN-UNIT (adds the unit-level
  enforcement above; isolates the algorithm surface from the pipeline).

### INV-REF-2 — Reference-mode lossless byte identity

Given a reference-mode detection on the `ref_v1` fixture, exporting a track
via `write_track` produces a file whose sample-data region equals the source
vinyl's sample bytes over the same `[start_sample, end_sample]` range.

- **Owner.** `write_track` in `src/core/audio_file.cpp` + the `ref_v1`
  fixture.
- **Enforcement.** `Reference mode pipeline: lossless export verification`
  in `tests/test_integration.cpp`.
- **Dependents.** FIXTURE-REF (closes).

### INV-RF64-1 — RF64 with `ds64` before data parses correctly

`parse_wav_header` recovers the correct `data_offset` and `data_size` for
an RF64 file whose `ds64` chunk appears immediately after `WAVE`.

- **Owner.** `parse_wav_header` in `src/core/audio_file.cpp` + fixture
  `tests/fixtures/rf64/rf64_ds64_first.wav`.
- **Enforcement.** `parse_wav_header: RF64 with ds64 before data` in
  `tests/test_lossless.cpp`.
- **Dependents.** FIXTURE-RF64, C-3.

### INV-RF64-2 — RF64 with `ds64` after data still parses

`parse_wav_header` recovers `data_offset` / `data_size` correctly when
the `ds64` chunk follows the `data` chunk. The tail-scan that finds
the post-data `ds64` is bounded to the spliced tail window
(`[kHeadSize, end)`) so head-window sample bytes cannot false-match
the `ds64` fourcc plus a syntactically valid 24-byte trailer
(M-4-FU-TAILSCAN tightening). The end-to-end production-pipeline
behaviour — that `AudioFile::open` exposes the parser-recovered
`AudioInfo` even when libsndfile rejects the file at `sf_open` —
is asserted separately by `INV-RF64-LIBSNDFILE-FALLBACK` below; both
invariants share the production-pipeline TEST_CASE for cure
attribution.

- **Owner.** `parse_wav_header` (helper-direct path) +
  `tests/fixtures/rf64/rf64_ds64_after.wav`. File-scope constants
  `kHeadSize` / `kTailWindow` at `src/core/audio_file.cpp:71-72`
  bind the tail-scan window.
- **Enforcement.**
  - Helper-direct (M-4 cure-attribution): `parse_wav_header: RF64 with
    ds64 after data` in `tests/test_lossless.cpp`.
  - In-head false-match regression (M-4-FU-TAILSCAN cure-attribution):
    `parse_wav_header: in-head ds64-shaped sample bytes are not
    false-matched by tail-scan` in `tests/test_lossless.cpp`. Inline
    128-byte buffer with planted false `ds64` fourcc + 24-byte
    trailer; pre-fix returns wrong `data_size`, post-fix returns
    `InvalidFormat`.
  - Production-pipeline (shared with INV-RF64-LIBSNDFILE-FALLBACK):
    `AudioFile::open: RF64 with ds64 after data exposes correct
    data_size` in `tests/test_lossless.cpp` is also a regression
    guard against helper-vs-splice divergence (the original
    M-4-FU-COVERAGE concern).
- **Dependents.** FIXTURE-RF64, M-4 (RESOLVED #35), M-4-FU-TAILSCAN
  (RESOLVED #38).

### INV-RF64-LIBSNDFILE-FALLBACK — `AudioFile::open` returns parser truth when libsndfile rejects RF64

When `parse_wav_header` recovers `data_offset` / `data_size` for an
RF64 file but libsndfile rejects the file at `sf_open` (libsndfile
1.2.2 returns "Unspecified internal error" on RF64 ds64-after-data
files, verified by direct probe), `AudioFile::open` returns the
parser's recovered `AudioInfo` rather than discarding it as
`AudioError::ReadError`. `info.frames` is derived from
`info.data_size / info.bytes_per_frame()`; `info.subtype` defaults
to `"PCM"`. Non-RF64 libsndfile failure remains `ReadError`
(unchanged); RF64 libsndfile success still uses libsndfile's
overrides. Production-pipeline-scoped: complements INV-RF64-2's
parser-scoped contract.

- **Owner.** `AudioFile::open` cross-validation block at
  `src/core/audio_file.cpp:344-388`.
- **Enforcement.**
  - Production-pipeline (also serves INV-RF64-2): `AudioFile::open:
    RF64 with ds64 after data exposes correct data_size` in
    `tests/test_lossless.cpp`. Opens
    `rf64_ds64_after.wav` via `AudioFile::open` (not via the helper),
    reads `audio_file.value().info()`, asserts `data_offset` /
    `data_size` against the manifest. The TEST_CASE was filed under
    M-4-FU-COVERAGE (axis: helper-vs-splice co-evolution) and
    redirected to M-4-FU-LIBSNDFILE-GATE attribution with
    `[!shouldfail]` per C-3 precedent in PR #39, then un-tagged
    atomic with the production fix in PR #40.
- **Dependents.** FIXTURE-RF64, M-4-FU-COVERAGE (RESOLVED via
  redirect in #39), M-4-FU-LIBSNDFILE-GATE (RESOLVED #40). Built on
  top of INV-RF64-2 (parser must succeed before fallback applies).

### INV-RF64-3 — RF64 round-trip is byte-identical and format-preserving

`write_track` on an RF64 source produces an RF64 output whose named
payload region is byte-identical (SHA-256 equal) to the source's same
region, AND whose container format remains `RF64`. *Currently violated.*

- **Owner.** `write_track` / `build_*_header` in `src/core/audio_file.cpp`
  + `tests/fixtures/rf64/rf64_ds64_first.wav`.
- **Enforcement.** `RF64 round-trip: sample region byte-identical`
  `[!shouldfail]` in `tests/test_lossless.cpp`. Tag drops when C-3 lands.
- **Dependents.** FIXTURE-RF64, C-3.

### INV-WAVEEXT-1 — WAVE_FORMAT_EXTENSIBLE with PCM/float SubFormat is accepted

`parse_wav_header` (and therefore `AudioFile::open`) accepts a
`WAVE_FORMAT_EXTENSIBLE` (0xFFFE) file whose SubFormat GUID identifies PCM
or IEEE-float, regardless of channel count, returning an `AudioInfo` whose
`channels`, `sample_rate`, `bits_per_sample`, `data_offset`, and
`data_size` match the on-disk header.

- **Owner.** `parse_wav_header` + the `pcm_24bit_stereo.wav` /
  `pcm_24bit_5ch.wav` fixtures in `tests/fixtures/waveext/`.
- **Enforcement.** Two tests in `tests/test_audio_file.cpp`:
  `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM 24-bit stereo accepted`
  and `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM 24-bit 5-channel
  accepted`. `[!shouldfail]` tag dropped at M-3 merge (PR #34).
- **Dependents.** FIXTURE-WAVEEXT, M-3 (RESOLVED #34).

### INV-WAVEEXT-2 — Unknown SubFormat GUID returns UnsupportedFormat

`parse_wav_header` returns `UnsupportedFormat` (not `InvalidFormat`, not a
crash) when the SubFormat GUID is neither PCM nor IEEE-float. Post-M-3
the parser inspects the GUID deliberately rather than rejecting all
0xFFFE incidentally.

- **Owner.** `parse_wav_header` + `pcm_extensible_unsupported_subformat.wav`.
- **Enforcement.** `parse_wav_header: extensible with unknown SubFormat
  returns UnsupportedFormat` in `tests/test_audio_file.cpp`. The test
  also asserts the PCM-subformat sibling parses successfully, confirming
  that GUID inspection (not blanket 0xFFFE rejection) drives the result.
  `[!shouldfail]` tag dropped at M-3 merge (PR #34).
- **Dependents.** FIXTURE-WAVEEXT, M-3 (RESOLVED #34).

### INV-WAVEEXT-3 — Round-trip of an EXTENSIBLE source preserves data bytes

`write_track` of a `WAVE_FORMAT_EXTENSIBLE` source produces an output WAV
whose data section is byte-identical to the requested region of the
source's data section.

- **Owner.** `write_track` + `build_*_header` + the `pcm_24bit_stereo.wav`
  fixture.
- **Enforcement.** `Lossless: 24-bit 2-ch extensible WAV round-trip
  preserves bytes` in `tests/test_lossless.cpp`. `[!shouldfail]` tag
  dropped at M-3 merge (PR #34); the read-side cure was sufficient on
  the round-trip-byte-identity axis. NEW-WAVEEXT-WRITE remains open as
  a separate concern (the EXTENSIBLE-emit format-identity surface), not
  a dependency of this invariant's byte-identity assertion.
- **Dependents.** FIXTURE-WAVEEXT, M-3 (RESOLVED #34). NEW-WAVEEXT-WRITE
  is orthogonal — it gates a format-identity invariant, not byte
  identity.

## Parser invariants

### INV-PARSER-BOUNDED — Header parsers are bounded in time and space

`parse_wav_header` and `parse_aiff_header` return in bounded time on every
input — no crash, no infinite loop, no gigabyte-scale allocation — for
every member of the FIXTURE-MALFORMED corpus.

- **Owner.** `parse_wav_header` and `parse_aiff_header` in
  `src/core/audio_file.cpp`.
- **Enforcement.** `FIXTURE-MALFORMED: header parsers reject every
  malformation` in `tests/test_audio_file.cpp` walks the manifest and
  asserts total parse time < 1 s wall clock. The test binary runs under
  ASan + UBSan in CI, so a crash, infinite loop, or absurd allocation
  fails loudly with the offending blob name.
- **Dependents.** FIXTURE-MALFORMED, M-2, M-3 (RESOLVED #34), M-4
  (RESOLVED #35), M-5 (RESOLVED #36), C-5.

### INV-PARSER-REJECT — Each malformation elicits a defined rejection

Each member of FIXTURE-MALFORMED elicits either
`AudioError::InvalidFormat` or `AudioError::UnsupportedFormat`. Cases the
parser is expected to handle correctly only after a backlog item lands
(M-2 / M-5) are tagged `[!shouldfail]` in
`tests/test_audio_file.cpp` until then.

- **Owner.** `parse_wav_header` / `parse_aiff_header` + the malformed
  corpus.
- **Enforcement.** Two `TEST_CASE`s in `tests/test_audio_file.cpp`
  (`FIXTURE-MALFORMED: header parsers reject every malformation` for the
  must-pass set, and `FIXTURE-MALFORMED: parsers reject pending-fix
  malformations` `[!shouldfail]` for the remaining gated set). Post-M-5
  the only `-pending-` entry left in
  `tests/fixtures/malformed/manifest.txt` is
  `data_size_overflows_file.wav` (`InvalidFormat-pending-M-2`); the
  M-5-gated entry was un-suffixed at the M-5 merge. Behaviour for
  `fmt_size_max.wav` deviates from the spec's implied `InvalidFormat`;
  see `docs/deviations.md`.
- **Dependents.** FIXTURE-MALFORMED, M-2 (last remaining
  `[!shouldfail]` gate), M-3 (RESOLVED #34), M-5 (RESOLVED #36).

## Implementation invariants (named in BACKLOG.md)

These are invariants the backlog promises will be enforced when the
named item lands. Entries flip from `pending` to a full Owner /
Enforcement form as items resolve; entries already in the full form
have their RESOLVED-status backlog item cited inline.

### INV-FLOAT80-10BYTES — `encode_float80` writes exactly 10 bytes

`encode_float80` writes exactly 10 bytes to its output buffer, matching
IEEE 754 80-bit extended-precision wire format used by the AIFF COMM
sampleRate field.

- **Owner.** `encode_float80` in `src/core/audio_file.cpp`.
- **Enforcement.**
  - `encode_float80: wire-format value 1.0` and
    `encode_float80: wire-format value 44100.0` in
    `tests/test_lossless.cpp` — direct wire-format byte assertions.
  - `AIFF sample-rate round-trip via libsndfile` in
    `tests/test_lossless.cpp` — six-rate end-to-end round-trip
    (44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz).
  - ASan + UBSan jobs on `test_lossless` (no stack-buffer-overflow on
    any AIFF emission path).
- **Dependents.** C-1 (RESOLVED #27), AIFF-INLINE-SCOPE (closed).

### INV-EXPECTED-PRECONDITION — `Expected<T,E>::value()` is a precondition

Calling `value()` on an errored `Expected` (or `error()` on a
value-bearing `Expected`) is loud (assert / terminate), not silent UB.

- **Owner.** `Expected<T,E>` in `src/core/audio_file.hpp` (post-M-14
  the storage is `std::variant<T,E>`-backed; the precondition contract
  is unchanged).
- **Enforcement.**
  - `Expected: value() on errored Expected aborts the process`
    (death-test, fork-based) in `tests/test_audio_file.cpp`.
  - `Expected: error() on value-bearing Expected aborts the process`
    (death-test) in `tests/test_audio_file.cpp`.
  - `Expected: documented contract — has_value() gates value()` in
    `tests/test_audio_file.cpp` — the non-death-test half of the
    contract.
- **Dependents.** C-2 (RESOLVED #29), M-14 (RESOLVED #31). M-15 is
  subsumed by C-2 (no separate dispatch); see INV-CLI-OPEN-GUARD for
  the call-site half.

### INV-RESULT-WRAPPER — One result wrapper, defined behaviour

The codebase has exactly one error-result wrapper (`Expected<T,E>`),
backed by `std::variant<T,E>` with no `reinterpret_cast`-based
storage. Its value/error accessors have defined behaviour on every
input via the precondition contract above. `LoadResult` is removed
from the tree; `AudioError` includes `ResampleError` so the unified
taxonomy is a strict superset of the previous load-specific values.

- **Owner.** `Expected<T,E>` in `src/core/audio_file.hpp`; `AudioError`
  in the same header.
- **Enforcement.** Sanitizer-clean build + the death-test pair under
  INV-EXPECTED-PRECONDITION; M-14 audit verified all 5 exit criteria
  + sanitizer-clean Expected access paths + no `LoadResult` symbol
  remains in the tree.
- **Dependents.** M-14 (RESOLVED #31), C-2 (RESOLVED #29), M-11
  (closed as duplicate of M-14), Mi-3 (RESOLVED #33; structural
  cure made `resample_linear` fallible under the unified taxonomy —
  see `docs/deviations.md` Mi-3 entry).

### INV-RATECONV-ROUNDED — Reference-mode rate conversion rounds, not truncates

Reference-mode boundaries are within one native-rate sample of the
analysis-rate result.

- **Owner.** `analysis_to_native_sample` in `src/modes/reference_mode.{hpp,cpp}`.
- **Enforcement.** `Reference mode: native-rate boundary is rounded not truncated`
  in `tests/test_reference_mode.cpp` (4 SECTIONs covering 44.1↔192 kHz
  round-vs-truncate divergence, exact-integer ratios, exact half-boundary
  rounding, and negative-input round-half-away-from-zero).
- **Cure constant.** `kAnalysisToNativeRoundingTolerance = 1` (native
  sample) at `src/modes/reference_mode.cpp:28`.
- **Status.** `holds` post-C-4 merge `e519bf6`. Pre-cure `analysis_to_native_sample`
  truncated toward zero (integer division) and could miss the nearest
  native-rate sample by up to (analysis_sr − 1) / analysis_sr native
  samples (~9 samples at 192 kHz native / 22050 Hz analysis). DRIFT-MODEL-RATE-TRUNCATION
  (PR #42, merge `718330c`) wired the rounding helper into
  `DriftModel::ref_to_vinyl_sample` at `src/core/drift_model.cpp:10,16,33`,
  closing the same defect at the second site. INV-REF-1's 50 ms
  envelope-frame floor remains the binding constraint at the integration
  surface; INV-RATECONV-ROUNDED tightens the unit-level accuracy that
  feeds into it.
- **Precondition contract (post-M-REF-RATE-VALIDATION).**
  `analysis_to_native_sample`'s precondition checks (`native_sr > 0`
  and `analysis_sr > 0`) are enforced symmetrically in Debug and
  Release builds via `MWAAC_ASSERT_PRECONDITION`
  (`src/core/audio_file.hpp:46-53`). Pre-cure the raw `assert(...)`
  calls compiled out under NDEBUG, leaving a future caller passing
  zero or negative rates to trigger integer division-by-zero (UB) in
  Release. Production callers in `analyze_reference_mode` derive
  these from libsndfile-validated `AudioBuffer::sample_rate` and the
  analysis_sr default (22050), so current paths are safe by
  construction; the cure pins the contract for future callers. Three
  death-test TEST_CASEs at `tests/test_reference_mode.cpp` (file end)
  exercise the `native_sr == 0`, `analysis_sr == 0`, and `native_sr < 0`
  paths under both Debug and Release CI lanes. Audit empirically
  verified the Release path fires. Holds post-M-REF-RATE-VALIDATION
  merge `3e20e26` (PR #49).

### INV-SPECTRAL-FLATNESS-DEFINED — `compute_spectral_flatness` either delivers or is removed

The function returns real flatness values (or correctly handles short
input by returning empty), or it is removed from the public header.

- **Status.** `pending` (C-5).

### INV-PARSER-CROSSCHECK — Parser and libsndfile agree on size

After `AudioFile::open`, `info.frames * info.bytes_per_frame() ==
info.data_size`; a violation returns `InvalidFormat`.

- **Status.** `pending` (M-2). Will gate
  FIXTURE-MALFORMED's `data_size_overflows_file.wav` once it lands —
  manifest entry flips from `InvalidFormat-pending-M-2` to `InvalidFormat`.

### INV-AIFF-SSND-OFFSET — `parse_aiff_header` honours SSND offset

`parse_aiff_header` reads the SSND `offset` field and applies it:
`data_offset = SSND_body + 8 + offset`,
`data_size = chunk_size - 8 - offset`. An out-of-bounds offset
(`offset > chunk_size - 8`) returns `InvalidFormat` per the
parser-errors local-view rule.

- **Owner.** `parse_aiff_header` SSND-handling block in
  `src/core/audio_file.cpp`.
- **Enforcement.**
  - `parse_aiff_header: non-zero SSND offset is honored` in
    `tests/test_audio_file.cpp` — inline AIFF with offset=4,
    chunk_size=28; asserts `data_offset=58, data_size=16`.
  - `parse_aiff_header: zero SSND offset is byte-identical to
    pre-M-5` in `tests/test_audio_file.cpp` — regression guard for
    the C-1 round-trip surface.
  - `FIXTURE-MALFORMED: header parsers reject every malformation`
    picks up `aiff_ssnd_offset_nonzero.aiff` (offset=16 > chunk_size-8=4
    → `InvalidFormat`); the `-pending-M-5` suffix dropped from the
    manifest atomically with the M-5 fix.
- **Dependents.** M-5 (RESOLVED #36), FIXTURE-MALFORMED.

### INV-AIFF-SAMPLERATE — `parse_aiff_header` decodes the 80-bit sample rate

`parse_aiff_header` produces an `AudioInfo` whose `sample_rate` matches
the file header (no `0`); also reads `bits_per_sample` from the correct
COMM-body offset (`chunk_offset + 14`, not the pre-Mi-1 `+18` which
read into the float80 sampleRate slot). NaN/Inf/negative/subnormal/
non-integer/over-INT32_MAX float80 sample rates return `InvalidFormat`.
Inverse of the (post-C-1) `encode_float80`.

- **Owner.** `parse_aiff_header` COMM-handling block in
  `src/core/audio_file.cpp` (uses `decode_float80_to_u64` /
  `decode_float80_to_u32` static helpers, inverse of `encode_float80`).
- **Enforcement.**
  - `parse_aiff_header: sample_rate decoded from 80-bit float` in
    `tests/test_audio_file.cpp` — six PROJECT_SPEC sample rates,
    genuine encoder/decoder round-trip via `build_aiff_header`.
  - `parse_aiff_header: bits_per_sample decoded from correct COMM
    offset` in `tests/test_audio_file.cpp` — four bit depths.
- **Dependents.** Mi-1 (RESOLVED #37), C-1 (RESOLVED #27;
  `encode_float80` is the inverse).

### INV-CLI-OPEN-GUARD — CLI short-circuits on failed `AudioFile::open`

Every `main.cpp` branch that uses `audio_file.value()` is preceded by a
validated guard. The C-2 fix-agent rewrote all three CLI branches
(reference, blind, tui) to the canonical
`if (!opened) return 1; auto& audio_file = opened.value();` pattern,
and the post-M-14 precondition guards on `Expected<T,E>::value()` /
`error()` (INV-EXPECTED-PRECONDITION) abort hard if any future
caller regresses, providing belt-and-braces protection beyond the
textual guards.

- **Owner.** `src/main.cpp` (three CLI branches: reference, blind,
  tui); precondition backstop in `src/core/audio_file.hpp`.
- **Enforcement.**
  - `main: failed AudioFile::open exits cleanly (no crash)` subprocess
    test in `tests/test_audio_file.cpp` — invokes the built
    `mwAudioAutoChop` binary with a non-existent path; asserts
    non-zero exit, no crash, sanitizer-clean.
  - Backstopped by INV-EXPECTED-PRECONDITION's death-test pair (any
    future unguarded `.value()` call aborts the process loudly).
- **Dependents.** C-2 (RESOLVED #29), M-15 (subsumed by C-2; no
  separate dispatch). F-AUDIT2-1 remains open as a coverage extension
  (an integration variant that exercises the guard end-to-end via a
  WAVE_FORMAT_EXTENSIBLE fixture rather than a non-existent path).

### INV-WRITE-ATOMIC — `write_track` is all-or-nothing at the target path

`write_track` produces either the complete output file or no file at the
target path. Implementation uses temp-sibling +
`std::filesystem::rename` (POSIX-atomic on a single filesystem); the
temp file is cleaned on every error path; temp-sibling path generation
handles filenames up to `NAME_MAX` (255 on POSIX) without losing the
random uniqueness component (truncation returns `WriteError`, never a
non-unique temp path).

- **Owner.** `write_track` and `make_temp_sibling_path` helper in
  `src/core/audio_file.cpp`.
- **Enforcement.**
  - `write_track: success leaves only target file at output path` in
    `tests/test_lossless.cpp`.
  - `write_track: failure leaves no file at output path (parent dir
    missing)` in `tests/test_lossless.cpp`.
  - `write_track: failure leaves no file at output path (target is a
    directory)` in `tests/test_lossless.cpp`.
  - `write_track: long-filename concurrent writes do not collide` in
    `tests/test_lossless.cpp` — ≥50-char filename × ≥8-thread stress;
    asserts no partial file at the target.
  - `write_track: target filename longer than NAME_MAX returns
    WriteError` in `tests/test_lossless.cpp`.
- **Dependents.** M-16. *Note:* M-16's BACKLOG.md entry header still
  reads as Active (no RESOLVED tag, exit-criteria checkboxes unmarked)
  but the production code and the five enforcement TEST_CASEs above are
  in tree — the BACKLOG drift is surfaced separately (out of
  invariant-doc scope).

### INV-ALIGN-EMPTY-VINYL — `align_per_track` skips against empty vinyl

Empty vinyl returns empty offsets, no UB from `std::clamp(..., hi<lo)`.

- **Owner.** `align_per_track` in `src/modes/reference_mode.cpp:832`.
- **Enforcement.**
  - Function-entry guard at `src/modes/reference_mode.cpp:843-857`
    (early-returns the empty offsets vector before entering the per-track
    loop, pinning "empty vinyl ⇒ empty offsets, no per-track loop body
    executed at all").
  - `align_per_track: empty vinyl returns empty offsets, no UB` in
    `tests/test_reference_mode.cpp` (constructs an empty vinyl and a
    non-empty `tracks` vector to force the loop to be tempted; UBSan-
    clean execution is the second cure-signal).
- **Status.** `holds` post-M-9 merge `7969aec` (PR #43). Pre-cure the
  per-track loop ended each iteration with
  `std::clamp(chosen_pos, 0, vinyl.samples.size() - 1)`, which evaluates
  to `std::clamp(x, 0, -1)` on empty vinyl — `hi < lo` is undefined
  behavior per cppreference. Function-entry guard chosen over per-iter
  in-loop guard because the early return shape cleanly distinguishes
  "no vinyl at all" from "this track skipped" (the cure rationale is
  documented in the cure comment block at `:843-857`).
- **Adjacent risk RESOLVED:** M-WAVEFORM-CLAMP-UB (Tier 7) — `render_waveform`
  had the same empty-container `std::clamp` UB pattern at
  `src/tui/waveform.cpp:68-69`, reachable when called with `height == 1`.
  Cross-tier finding from M-9 pre-dispatch sweep, filed in commit
  `8f70230`. **RESOLVED via PR #56 merge `3650fe2`** (2026-05-19) — see
  sibling [[INV-WAVEFORM-DEGENERATE-HEIGHT]] below for the cured
  invariant.

### INV-WAVEFORM-DEGENERATE-HEIGHT — `render_waveform` rejects degenerate-height input at the boundary

`render_waveform` requires `height >= 2` (one row for the waveform +
one row for track-number labels). The pre-cure guard at `:53` admitted
`height == 1`, which then computed `waveform_height = height - 1 == 0`
and invoked `std::clamp(min_row, 0, waveform_height - 1)` with
`hi = -1 < lo = 0` — undefined behavior per cppreference. The cure
tightened the input-boundary guard to fail fast on any height that
cannot produce at least one waveform row.

- **Owner.** `render_waveform` in `src/tui/waveform.cpp`.
- **Enforcement.**
  - Input-boundary guard at `src/tui/waveform.cpp:53` widened from
    `height <= 0` to `height < 2`. Returns empty `std::vector<std::string>`
    for any input that has no usable waveform rows to draw.
  - `render_waveform: height==1 does not invoke std::clamp with hi < lo`
    in `tests/test_waveform.cpp` exercises the cured path with non-empty
    peaks; primary signal is sanitizer-clean run (the CI `sanitizers
    (asan+ubsan)` job — pre-cure the test would trip UBsan on
    iteration 1 of the per-column loop). Functional assertion is on
    the returned vector being empty.
  - `render_waveform: height==0 returns empty` defensive test that
    the widened guard preserves the original `height <= 0` arm.
  - `render_waveform: height==2 returns one waveform row + one
    track-number row` no-regression test for the smallest valid
    height — catches a hypothetical regression that widened the
    guard further (e.g., to `height < 3`) and broke previously-valid
    inputs.
- **Status.** `holds` post-M-WAVEFORM-CLAMP-UB merge `3650fe2` (PR #56).
  Tier 7 first item; same defensive-cure pattern family as M-9 (PR
  closed earlier this cycle, `5c577d7`) and M-10 (PR `5c577d7`).
  Three instances of the family now closed; pre-dispatch sweep on
  PR #56 verified all five remaining `std::clamp` sites in `src/`
  are invariant-protected (see PR #56 description), so the family
  has reached fixed point on the `std::clamp(x, lo, hi)` UB axis.
- **TUI test infrastructure note.** `tests/test_waveform.cpp` is the
  first test target to link `mwaac_tui`; it establishes a pure-
  function test wedge for `src/tui/`. Mi-8 / Mi-9 (TUI marker nudge
  + view bounds) BACKLOG entries call out the need for a "headless
  state-mutator harness" for `app.cpp` event handlers — that is a
  distinct harness from this wedge and is not addressed by the
  M-WAVEFORM-CLAMP-UB cure. Filing as a forward note so the next
  Tier 7 dispatch starts with this scope distinction in mind.

### INV-ZCR-SHORT-FRAME — `compute_zero_crossing_rate` is 0 below 2 samples

ZCR is defined as `0` for frames of length `< 2`.

- **Owner.** `compute_zero_crossing_rate` in `src/core/analysis.cpp`.
- **Enforcement.**
  - Per-frame in-loop guard at `src/core/analysis.cpp:73-76`
    (`if (end - start < 2) { zcr[i] = 0.0f; continue; }`) — sits
    adjacent to the offending divisor; cure rationale documented in the
    inline comment block at `:60-72`.
  - `compute_zero_crossing_rate: single-sample frame returns 0, not NaN`
    in `tests/test_analysis.cpp:66-77` (three assertions: size,
    exact-match `== 0.0f`, NaN exclusion). Pre-cure both content
    REQUIREs would fail (`NaN != 0.0f`, isnan true).
- **Status.** `holds` post-M-10 merge `6a8c805` (PR #44). Pre-cure the
  per-frame loop body computed `(2 * zero_crossings) / (end - start - 1)`,
  div-by-zero on `end - start == 1`. Cure choice (a) per-frame in-loop
  guard chosen over (b) function-entry early-return per defense-in-depth:
  (a) pins the invariant verbatim at per-frame granularity; (b) would
  conflate frame-length with input-length and leave a latent gap if a
  non-empty signal produced a degenerate trailing frame.

### INV-CC-NORMALIZATION — Naive cross-correlate is a verification shim

The naive `cross_correlate` is a verification shim for the FFT
implementation; callers using its peak as a probability are using it
wrong. Documented in the header.

- **Owner.** `cross_correlate` (naive) in
  `src/core/correlation.{hpp,cpp}`.
- **Enforcement.**
  - Header docstring at `src/core/correlation.hpp:15-30` documents the
    NORMALIZATION CAVEAT (single GLOBAL norm factor `sqrt(total_ref_energy *
    total_tgt_energy)` applied uniformly at every lag, NOT per-lag slice
    energies; peak not bounded to `[-1, 1]` for arbitrary inputs);
    framed as "testing-only verification shim for cross_correlate_fft."
  - Shared `CorrelationResult.peak_value` field comment at `:12-15`
    enumerates per-impl ranges so the field-level documentation does
    not contradict the function-level docstring.
  - `cross_correlate_fft` docstring at `:76-80` scopes
    naive↔FFT comparability to lag selection only (peak magnitudes
    use different normalizations).
  - `FFT correlation agrees with naive implementation` at
    `tests/test_correlation.cpp:83-108` cross-checks the lag-selection
    invariant (`REQUIRE(fft_result.lag == true_lag)` and
    `REQUIRE(naive_result.lag == true_lag)`).
- **Status.** `holds` post-Mi-4 merge `c8db84b` (PR #45). Pre-cure the
  naive function's docstring claimed "FFT-based cross-correlation"
  (literally wrong) and asserted `(0-1)` peak range (only true for the
  FFT impl). Marker decision: prose framing instead of `[[deprecated]]`
  — `[[deprecated]]` would emit a warning at the regression-guard test
  callsite (`tests/test_correlation.cpp:99`) forcing either a `-Werror`
  break or a localized `#pragma` suppression with no semantic gain.
  Mi-4 audit caught two same-file adjacent-axis findings (struct field
  comment + FFT-side comparability claim) the fix-agent's naive-side-
  only sweep missed; both folded into the merge.

### INV-NATURAL-SORT-NEVER-THROWS — natural-sort comparators are total-order, never throw

`mwaac::natural_less` produces a strict-weak-ordering total order on
filenames containing arbitrary-length digit runs, and never throws on
any input. The path-taking wrapper `mwaac::natural_less_filename`
delegates to `mwaac::natural_less` and inherits the invariant
by construction.

- **Owner.** `natural_less` in `src/modes/reference_mode.{hpp,cpp}`;
  thin path-wrapper `natural_less_filename` in
  `src/modes/reaper_export.{hpp,cpp}` delegates to it. Both are exposed
  at `mwaac::` namespace scope (rather than file-static in an anonymous
  namespace) so unit tests can assert ordering and overflow behavior
  directly. Consistent with the existing precedent of
  `analysis_to_native_sample` exposed in `reference_mode.hpp` for the
  same testability reason.
- **Enforcement.**
  - **String surface (canonical).** Length-then-lex compare on
    zero-stripped digit strings inside the digit-run branch at
    `src/modes/reference_mode.cpp:716-762`. Mathematically equivalent
    to numeric compare for any in-range value AND well-defined for
    digit runs of any length. Pre-cure `std::stoll` calls deleted.
  - **Path surface (delegation).** `mwaac::natural_less_filename` at
    `src/modes/reaper_export.cpp:21-37` is a one-line delegation
    `return natural_less(a.filename().string(), b.filename().string());`,
    inheriting the no-throw guarantee from the canonical surface.
  - `Reference mode: natural filename sort ordering` at
    `tests/test_reference_mode.cpp:120-141` and `natural_less: digit
    run > 18 characters does not throw` at `:151-181` exercise the
    string surface directly (primary invariant + overflow regime).
  - `Reaper export: natural filename sort ordering` at
    `tests/test_reaper_export.cpp:19-40` and `Reaper export:
    natural_less_filename does not throw on >18-char digit run` at
    `:42-73` exercise the path-wrapper surface (primary invariant
    with path-prefix tie-break check + overflow regime mirroring
    Mi-17's structure).
- **Status.** `holds` on both surfaces.
  - String surface post-Mi-17 merge `4d542d3` (PR #46). Pre-cure
    `std::stoll` on each digit run threw `std::out_of_range` on runs > ~19
    chars and propagated out of the `std::sort` callsite at the file-
    static `natural_sort` (called by `load_reference_tracks`) to abort
    the program. Cure shape: option (c) per Mi-17 BACKLOG (manual
    digit comparison, never converts to integer).
  - Path surface post-M-REAPER-EXPORT-SORT-THROW merge `88e5267`
    (PR #47). Pre-cure `natural_less_filename` carried its own copy
    of the natural-sort algorithm with the identical std::stoll throw
    shape, sibling of Mi-17. Cure shape: delegation to
    `mwaac::natural_less`, which eliminates the duplicate-algorithm
    divergence risk (any future cure or regression to `mwaac::natural_less`
    automatically applies here).

### INV-INDEX-TYPE-DISJOINT — Sample-, frame-, and envelope-frame-index types are not implicitly convertible

`mwaac::detail::SampleIdx`, `mwaac::detail::FrameIdx`, and
`mwaac::detail::EnvFrameIdx` (all declared in
`src/core/frame_sample_bridge.hpp` — originally `src/modes/blind_mode_indices.hpp`
in M-6's PR #52; hoisted to `core/` by M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE
in PR #53 `0806db3` when the bridge gained a second consumer;
`EnvFrameIdx` added by M-REF-FRAME-SAMPLE-BRIDGE in PR #54 `a09af8f`
to serve the reference-mode envelope path) are opaque phantom-typed
structs with explicit-only constructors, no implicit conversions, no
arithmetic, no comparison. The only supported FrameIdx → SampleIdx
conversion is the `frame_to_sample(f, hop_length)` bridge in the same
header; the only supported EnvFrameIdx → SampleIdx conversion is the
sibling `env_frame_to_sample(f, frame_size)` bridge. The two frame
types are mutually disjoint — `FrameIdx` and `EnvFrameIdx` cannot
construct from each other, so passing an RMS-frame index where an
envelope-frame index is expected (or vice versa) fails to compile.
Thirteen `static_assert`s in the header pin the contract: any future
change that strips `explicit` (or otherwise relaxes the type
discipline) fails the build at a named static_assert rather than
silently regressing the cure.

- **Owner.** `src/core/frame_sample_bridge.hpp` (the types + both
  bridges); `src/modes/blind_mode.cpp` (`analyze_blind_mode`'s
  gap-iteration loop uses the FrameIdx bridge at the conversion
  sites); `src/core/music_detection.cpp` (`detect_music_start`'s
  frame-to-sample return path uses the FrameIdx bridge — adopted in
  PR #53); `src/core/analysis.cpp` (`compute_rms_energy` and
  `compute_zero_crossing_rate` frame-loop sample-base computations
  use the FrameIdx bridge — adopted in PR #55, root of the cure
  family since `compute_rms_energy` is upstream of every other
  RMS-frame consumer); `src/modes/reference_mode.cpp`
  (`measure_fade_in_samples` loop-base and return,
  `compute_rms_envelope` loop-base, and `envelope_refine_start`
  return all use the EnvFrameIdx bridge — adopted in PR #54).
- **Enforcement.**
  - 13 in-header `static_assert`s in
    `src/core/frame_sample_bridge.hpp` (6 on the FrameIdx/SampleIdx
    pair + 7 on the EnvFrameIdx pair including the symmetric
    `!std::is_constructible_v<SampleIdx, EnvFrameIdx>` close-out
    NIT added per audit-2 of PR #54) verify the types reject mutual
    construction, raw-int construction, and raw-int decay. The
    EnvFrameIdx set is intentionally mutually disjoint from FrameIdx
    so cross-mode index mixing fails to compile.
  - `M-6: SampleIdx/FrameIdx reject mixing and implicit conversion` at
    `tests/test_blind_mode.cpp:152-181` mirrors the FrameIdx in-header
    static_asserts as `STATIC_REQUIRE`s from outside the blind-mode
    TU, so a regression that strips `explicit` shows up as a
    recognisable test-source failure (not just an internal-header
    build error).
  - `M-6: frame_to_sample bridge multiplies by hop_length` at
    `tests/test_blind_mode.cpp:183-205` exercises the runtime
    FrameIdx bridge on representative inputs (frames 0, 1, 100, 397
    at hop 551) plus a constexpr-evaluation `STATIC_REQUIRE`.
  - `M-MUSIC-DETECT: SampleIdx/FrameIdx contract holds in core/` at
    `tests/test_music_detection.cpp:56-72` mirrors the same FrameIdx
    `STATIC_REQUIRE`s from the music-detection TU, so a regression
    fires recognisably in every TU that adopts the bridge (and at any
    future TU that includes the header).
  - `M-MUSIC-DETECT: detect_music_start returns a sample-index, not a frame-index`
    at `tests/test_music_detection.cpp:74-112` smoke-tests that the
    return value of `detect_music_start` is divisible by hop_length
    (= 551 at sr=44100), catching a hypothetical regression that
    returned the raw frame index `i` without the multiplication.
  - `M-REF-FRAME-SAMPLE-BRIDGE: EnvFrameIdx contract is disjoint from FrameIdx`
    in `tests/test_reference_mode.cpp` mirrors the EnvFrameIdx
    in-header static_asserts from the reference-mode TU, including
    the cross-disjointness pair with FrameIdx (passing an RMS frame
    index to the envelope bridge — or vice versa — fails to compile).
  - `M-REF-FRAME-SAMPLE-BRIDGE: env_frame_to_sample multiplies by frame_size`
    in `tests/test_reference_mode.cpp` exercises the runtime
    EnvFrameIdx bridge at representative envelope frame_sizes (50 ms
    and 100 ms at sr=44100) + constexpr-evaluation `STATIC_REQUIRE`.
  - `M-ANALYSIS-FRAME-SAMPLE-BRIDGE: SampleIdx/FrameIdx contract holds in analysis TU`
    in `tests/test_analysis.cpp` mirrors the FrameIdx in-header
    static_asserts from the analysis TU. The analysis TU is the
    upstream of every RMS-frame consumer in the codebase, so the
    contract fires recognisably from the root of the bridge family.
  - `M-ANALYSIS-FRAME-SAMPLE-BRIDGE: compute_rms_energy frame stride is hop_length`
    in `tests/test_analysis.cpp` exercises behavior preservation
    using a polarity-flip square-wave fixture aligned to hop_length;
    every frame's RMS == 1.0 when the bridge computes the correct
    frame-aligned sample base. (Behavior-preservation verification,
    not a regression-guard against revert — the contract block above
    is the regression-guard; this matches the established
    M-MUSIC-DETECT smoke-test pattern.)
- **Status.** `holds` (scoped to blind_mode + music_detection +
  reference_mode + analysis internals; cure family is at fixed point
  for the frame→sample axis in `src/`) post-M-ANALYSIS-FRAME-SAMPLE-BRIDGE
  merge `48263d1` (PR #55). Scope is explicitly internal — public APIs of
  `blind_mode.hpp`, `music_detection.hpp`, and `reference_mode.hpp`
  are unchanged; only the frame-to-sample crossings inside the
  curated functions use the typed forms. Public APIs across the rest
  of the codebase remain untagged size_t / int64_t per the
  user-authorized "scoped" cure choice (presented as 3-option
  AskUserQuestion 2026-05-17; user chose scoped over project-wide).
- **Adjacent-entry siblings filed (audit-2 catches):** M-6's typed
  bridge cured the frame×hop_length untagged-arithmetic pattern at
  the one site in blind_mode.cpp. Subsequent audit-2 grep sweeps
  surfaced same-shape sites in different TUs, each filed as a
  separate Tier 6 item per `feedback_tier_boundary_preservation.md`:
  - **M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE** — `src/core/music_detection.cpp:75`
    (`static_cast<int64_t>(i) * hop_length` in `detect_music_start`).
    **RESOLVED in PR #53 `0806db3`** — bridge hoisted from
    `src/modes/blind_mode_indices.hpp` to
    `src/core/frame_sample_bridge.hpp` and adopted at the cure-site.
    Bridge identity preserved across the hoist (same
    `mwaac::detail::SampleIdx`/`FrameIdx` types, same six static_assert
    contracts; rename-only).
  - **M-REF-FRAME-SAMPLE-BRIDGE** — `src/modes/reference_mode.cpp:332`,
    `:253` (originally filed) + `:200`, `:272` (audit-1 scope
    expansion for adjacent loop-base sites). **RESOLVED in PR #54
    `a09af8f`** — added EnvFrameIdx as a sibling type (mutually
    disjoint from FrameIdx by design — envelope frame strides differ
    from RMS-frame hops) with its own `env_frame_to_sample` bridge,
    and adopted at all four cure sites. 7 new EnvFrameIdx
    static_asserts (including the symmetric SampleIdx-from-
    EnvFrameIdx assert added in close-out per audit-2 NIT).
  - **M-ANALYSIS-FRAME-SAMPLE-BRIDGE** — `src/core/analysis.cpp:26`
    (`compute_rms_energy`) and `:57` (`compute_zero_crossing_rate`)
    have the `size_t start = i * static_cast<std::size_t>(hop_length)`
    shape producing sample offsets. Filed by audit-2 of PR #54;
    same defect class as M-MUSIC-DETECT in the analysis TU that is
    upstream of every other RMS-frame consumer. **RESOLVED in PR #55
    `48263d1`** — pure adoption of the existing FrameIdx +
    frame_to_sample bridge (no new types). Closes the cure family
    at its root.
  - **M-CORRELATION-FRAME-SAMPLE-BRIDGE** — `src/core/correlation.cpp:145`
    (`size_t start = i * static_cast<std::size_t>(factor)` in
    `downsample`). Filed by audit-1 of PR #55; audit-2 classified as
    not-a-match (inter-lattice sample-to-sample mapping rather than
    frame×stride). **RESOLVED INVESTIGATE-ONLY 2026-05-18 (commit
    after this one)** — orchestrator gate-eval confirmed audit-2's
    framing: downsample is rate conversion between two sample-domain
    lattices, not a frame extraction. Sibling `:214` (`coarse_lag =
    best_coarse_lag * downsample_factor`) is the structural
    confirmation — both operands are sample-domain lag-quantities.
    Generalization: the bridge defect class is "X-domain index ×
    per-X-element stride → Y-domain offset where X and Y are
    semantically different kinds"; inter-lattice mappings between
    same-kind lattices at different rates are NOT in the family.

### INV-NO-DEAD-PARAMS — Public APIs do not carry dead parameters

Public function signatures do not carry `[[maybe_unused]]` parameters
reserved for a future implementation that has not yet landed.

- **Owner.** Project-wide; specifically called out on `score_gap` in
  `src/modes/blind_mode.{hpp,cpp}`.
- **Enforcement.** Code review (the invariant is a documentation
  contract rather than a build-time check). The current state is
  verified by grep: no `[[maybe_unused]]` parameters remain on public
  function declarations in `src/modes/` (one `[[maybe_unused]] float
  window_seconds` remains in `src/core/music_detection.cpp:14` on
  `estimate_noise_floor` but is documented as API-stability
  scaffolding across noise-floor algorithm changes, not a placeholder
  for unimplemented behavior — distinct from M-7's pattern).
- **Status.** `holds` post-M-7 merge `02eef0c` (PR #50). Pre-cure
  `score_gap` carried `[[maybe_unused]] int sample_rate` reserved for
  a future spectral-flatness scoring path that belongs to C-5's scope.
  Cure: removed the parameter rather than implementing the deferred
  scoring (YAGNI; folding C-5 would have violated single-function
  scope and tied M-7 to C-5's stub implementation). Mi-7 ("score_gap
  drops sample_rate") was explicitly a duplicate of M-7 and is closed
  by the same merge.

### INV-BLIND-SINGLE-TRACK — Blind mode returns a single-split result on a gap-free input

`analyze_blind_mode` on an input that produces zero gap candidates
(either because `detect_gaps` finds no contiguous below-threshold
region or because every candidate region is outside the
[min_gap_seconds, max_gap_seconds] window) returns a successful
`AnalysisResult` containing a single SplitPoint that spans the entire
input (start_sample = 0, end_sample = samples.size() - 1), with
`confidence = 1.0` and `metadata["num_gaps_found"] = 0`. A gap-free
input is a legitimate outcome, not an error.

- **Owner.** `analyze_blind_mode` in `src/modes/blind_mode.cpp` —
  specifically the gap-empty fall-through path (no early-return on
  `gaps.empty()`).
- **Enforcement.**
  - Cure-rationale comment block at `src/modes/blind_mode.cpp:182-191`
    documents the gap-empty fall-through behavior.
  - `analyze_blind_mode: single-track (gap-free) input returns 1 split`
    at `tests/test_blind_mode.cpp:69-141` exercises the cure path with
    a 1-second tone fixture (chosen specifically because
    1 s < min_gap_seconds=2 s forces `detect_gaps` to drop the
    candidate). Asserts the 1-split result shape (start_sample 0,
    end_sample samples.size()-1, source "blind", confidence 1.0) AND
    `metadata["num_gaps_found"] == 0.0` (second-axis guard against
    the M-8 audit-1 catch — a longer-tone fixture would pass through
    the score-rejection path rather than the cure path, masking
    whether the cure works).
- **Status.** `holds` post-M-8 merge `5c533da` (PR #51). Pre-cure the
  function returned `BlindError::NoGapsFound` on empty gaps; the CLI
  surfaced this as an "Analysis failed" exit. Confidence-value
  interpretation: 1.0 means "single-track assertion is well-supported
  by absence of gap evidence" — there is no gap evidence contradicting
  the structural claim, so the assertion is maximally certain at the
  structural level. (Alternative interpretation "0.5 = no evidence
  either way" was considered and rejected: blind mode's confidence
  values are about the structural claim's support, not about how much
  uncertainty exists in the underlying audio.)
- **Adjacent structural-sibling filed:** M-REF-NO-TRACKS-OUTCOME
  (Tier 6, commit `e8261d8`) tracks `ReferenceError::NoTracksFound`
  for investigation. Structurally similar enum-value-as-error pattern
  but likely a true user-config error (vs. M-8's algorithm-finds-
  nothing); cure shape pending investigation outcome.

### INV-BLIND-CLEAN-2TRACK — Blind mode finds ≥2 splits on a clear two-track fixture

`analyze_blind_mode` on a clean 2-track synthetic vinyl rip with an
inter-track silence ≥ `min_gap_seconds` returns ≥ 2 split points (one
implicit at sample 0 plus one for each detected gap that passes the
confidence threshold).

- **Owner.** `analyze_blind_mode` and `score_gap` in
  `src/modes/blind_mode.cpp`.
- **Enforcement.**
  - `Blind mode pipeline: gap detection` in `tests/test_integration.cpp:492`
    (assertion at `:525`) — exercises a 2s-tone + 3s-silence + 2s-tone
    fixture at 44100 Hz. Pre-cure the gap was rejected at the
    confidence gate; post-cure the assertion `split_points.size() >= 2`
    passes.
  - `Combined workflow: reference then blind analysis` in
    `tests/test_integration.cpp:712` (assertion at `:769`) — exercises
    a 1s-tone + 3s-silence + 1s-tone fixture at 22050 Hz with the
    reference-mode side-channel (`(void)`'d pending FIXTURE-REF
    coverage). Post-cure passes.
- **Status.** `holds` post-NEW-BLIND-GAP merge `7c0bc4a` (PR #48).
  Pre-cure the score_gap parameter `noise_floor_rms` was
  passed the noise-floor estimate from `analyze_blind_mode`, but the
  formula `1 - gap_rms / ref` only yields meaningful confidence when
  `ref` is a SIGNAL reference level — on a fixture where silence
  dominates the signal duration (~42% in the failing fixture), the
  10th-percentile noise-floor estimate equals the gap RMS by
  construction and the formula degenerates to 0, rejecting every
  detected gap. Cure renamed the parameter to `signal_reference_rms`
  and added a caller-side estimator (p90 of frame RMS — sits in the
  music region for any fixture where music ≥ 10% of signal duration).
  See `INV-SCORE-GAP-REFERENCE-IS-SIGNAL-LEVEL` below for the
  parameter-contract invariant.

### INV-SCORE-GAP-REFERENCE-IS-SIGNAL-LEVEL — `score_gap`'s reference parameter is a signal level, not a noise floor

`score_gap`'s 5th parameter (`signal_reference_rms`) is semantically
the typical loudness of the surrounding music — a reference level
against which the gap's quietness is measured by the formula
`1 - gap_rms / signal_reference_rms` (clamped to `[0, 1]`). Callers
must NOT pass a noise-floor estimate or any other quiet-floor value:
the formula degenerates to ~ 0 when `signal_reference_rms ≈ gap_rms`,
so the parameter must be a LOUD reference (e.g. high percentile of
frame RMS, or mean of frames above the gap-detection threshold).

- **Owner.** `score_gap` in `src/modes/blind_mode.{hpp,cpp}`.
- **Enforcement.**
  - 25-line header docstring at `src/modes/blind_mode.hpp:43-69` documents
    the formula, names the previous noise-floor naming bug, and gives
    caller-side estimator guidance.
  - `Gap scoring based on energy` in `tests/test_blind_mode.cpp:21-32`
    encodes the contract through assertion: passes 0.5 (the LOUD
    amplitude of the test samples) as the 5th argument and asserts
    `score > 0.9`. A future caller passing the wrong value (e.g.
    a noise floor) would be caught by this test only if the
    pre-cure noise-floor naming were re-introduced; the docstring
    is the primary structural defense.
  - Production caller `analyze_blind_mode` in
    `src/modes/blind_mode.cpp` computes `signal_reference_rms = p90`
    of sorted frame RMS values and passes that explicitly.
- **Status.** `holds` post-NEW-BLIND-GAP merge `7c0bc4a` (PR #48).
  Promoted from the cure to a documented INV per
  `feedback_close_followups_before_next_epic.md`-style discipline:
  audit-2 on PR #48 noted the contract was load-bearing (the entire
  cure pivots on it) but had no INV; this entry pins it.

### INV-RESULT-NO-AMBIGUOUS-DEFAULT — No default construction leaves a result wrapper ambiguous

Subsumed by INV-RESULT-WRAPPER above. With `Expected<T,E>` backed by
`std::variant<T,E>` post-M-14, default construction follows the
variant's defined behaviour (default-constructs the first alternative,
`T`, when `T` is default-constructible); no aligned-storage
discriminant can be left unset. `LoadResult`'s ambiguous default
constructor is removed from the tree.

- **Status.** Closed by M-14. M-11 was filed as a duplicate;
  documented in BACKLOG.md M-11 entry.
- **Dependents.** M-11 (closed via M-14), M-14 (RESOLVED #31).

### INV-RESAMPLE-FALLIBLE — `resample_linear` surfaces precondition violations as `ResampleError`

`resample_linear` returns `Expected<AudioBuffer, AudioError>` (not the
pre-Mi-3 infallible `AudioBuffer`). Two error producers, both yielding
`AudioError::ResampleError`: (1) `input.sample_rate <= 0` (the
historical Mi-3 div-by-zero precondition); (2) `output_size` overflow
during the `double → size_t` conversion at the buffer-sizing line,
guarded with `std::isfinite` / non-negative / `<= numeric_limits
<size_t>::max()` before the cast. Establishes that
`AudioError::ResampleError` has a real producer (no contract-lie
enum value) under the post-M-14 unified taxonomy.

- **Owner.** `resample_linear` in `src/core/audio_buffer.cpp`;
  one production caller `load_audio_mono` already returns
  `Expected<AudioBuffer, AudioError>` so the `Expected` propagates.
- **Enforcement.**
  - `resample_linear: sample_rate == 0 returns ResampleError` in
    `tests/test_audio_buffer.cpp`.
- **Dependents.** Mi-3 (RESOLVED #33; structural cure deviates from
  the original "early return `{}`" spec — see `docs/deviations.md`
  Mi-3 entry for reasoning), M-14 (RESOLVED #31; the unified
  taxonomy made this cure the right shape).

### INV-SPLITPOINT-ORDER — `0 ≤ start ≤ end ≤ total - 1` for every SplitPoint

For every `SplitPoint` produced by the analysis pipelines and
maintained by the TUI editing surface:
`0 ≤ start_sample ≤ end_sample ≤ total_samples - 1`. Within-marker
ordering is preserved trivially by block-shift nudge semantics
(both edges shift by the same delta — see Mi-8 cure). Cross-marker
no-gap invariant `markers[i].end_sample < markers[i+1].start_sample`
is maintained by the sibling clamp in the TUI nudge mutators
(blind_mode and reference_mode pipelines produce it via the
post-loop end_sample fill-in; TUI editing must not break it).

- **Owner.** TUI marker editing in `src/tui/app_handlers.cpp`
  (`nudge_marker_right` + `nudge_marker_left`); algorithmic
  producers in `src/modes/blind_mode.cpp` and
  `src/modes/reference_mode.cpp`.
- **Enforcement.**
  - In-mutator clamping: `nudge_marker_right` refuses to nudge if
    the result would exceed `min(total_samples - 1, next_marker.start_sample - 1)`;
    `nudge_marker_left` refuses if the result would fall below
    `max(0, prev_marker.end_sample + 1)`.
  - `tests/test_app_handlers.cpp` 11 TEST_CASEs (7 Mi-8 regression-
    guards + 4 invariant locks). Classification annotated in the
    file's header per Mi-8 audit-1 finding 2 so future-Mi-* authors
    extending the harness preserve the regression-guard ratio.
  - `duration_samples()` preservation test asserts the within-marker
    invariant across long nudge sequences — locks the block-shift
    property against a future regression that might shift one edge
    without the other.
- **Status.** `holds` (block-shift semantics) post-Mi-8 merge
  `0980606` (PR #58). See **Mi-MARKER-NUDGE-SEMANTIC** in
  `BACKLOG.md` for the audit-2 UX discovery that the block-shift
  semantic combined with blind_mode's algorithmic-output
  gap-of-exactly-1 makes interior markers in blind-mode output
  unable to nudge in either direction (only first-marker-left and
  last-marker-right can move). Pending user judgment on whether to
  switch to boundary-shift semantics.

### INV-VIEW-NON-INVERTED — `0 ≤ view_start < view_end ≤ total_samples` in TUI

- **Status.** `pending` (Mi-9).

### INV-RUN-TUI-EXIT-CODE — `run_tui` returns `0` on normal exit

`run_tui` returns 0 on any normal exit from the event loop —
user-initiated quit ('q'/'Q'), Ctrl-C signal handled by the FTXUI
screen loop, or terminal disconnect. Pre-cure (Mi-10) returned
`quit ? 0 : 1` — the `quit` sentinel was set only by the 'q'/'Q'
handler, so Ctrl-C and any other non-Q exit path returned 1,
inverting the documented contract.

- **Owner.** `run_tui` in `src/tui/app.cpp`.
- **Enforcement.**
  - `src/tui/app.cpp:271` (post-cure) returns `0` unconditionally;
    the `quit` sentinel was removed (one-set/one-read; only reader
    was the inverted return). `screen.Exit()` in the 'q'/'Q' handler
    at `:154-163` remains the actual quit mechanism.
  - `src/tui/app.hpp:35-46` docstring rewritten to make the contract
    explicit, with the audit-1 catch about FTXUI's no-throw semantics
    softened from "propagates as a thrown exception" to "best-effort
    with no throwing failure modes ... vacuously satisfied" (verified
    against the vendored FTXUI source — zero `throw` in
    screen_interactive.cpp).
- **Status.** `holds` post-Mi-10 merge `f359e19` (PR #57).
- **Test gap acknowledgment.** No behavioral test exists for the
  exit-code contract. `run_tui` blocks on terminal input via
  `screen.Loop()` which cannot be invoked from a unit test without
  a headless TUI state-mutator harness — that harness work is filed
  under Mi-8 and Mi-9. Cure is verification-by-code-review; the
  invariant becomes test-enforceable when the Mi-8/Mi-9 harness
  lands.

### INV-THIRD-PARTY-ATTRIBUTION — Vendored third-party files carry attribution

- **Status.** `pending` (M-13).

### INV-MAGIC-NUMBER-CITATION — Decision thresholds are `constexpr` with a citation

- **Status.** `pending` (Mi-5).

### INV-README-CLAIM-TESTED — Every README claim is enforced or rewritten

- **Status.** `pending` (DOC-1, DOC-2).

---

## Cross-references

- Fixture manifests: `tests/fixtures/ref_v1/manifest.txt`,
  `tests/fixtures/rf64/manifest.txt`,
  `tests/fixtures/waveext/manifest.txt`,
  `tests/fixtures/malformed/manifest.txt`.
- Fixture READMEs: `tests/fixtures/<id>/README.md`.
- Deviations: `docs/deviations.md`.
- Backlog: `BACKLOG.md`, `BACKLOG.archive.md`.
