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
`ds64` follows the `data` chunk. *Currently violated.*

- **Owner.** `parse_wav_header` + `tests/fixtures/rf64/rf64_ds64_after.wav`.
- **Enforcement.** `parse_wav_header: RF64 with ds64 after data`
  `[!shouldfail]` in `tests/test_lossless.cpp`. Tag drops when M-4 lands.
- **Dependents.** FIXTURE-RF64, M-4.

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
`data_size` match the on-disk header. *Currently violated.*

- **Owner.** `parse_wav_header` + the `pcm_24bit_stereo.wav` /
  `pcm_24bit_5ch.wav` fixtures in `tests/fixtures/waveext/`.
- **Enforcement.** Two `[!shouldfail]` tests in `tests/test_audio_file.cpp`:
  `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM 24-bit stereo accepted`
  and `... 5-channel accepted`. Tag drops when M-3 lands.
- **Dependents.** FIXTURE-WAVEEXT, M-3.

### INV-WAVEEXT-2 — Unknown SubFormat GUID returns UnsupportedFormat

`parse_wav_header` returns `UnsupportedFormat` (not `InvalidFormat`, not a
crash) when the SubFormat GUID is neither PCM nor IEEE-float. *Currently
satisfied incidentally (parser rejects all 0xFFFE today); the post-M-3
test verifies this is the deliberate path.*

- **Owner.** `parse_wav_header` + `pcm_extensible_unsupported_subformat.wav`.
- **Enforcement.** `parse_wav_header: extensible with unknown SubFormat
  returns UnsupportedFormat` `[!shouldfail]` in
  `tests/test_audio_file.cpp`. Tag drops when M-3 lands and the parser
  inspects GUIDs deliberately.
- **Dependents.** FIXTURE-WAVEEXT, M-3.

### INV-WAVEEXT-3 — Round-trip of an EXTENSIBLE source preserves data bytes

`write_track` of a `WAVE_FORMAT_EXTENSIBLE` source produces an output WAV
whose data section is byte-identical to the requested region of the
source's data section. *Currently violated.*

- **Owner.** `write_track` + `build_*_header` + the `pcm_24bit_stereo.wav`
  fixture.
- **Enforcement.** `Lossless: 24-bit 2-ch extensible WAV round-trip
  preserves bytes` `[!shouldfail]` in `tests/test_lossless.cpp`. Tag drops
  when M-3 *and* the EXTENSIBLE-emit piece (NEW-WAVEEXT-WRITE) land.
- **Dependents.** FIXTURE-WAVEEXT, NEW-WAVEEXT-WRITE, M-3.

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
- **Dependents.** FIXTURE-MALFORMED, M-2, M-3, M-4, M-5, C-5.

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
  malformations` `[!shouldfail]` for the M-2 / M-5 set). Behaviour for
  `fmt_size_max.wav` deviates from the spec's implied
  `InvalidFormat`; see `docs/deviations.md`.
- **Dependents.** FIXTURE-MALFORMED, M-2, M-3, M-5.

## Implementation invariants (named in BACKLOG.md, enforcement pending)

These are invariants the backlog promises will be enforced when the
named item lands. Listed here so that, when DOC-3 is updated after an
item lands, only this section's entry needs to move into the relevant
"Owner / Enforcement" form.

### INV-FLOAT80-10BYTES — `encode_float80` writes exactly 10 bytes

- **Status.** `pending` (C-1).
- **Owner-to-be.** `encode_float80` in `src/core/audio_file.cpp`.

### INV-EXPECTED-PRECONDITION — `Expected<T,E>::value()` is a precondition

Calling `value()` on an errored `Expected` is loud (assert / terminate),
not silent UB.

- **Status.** `pending` (C-2; full migration is M-14 / INV-RESULT-WRAPPER).
- **Owner-to-be.** `Expected<T,E>` in `src/core/audio_file.hpp`.

### INV-RESULT-WRAPPER — One result wrapper, defined behaviour

The codebase has exactly one error-result wrapper, with defined behaviour
on every accessor.

- **Status.** `pending` (M-14).

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

`data_offset = SSND_body + 8 + offset`,
`data_size = chunk_size - 8 - offset`.

- **Status.** `pending` (M-5). Gates
  FIXTURE-MALFORMED's `aiff_ssnd_offset_nonzero.aiff`.

### INV-AIFF-SAMPLERATE — `parse_aiff_header` decodes the 80-bit sample rate

`parse_aiff_header` produces an `AudioInfo` whose `sample_rate` matches
the file header (no `0`).

- **Status.** `pending` (Mi-1). Inverse of the (post-C-1) `encode_float80`.

### INV-CLI-OPEN-GUARD — CLI short-circuits on failed `AudioFile::open`

Every `main.cpp` branch that uses `audio_file.value()` is preceded by a
validated guard.

- **Status.** `pending` (C-2 + M-15).

### INV-WRITE-ATOMIC — `write_track` is all-or-nothing at the target path

`write_track` produces either the complete output file or no file at the
target path.

- **Status.** `pending` (M-16).

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

- **Status.** `pending` (M-11; resolved by M-14).

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
