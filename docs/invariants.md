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
  `analyze_reference_mode` in `src/modes/reference_mode.cpp`.
- **Enforcement.** `Reference mode pipeline: track positions within
  tolerance` in `tests/test_integration.cpp` (and the structural pre-check
  in `Reference mode pipeline: basic detection`).
- **Dependents.** FIXTURE-REF (closes), C-4 (tightens tolerance to one
  native sample once rate-conversion truncation is fixed; `kRefFixtureToleranceSamples`
  is the upper bound, the test continues to assert against it after C-4).

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

- **Status.** `pending` (C-4). Note INV-REF-1 currently tolerates 50 ms;
  C-4 tightens the achievable accuracy and INV-RATECONV-ROUNDED is the
  unit-level invariant that makes that possible. INV-REF-1 stays at the
  envelope-frame floor.

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

- **Status.** `pending` (M-9).

### INV-ZCR-SHORT-FRAME — `compute_zero_crossing_rate` is 0 below 2 samples

ZCR is defined as `0` for frames of length `< 2`.

- **Status.** `pending` (M-10).

### INV-CC-NORMALIZATION — Naive cross-correlate is a verification shim

The naive `cross_correlate` is a verification shim for the FFT
implementation; callers using its peak as a probability are using it
wrong. Documented in the header.

- **Status.** `pending` (Mi-4).

### INV-INDEX-TYPE-DISJOINT — Sample- and frame-index types are not implicitly convertible

- **Status.** `pending` (M-6).

### INV-NO-DEAD-PARAMS — Public APIs do not carry dead parameters

`score_gap` either uses `sample_rate` or removes it from the signature.

- **Status.** `pending` (M-7, Mi-7).

### INV-BLIND-SINGLE-TRACK — Blind mode returns a single-split result on a gap-free input

- **Status.** `pending` (M-8).

### INV-BLIND-CLEAN-2TRACK — Blind mode finds ≥2 splits on a clear two-track fixture

- **Status.** `pending` (NEW-BLIND-GAP).

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

- **Status.** `pending` (Mi-8).

### INV-VIEW-NON-INVERTED — `0 ≤ view_start < view_end ≤ total_samples` in TUI

- **Status.** `pending` (Mi-9).

### INV-RUN-TUI-EXIT-CODE — `run_tui` returns `0` on normal exit

- **Status.** `pending` (Mi-10).

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
