# Remediation Backlog

This backlog implements the Knuth-level review of `mwAudioAutoChop-C++`. Every
item has an ID corresponding to the review (C-#, M-#, Mi-#, N-#) or a new ID
minted here (FIXTURE-#, INV-#, DOC-#). The prior BACKLOG.md is archived at
`BACKLOG.archive.md`.

Execution rules (from the remediation plan):

- **One backlog item per PR.** No batching.
- **Every item closes via an audit-agent pass.** The fix-agent's self-check
  does not close the item.
- **Critical items get two audit passes.**
- **Red CI halts new dispatches.** Fix the regression before starting anything
  new.
- **Deviations are recorded in `docs/deviations.md`**, not negotiated silently.

Status legend: `[ ]` pending · `[~]` in progress · `[x]` closed · `[!]`
blocked · `[D]` deviated (see docs/deviations.md).

---

## Phase 0 — Foundation (COMPLETE)

Recorded here for the paper trail.

- [x] **F-1** pkg-config IMPORTED_TARGET for libsndfile *(M-1)* — commit
  d3c2043.
- [x] **F-2** MWAAC_WERROR + MWAAC_SANITIZE harness — commit b48159d.
- [x] **F-3** CI matrix: Debug, sanitizers, clang-tidy — commit 35fee39.
- [x] **F-4** Re-enable disabled AIFF tests *(C-1 surfacing)* — commit c32d081.
- [x] **F-5** Neutralize CHECK(true) patterns — commit d5f8c1e.

After Phase 0, the build is red in a disciplined way: 89 warning-as-error
findings (one dominant category per file), 1 sanitizer-confirmed stack smash
(C-1), and two newly-honest integration test failures (blind mode returning
only 1 split, 24-bit 2ch write failing). Each is a clean target for Phase 2.

---

## Tier 1 — Unblockers (test fixtures)

Produce the reproducible synthetic fixtures the existing tests pretend they
have. Without these, reference-mode tests can't assert correctness.

### FIXTURE-REF — Realistic synthetic vinyl fixture

- **Defect.** Integration tests for reference mode generate tones-in-noise,
  which reference mode cannot reliably align against (no distinctive envelope
  shape, rhythmic tones produce ambiguous correlation peaks). Tests that
  depended on this currently `SKIP()`.
- **Invariant established.** "Reference mode aligns each track of a known
  fixture within ±N samples of the ground-truth boundary, where N ≤ 1 ms
  at the fixture's sample rate."
- **Files touched.** `tests/fixtures/build_fixtures.cpp` (new),
  `tests/fixtures/README.md` (new), `tests/fixtures/ref_fixture_v1/` (generated
  artifacts + ground-truth JSON), `tests/test_integration.cpp` (swap SKIP for
  REQUIRE on the three reference cases).
- **Tests added/re-enabled.**
  - `Reference mode pipeline: basic detection` (un-SKIP)
  - `Reference mode pipeline: track positions within tolerance` (un-SKIP)
  - `Reference mode pipeline: lossless export verification` (un-SKIP)
- **Exit criteria.**
  - [ ] Fixture is reproducible from `build_fixtures` invocation; no binary
        blobs in-tree unless a SHA-256 checksum is also committed.
  - [ ] Three previously-SKIP'd test cases now run and pass.
  - [ ] Each test asserts position within a named tolerance constant.
  - [ ] Fixture README lists what each fixture exercises.

### FIXTURE-RF64 — >4 GiB RF64 fixture (sparse file)

- **Defect.** No RF64 round-trip coverage. Required for C-3.
- **Invariant established.** "For an RF64 input, write_track produces an RF64
  output whose sample-data region is byte-identical to the source region."
- **Files touched.** `tests/fixtures/build_fixtures.cpp`,
  `tests/test_lossless.cpp`.
- **Tests added.**
  - `Lossless: RF64 round-trip preserves sample bytes` (new).
  - `RF64 header parsing: ds64 before data` (new).
  - `RF64 header parsing: ds64 after data` (new, M-4).
- **Exit criteria.**
  - [ ] Sparse-file creation runs in <1 s on CI workers; disk footprint
        near zero.
  - [ ] Round-trip compares SHA-256 of sample region, not full file.

### FIXTURE-WAVEEXT — 24-bit WAVE_FORMAT_EXTENSIBLE fixture

- **Defect.** No coverage for the format Pro Tools / REAPER / modern Audacity
  emit. Required for M-3 and the newly-surfaced 24-bit-2ch write failure.
- **Invariant established.** "AudioFile::open accepts WAVE_FORMAT_EXTENSIBLE
  (0xFFFE) whose SubFormat GUID identifies PCM or IEEE-float."
- **Files touched.** `tests/fixtures/build_fixtures.cpp`,
  `tests/test_audio_file.cpp`, `tests/test_lossless.cpp`.
- **Tests added.**
  - `AudioFile::open: WAVE_FORMAT_EXTENSIBLE 24-bit stereo` (new).
  - `Lossless round-trip: 24-bit 2-channel WAV (extensible)` (re-enable
    the currently-failing e2e format test).
- **Exit criteria.**
  - [ ] Fixture is produced from raw byte assembly (not libsndfile, which
        would write legacy PCM format-tag 1), so the extensible path is
        exercised in the parser.

### FIXTURE-MALFORMED — Truncated and malformed header corpus

- **Defect.** Parser hardening (M-3, M-4, M-5) needs inputs; currently none
  exist.
- **Invariant established.** "Every malformed header returns `InvalidFormat`
  within bounded time; none triggers ASan/UBSan findings."
- **Files touched.** `tests/fixtures/malformed/` (new: tiny hand-written
  files), `tests/test_audio_file.cpp`.
- **Tests added.** Parametric over `tests/fixtures/malformed/*.wav|aiff`.
- **Exit criteria.**
  - [ ] At least: <12-byte file, valid RIFF + truncated fmt chunk, data
        chunk claiming size > file size, RF64 without ds64, SSND with
        non-zero offset field.

---

## Tier 2 — Critical correctness (review IDs C-* and latent-Critical M-*)

Order within this tier is by dependency: the Expected unification (M-14) is a
Tier 3 item but touches every C-2 call site, so C-2 is scoped to *signalling
the UB in the existing type* first (assert-or-terminate), and the full
migration to `std::expected`-style storage happens in M-14.

### C-1 — encode_float80 stack buffer overflow

- **Defect.** `encode_float80` writes 11 bytes into a 10-byte `std::byte`
  buffer at `audio_file.cpp:496` (`out[10] = ...`). Buffer declared at
  `:659, :670` as `std::byte float80[10]`.
- **Invariant established.** "encode_float80 writes exactly 10 bytes to its
  output buffer, matching IEEE 754 80-bit extended precision wire format."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added/re-enabled.** Two `[lossless]` AIFF tests already re-enabled
  in F-4 currently crash under ASan. The fix makes them pass.
- **Exit criteria.**
  - [ ] ASan reports no stack-buffer-overflow on `test_lossless` under
        `MWAAC_SANITIZE=ON`.
  - [ ] AIFF mantissa layout matches a reference implementation (see
        comment citation in the diff).
  - [ ] Re-audit comment on the function explains why 10 bytes is correct.

### C-2 — `Expected<T,E>::value()` is UB when holding error

- **Defect.** `audio_file.hpp:106–116` `return *reinterpret_cast<T*>(&storage_)`
  without checking `has_value_`. Call sites: `main.cpp:160, 250–260, 300`.
- **Invariant established.** "Every `Expected<T,E>::value()` call is a
  precondition contract; violation is noisy (assert / terminate), not silent
  UB."
- **Files touched.** `src/core/audio_file.hpp`, `src/main.cpp`.
- **Tests added.**
  - `Expected: value() on errored object aborts under debug` (new, uses
    `Catch::Matchers::Throws` or a death-test equivalent).
  - `main: failed AudioFile::open exits cleanly` (new, subprocess test).
- **Exit criteria.**
  - [ ] `Expected<T,E>::value()` has a precondition check (`assert` in
        Debug, `std::terminate` otherwise) — *not* a full reimplementation;
        that lands in M-14.
  - [ ] Every `audio_file.value()` call in `main.cpp` is guarded by
        `if (!audio_file) { ... return 1; }` on first use.
  - [ ] UBSan passes on full test suite.
- **Audit mandate (orchestrator-recorded).** The C-2 audit pass must
  explicitly evaluate the `Expected` API shape choice — assert + `value_or`
  on top of the current placement-new-in-aligned-storage layout, vs.
  full migration to a `std::variant<T, E>`-backed implementation — and
  record that decision in `docs/deviations.md` under a C-2 entry (or a
  dedicated `docs/decisions/expected-api.md`). The decision propagates
  into M-14 (contract unification) and M-11 (LoadResult removal).
  We decide `Expected`'s shape *once*, at C-2; M-14 then carries it out.

### C-3 — RF64 output silently truncated above 4 GiB

- **Defect.** `build_wav_header` writes `file_size` / `data_size` as `uint32_t`
  fields (`audio_file.cpp:531, 591–594`). write_track never emits RF64.
- **Invariant established.** "For any `bytes_to_write > 0xFFFFFFFE`, or when
  the source file format is RF64, write_track emits a valid RF64 header with
  ds64."
- **Files touched.** `src/core/audio_file.hpp` (add `build_rf64_header`),
  `src/core/audio_file.cpp` (implement, route from write_track),
  `tests/test_lossless.cpp` (assertion; needs FIXTURE-RF64).
- **Tests added.**
  - `build_rf64_header: RIFF+ds64+data layout` (new, unit).
  - `Lossless: RF64 round-trip preserves sample bytes` (new, via
    FIXTURE-RF64).
- **Depends on.** FIXTURE-RF64.
- **Exit criteria.**
  - [ ] For any data_size ≥ 0xFFFFFFFE, header is RF64 not RIFF.
  - [ ] For RF64 *input*, output is RF64 regardless of size (preserves
        format identity).
  - [ ] Round-trip SHA-256 on sample region matches source.
  - [ ] Remove the `[!shouldfail]` tag on the RF64 round-trip test in
        `tests/test_lossless.cpp` in the same PR.

### C-4 — Rate-conversion truncation breaks "sample-accurate" claim

- **Defect.** `reference_mode.cpp:1109, 1111` convert analysis-rate sample
  indices to native-rate via integer division, truncating up to ~9 samples
  at 192 kHz.
- **Invariant established.** "Reference mode boundaries are within one
  native-rate sample of the analysis-rate result."
- **Files touched.** `src/modes/reference_mode.cpp`.
- **Tests added.**
  - `Reference mode: native-rate boundary is rounded not truncated` (new,
    unit-level, using a direct analysis→native conversion helper).
- **Exit criteria.**
  - [ ] Conversion uses rounding (`std::llround` or `(a*num + den/2)/den`).
  - [ ] A dedicated helper function carries the rounding; inline
        multiplications by native_sr/analysis_sr are removed.
  - [ ] README's "sample-accurate" claim is reconciled (DOC-1) with the
        achievable tolerance.

### C-5 — `compute_spectral_flatness` unsigned wrap + stub implementation

- **Defect.** `analysis.cpp:82–84` wraps on `samples.size() < frame_length`,
  requesting a ~2⁶³ allocation. The body also returns all-0.5 placeholder
  values.
- **Invariant established.** "Every analysis function that declares a public
  signature delivers on it (no stub returns that masquerade as data) or is
  removed from the public header."
- **Files touched.** `src/core/analysis.hpp`, `src/core/analysis.cpp`,
  `tests/test_analysis.cpp`.
- **Tests added.**
  - `compute_spectral_flatness: short input returns empty, not crash` (new).
  - `compute_spectral_flatness: flat noise gives flatness near 1` (new).
  - `compute_spectral_flatness: pure tone gives flatness near 0` (new).
- **Exit criteria.**
  - [ ] Either a real FFT-based implementation lands (using the vendored
        pocketfft) or the function is removed from the public header and
        all call sites (none currently).
  - [ ] Guard mirrors `compute_rms_energy`.

### M-2 — AudioFile::open does not cross-check parser vs libsndfile

- **Defect.** `audio_file.cpp:151–176` mixes hand-parser fields and libsndfile
  fields without reconciliation.
- **Invariant established.** "After AudioFile::open, `info.frames *
  info.bytes_per_frame() == info.data_size`. Violation returns
  `InvalidFormat`."
- **Files touched.** `src/core/audio_file.cpp`, `tests/test_audio_file.cpp`.
- **Tests added.**
  - `AudioFile::open: parser/libsndfile disagreement surfaces as
    InvalidFormat` (new, needs a malformed fixture).
- **Depends on.** FIXTURE-MALFORMED.
- **Exit criteria.**
  - [ ] Assert or error-return on size mismatch.
  - [ ] bits_per_sample comes from a single authoritative source.

### M-15 — CLI continues after failed AudioFile::open with total_frames=0

- **Defect.** `main.cpp:253–260` uses `audio_file ? ... : 0` pattern then
  unconditionally writes tracks, producing `sp.end_sample = -1`.
- **Invariant established.** "Every CLI branch that depends on a successful
  AudioFile::open must short-circuit on failure before using the result."
- **Files touched.** `src/main.cpp`.
- **Tests added.**
  - Covered by C-2's `main: failed AudioFile::open exits cleanly` test.
- **Exit criteria.**
  - [ ] Single guard immediately after the `open` call.
  - [ ] No `audio_file.value()` calls in main.cpp that aren't preceded by a
        validated guard.

### M-16 — write_track is not atomic on partial write

- **Defect.** `audio_file.cpp:414` writes directly to the output path; a
  partial write leaves a corrupt file masquerading as valid output.
- **Invariant established.** "write_track produces either the complete output
  file or no file — never a partial file at the target path."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `write_track: partial write (disk full simulation) leaves no target
    file` (new, using a constrained filesystem or a hooked ofstream).
- **Exit criteria.**
  - [ ] Uses temp-sibling + `std::filesystem::rename` idiom.
  - [ ] Temp file is cleaned on any error path.
  - [ ] Temp-sibling path generation handles filenames up to `NAME_MAX`
        (255 on POSIX) without losing the random uniqueness component.
        Truncation results in `WriteError`, never a non-unique temp path.
        Regression test uses a ≥50-char target filename and ≥8 concurrent
        threads writing to the same target; assertion: no file at the
        target with size below the WAV header size at any time after the
        threads return. *Audit-1 finding (REJECTED) on the first M-16
        attempt: snprintf into a 64-byte buffer silently truncated the
        random suffix, producing 40 corrupt target files in 6400 calls
        under 32-thread stress with a 54-char filename.*

---

## Tier 3 — Contract unification

### M-14 — Collapse LoadResult and Expected

- **Defect.** Two parallel error-wrappers (`LoadResult<T>`,
  `Expected<T,E>`) with different semantics, including the reinterpret_cast
  UB pattern from C-2.
- **Invariant established.** "The codebase has exactly one error-result
  wrapper. Its value/error accessors have defined behavior on every input."
- **Files touched.** `src/core/audio_file.hpp` (replace Expected),
  `src/core/audio_buffer.hpp` (remove LoadResult), `src/core/audio_buffer.cpp`,
  `src/modes/*.cpp`, `src/tui/app.cpp`, `src/main.cpp`, every test that uses
  either type.
- **Tests added.**
  - `Expected: move from errored; move from valued; value() on error
    aborts` (new).
- **Depends on.** C-2 (the precondition-check version).
- **Exit criteria.**
  - [ ] Backed by `std::variant<T, E>` internally — no reinterpret_cast.
  - [ ] Implicit conversions from T and E are deliberate and documented.
  - [ ] `LoadResult` removed from the tree.

---

## Tier 4 — Parser hardening

### M-3 — WAVE_FORMAT_EXTENSIBLE (0xFFFE) rejected

- **Defect.** `audio_file.cpp:279–286` returns UnsupportedFormat for the
  format tag most modern DAWs emit.
- **Invariant established.** "parse_wav_header accepts 0xFFFE when the
  SubFormat GUID identifies PCM or IEEE-float; all other subtypes return
  UnsupportedFormat."
- **Depends on.** FIXTURE-WAVEEXT.
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM accepted`.
  - `parse_wav_header: extensible with non-PCM/float subformat rejected`.
- **Exit criteria.**
  - [ ] Both previously-failing integration tests
        (`Lossless end-to-end: verify exported file formats`) now pass.
  - [ ] Remove the `[!shouldfail]` tags on the four EXTENSIBLE tests added
        by FIXTURE-WAVEEXT in the same PR.

### M-4 — RF64 data placeholder confuses chunk walker

- **Defect.** `audio_file.cpp:263–317`: chunk_size == 0xFFFFFFFF placeholder
  causes the walker to skip ahead past any subsequent ds64/LIST chunks.
- **Invariant established.** "For RF64 files where ds64 appears after data,
  parse_wav_header still recovers the correct data_size."
- **Depends on.** FIXTURE-RF64.
- **Files touched.** `src/core/audio_file.cpp`, `tests/test_lossless.cpp`.
- **Tests added.**
  - Shared with FIXTURE-RF64's `ds64 after data` case.
- **Exit criteria.**
  - [ ] Two-pass scan, or use ds64's RIFF-size when present to cap the
        walker, or break out of the loop after recognising RF64 + data.
  - [ ] The helper `rf64_read_full_with_tail` in `tests/test_lossless.cpp`
        (introduced by FIXTURE-RF64 as a documented stub returning head
        only) is revised to feed the parser the exact shape the M-4 fix
        expects — head-plus-tail slice, full file, or two-pass scan as
        appropriate. Otherwise the `ds64-after-data` test will silently
        pass by not-actually-exercising the fix. *Raised by invariant-agent
        during Tier 1 fixture audit.*
  - [ ] Remove the `[!shouldfail]` tag on the `ds64-after-data` test in
        the same PR — the absence of that tag after M-4 lands is the
        positive check that M-4 closed its invariant.

### M-5 — AIFF SSND offset field assumed zero

- **Defect.** `audio_file.cpp:376–381` ignores the 4-byte SSND offset field.
- **Invariant established.** "parse_aiff_header honors SSND offset: data
  begins at SSND_body + 8 + offset, data_size is chunk_size - 8 - offset."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `parse_aiff_header: non-zero SSND offset is honored` (new, malformed
    fixture).
- **Exit criteria.**
  - [ ] Read SSND offset; apply to data_offset and data_size.

### Mi-1 — parse_aiff_header returns sample_rate = 0

- **Defect.** Comment says "libsndfile validates later"; function contract is
  violated when called directly.
- **Invariant established.** "parse_aiff_header produces a fully-populated
  AudioInfo matching the file header, or returns InvalidFormat."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `parse_aiff_header: sample_rate decoded from 80-bit float`.
- **Exit criteria.**
  - [ ] Decode the IEEE 80-bit extended sample-rate field (inverse of the
        fixed encode_float80).

---

## Tier 5 — Algorithmic correctness

### M-9 — std::clamp with hi < lo when vinyl is empty

- **Defect.** `reference_mode.cpp:994` clamp upper bound may be -1.
- **Invariant established.** "align_per_track skips tracks against empty
  vinyl rather than invoking std::clamp with invalid bounds."
- **Files touched.** `src/modes/reference_mode.cpp`,
  `tests/test_reference_mode.cpp`.
- **Tests added.**
  - `align_per_track: empty vinyl returns empty offsets, no UB` (new).
- **Exit criteria.**
  - [ ] Guard at top of per-track loop.

### M-10 — compute_zero_crossing_rate divides by zero

- **Defect.** `analysis.cpp:68` when `end - start == 1`.
- **Invariant established.** "ZCR is defined as 0 for frames of length
  less than 2."
- **Files touched.** `src/core/analysis.cpp`, `tests/test_analysis.cpp`.
- **Tests added.**
  - `compute_zero_crossing_rate: single-sample frame returns 0, not NaN`.
- **Exit criteria.** [ ] Guard and test.

### Mi-4 — Naive cross_correlate normalization documentation

- **Defect.** The naive impl uses a global norm factor, which is not Pearson
  NCC per-lag. The docstring doesn't say so.
- **Invariant established.** "The naive `cross_correlate` is a verification
  shim for the FFT implementation; callers treating its peak value as a
  probability are using it wrong."
- **Files touched.** `src/core/correlation.hpp`, `src/core/correlation.cpp`.
- **Tests added.**
  - `cross_correlate and cross_correlate_fft agree on lag` (already in
    test suite; just re-verify after comment).
- **Exit criteria.**
  - [ ] Header docstring notes the normalization difference explicitly.
  - [ ] Consider marking `[[deprecated]]` or `/* testing-only */`.

---

## Tier 6 — API hygiene

### M-6 — score_gap units are ambiguous

- **Defect.** `blind_mode.cpp:57–93` takes sample indices; `detect_gaps`
  returns frame indices. Call sites multiply by hop_length to bridge.
- **Invariant established.** "Sample-index and frame-index types are not
  implicitly convertible."
- **Files touched.** `src/modes/blind_mode.hpp`,
  `src/modes/blind_mode.cpp`, `tests/test_blind_mode.cpp`.
- **Tests added.** Compile-time tests that mixing units fails.
- **Exit criteria.**
  - [ ] `SampleIndex`/`FrameIndex` tagged int types, or at minimum
        unambiguous parameter names + a header comment stating units.

### M-7 — score_gap ignores sample_rate parameter

- **Defect.** Parameter marked `[[maybe_unused]]`.
- **Invariant established.** "Public APIs do not carry dead parameters."
- **Files touched.** `src/modes/blind_mode.hpp`, `src/modes/blind_mode.cpp`,
  `tests/test_blind_mode.cpp` (signature update).
- **Exit criteria.**
  - [ ] Either use sample_rate (spectral-flatness scoring) or remove it.

### M-8 — Blind mode returns error on single-track rips

- **Defect.** `NoGapsFound` is a legitimate outcome, not an error.
- **Invariant established.** "Blind mode returns a single-split result on a
  gap-free input, with confidence reflecting the absence of evidence."
- **Files touched.** `src/modes/blind_mode.cpp`, `src/main.cpp`
  (handling tweak).
- **Tests added.**
  - `analyze_blind_mode: single-track input returns 1 split` (new).
- **Exit criteria.**
  - [ ] No error return on empty `gaps`.

### M-11 — LoadResult default-constructed state is ambiguous

- **Defect.** Default ctor sets error but also default-constructs the value.
- **Invariant established.** "No default construction leaves a result
  wrapper in an ambiguous state."
- **Files touched.** Resolved by M-14.
- **Exit criteria.** Closed as a duplicate of M-14 once M-14 lands.

### Mi-7 — score_gap drops sample_rate

- Duplicate of M-7 / same resolution.

### NEW-BLIND-GAP — Blind mode returns only 1 split on clear 2-track fixture

- **Defect.** Surfaced by Phase 0.5 at `test_integration.cpp:479` and `:762`.
  Blind mode on a clear tone+3s-silence+tone fixture returns only 1 split.
- **Invariant established.** "Blind mode on a clean 2-track fixture with a
  silence ≥ min_gap_seconds returns ≥2 splits."
- **Files touched.** Likely `src/modes/blind_mode.cpp`,
  `src/core/music_detection.cpp`.
- **Tests added.** Already present; fix is to make them pass.
- **Exit criteria.**
  - [ ] Root cause traced (noise-floor estimator? gap detector? threshold?).
  - [ ] Two integration tests (`clear silence detection`, `combined
        workflow`) pass.

---

## Tier 7 — TUI invariants

### Mi-8 — TUI marker nudge breaks start≤end invariant

- **Defect.** `tui/app.cpp:191–204` increments/decrements without bounds.
- **Invariant established.** "For every SplitPoint:
  `0 ≤ start_sample ≤ end_sample ≤ total_samples - 1`."
- **Files touched.** `src/tui/app.cpp`.
- **Tests added.** TUI tests are currently absent; add a headless unit test
  at the state-mutator level.
  - `tui: nudge clamps against neighbor start_sample` (new).
- **Exit criteria.**
  - [ ] Nudge handlers clamp against sibling markers and global limits.

### Mi-9 — TUI view bounds can invert

- **Defect.** `tui/app.cpp:208–241` — view_end < view_start possible.
- **Invariant established.** "0 ≤ view_start < view_end ≤ total_samples."
- **Files touched.** `src/tui/app.cpp`.
- **Tests added.** Same headless-unit-test harness as Mi-8.
  - `tui: view handlers never invert or zero the range`.
- **Exit criteria.**
  - [ ] Post-handler normalization helper.

### Mi-10 — run_tui exit-code documentation

- **Defect.** `tui/app.cpp:271` inverted return value on non-quit exit.
- **Invariant established.** "run_tui returns 0 on normal exit (Q, Ctrl-C);
  non-zero only on initialization failure."
- **Files touched.** `src/tui/app.cpp`.
- **Exit criteria.**
  - [ ] Header docstring restated.
  - [ ] Exit code matches doc.

---

## Tier 8 — Documentation, attribution, hygiene

### M-12 — FFTW3 is dead

- **Defect.** Already resolved in Phase 0.3 (CMake + CI). Close on
  audit-agent verification.

### M-13 — pocketfft attribution

- **Defect.** Vendored pocketfft_hdronly.h has no LICENSE/attribution in-tree.
- **Invariant established.** "Every third-party file in-tree is accompanied
  by attribution satisfying its license."
- **Files touched.** `THIRD_PARTY_LICENSES.md` (new), `README.md`
  (acknowledgments).
- **Exit criteria.**
  - [ ] pocketfft BSD-3 text reproduced; author + URL listed.

### Mi-5 — Magic threshold soup in reference mode

- **Defect.** `reference_mode.cpp:584–585` (and ~12 other sites) have
  unexplained numeric thresholds.
- **Invariant established.** "Every decision threshold is a `constexpr` at
  top of translation unit with a comment citing the observation or corpus
  that produced it."
- **Files touched.** `src/modes/reference_mode.cpp`.
- **Exit criteria.**
  - [ ] No magic numbers remain in the per-track loop bodies.

### DOC-1 — README "sample-accurate" claim reconciliation

- **Defect.** README uses "sample-accurate" in a context where the code
  rounds ±1 native-rate sample (post-C-4 fix).
- **Invariant established.** "Every README claim is either enforced by a
  test or rewritten to match behavior."
- **Files touched.** `README.md`.
- **Exit criteria.**
  - [ ] Claim reworded to match the tolerance guaranteed by C-4's new test.

### DOC-2 — PROJECT_SPEC.md reconciliation

- **Files touched.** `PROJECT_SPEC.md`.
- **Exit criteria.** Spec and CMakeLists.txt agree on warning flags, standard,
  and dependencies.

### DOC-3 — docs/invariants.md living document

- **Invariant established.** "Every invariant named in this backlog has an
  entry in docs/invariants.md citing the enforcement site(s)."
- **Files touched.** `docs/invariants.md` (new).
- **Exit criteria.** File exists, maintained by invariant-agent every 3–5
  completed items.

---

## Tier 9 — Cleanup (Minor, Nit)

### Mi-2 — compute_rms_energy guard order fragility — `src/core/analysis.cpp`.
### Mi-3 — resample_linear divides by zero when sample_rate == 0 — `src/core/audio_buffer.cpp`.
### Mi-6 — `min` identifier shadows std::min — `src/main.cpp`, `src/modes/reference_mode.cpp`.
### Mi-11 — test_deps.cpp is dead — delete or compile.
### Mi-12 — src/core/core.hpp is dead — delete.
### Mi-13 — verbose.hpp g_timer_start is unused — delete.
### Mi-14 — verbose globals not thread-safe — std::atomic<bool> or Logger&.
### Mi-15 — explicit ctors audit on result wrappers — resolved by M-14.
### Mi-16 — encode_float80 NaN/over-/under-flow handling

- Replace the silent `biased_exp` clamp on subnormal/overflow inputs with
  `assert(std::isfinite(value) && value >= 0)` at function entry.
- Either (a) make NaN handling explicit (e.g., dedicated branch that
  encodes a quiet-NaN bit pattern), or (b) update the docstring/comment
  block to say "NaN: undefined output, asserted-against in Debug" — the
  current header comment claims "NaN: encodes as +0" but in Release the
  NaN actually flows through `frexp`, which is unspecified. Fix the
  doc/code mismatch one way or the other. *Audit-2 finding under C-1.*
- Add a unit test that `encode_float80(1e-5000)` and `encode_float80(1e+5000)`
  either reject in Debug or emit a documented bit pattern.
- *Note.* AIFF sample rates 44.1 k–192 k all fit comfortably; this is a
  hardening item, not a correctness bug for the project's actual use case.
### Mi-17 — std::stoll in natural_less can throw — bound digit count.
### Mi-18 — -Wconversion / -Wdouble-promotion / -Wsign-conversion cleanup

Systematic cleanup of the 89 warning-as-error findings the Phase 0.2
harness surfaced. One PR per TU. **Recommended starting TU:**
`src/core/audio_buffer.cpp` — its `-Wdouble-promotion` finding at line 68
is the *first* error a clean Release+`MWAAC_WERROR=ON` build hits, so
fixing it is a precondition for anyone trying to validate that the
quality gate works at all. Audit-2 of C-1 raised this explicitly as a
"PROJECT_SPEC.md says `-Werror` should pass; right now it can't" item.

Per-TU sub-tasks (rough; finalise after the audit_buffer.cpp pass
calibrates effort):

- `src/core/audio_buffer.cpp` (~7 findings — start here).
- `src/core/correlation.cpp` (~12).
- `src/core/analysis.cpp` (~9).
- `src/core/music_detection.cpp` (~5).
- `src/core/audio_file.cpp` (~3 remaining after C-1).
- `src/modes/reference_mode.cpp` (large; may split further).
- `src/modes/blind_mode.cpp`.
- `src/modes/reaper_export.cpp`.
- `src/tui/app.cpp`, `src/tui/waveform.cpp`.
- `src/main.cpp`.

Each of these is a ≤ 30-line diff; one item, one PR, one audit.

### Nits — N-1 through N-~12

The review's Nit list (dead `static` on constexpr magic bytes, `M_PI` →
`std::numbers::pi`, `tui/waveform.cpp:57` over-allocation, etc.) ride along
with their enclosing TU's -Wconversion pass (Mi-18), one commit per TU.

---

## New invariants surfaced during remediation

Items opened by Phase 0:

- **NEW-BLIND-GAP** (above) — blind mode on a clean 2-track fixture returns
  only 1 split.
- **NEW-WAVEEXT-WRITE** — `test_integration.cpp:691`'s
  `export_result.has_value()` fails for a 48kHz 2ch 24-bit file. Likely
  subsumed by M-3 once WAVE_FORMAT_EXTENSIBLE lands, but track separately
  until confirmed.

---

## Deferred / out of scope

- **Property-based tests / fuzzing infrastructure** — planned, but the first
  pass is the structured malformed corpus (FIXTURE-MALFORMED). A real
  libFuzzer harness is a later item.
- **Windows CI** — PROJECT_SPEC.md lists it as optional; not in the
  review's must-fix list; deferred.

---

## Dispatch order

Strict precedence:

1. FIXTURE-REF, FIXTURE-RF64, FIXTURE-WAVEEXT, FIXTURE-MALFORMED
   (test-fixture-agent; these unblock many downstream items).
2. C-1, M-16 (atomic-write), C-2 (precondition check only), M-15
   (call-site guards).
3. M-14 (contract unification, depends on C-2).
4. C-3 (depends on FIXTURE-RF64), M-2.
5. M-3 (depends on FIXTURE-WAVEEXT), M-4, M-5, Mi-1.
6. C-4, M-9, M-10, Mi-4.
7. C-5 (spectral flatness).
8. M-6, M-7, M-8, M-11, NEW-BLIND-GAP.
9. Mi-8, Mi-9, Mi-10.
10. M-12, M-13, Mi-5, DOC-1, DOC-2, DOC-3.
11. Cleanup (Mi-2, Mi-3, Mi-6, Mi-11, Mi-12, Mi-13, Mi-14, Mi-16, Mi-17,
    Mi-18 with Nits folded in).

Within each tier, dispatch items whose file-region scope doesn't overlap in
parallel. Serialize items that touch the same TU.
