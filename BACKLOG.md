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

### C-1 — encode_float80 stack buffer overflow — **RESOLVED in #27 (`0bf13a1`)**

- **Defect.** `encode_float80` writes 11 bytes into a 10-byte `std::byte`
  buffer at `audio_file.cpp:496` (`out[10] = ...`). Buffer declared at
  `:659, :670` as `std::byte float80[10]`.
- **Invariant established.** "encode_float80 writes exactly 10 bytes to its
  output buffer, matching IEEE 754 80-bit extended precision wire format."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added/re-enabled.** Two `[lossless]` AIFF tests already re-enabled
  in F-4 currently crash under ASan. The fix makes them pass.
- **Exit criteria.**
  - [x] ASan reports no stack-buffer-overflow on `test_lossless` under
        `MWAAC_SANITIZE=ON`. *Verified across all asan+ubsan CI runs since PR #27 (`0bf13a1`); `test_lossless` passes under sanitizers; encode_float80 wire-format TEST_CASEs at `tests/test_lossless.cpp:1115+` exercise the cured byte layout.*
  - [x] AIFF mantissa layout matches a reference implementation. *`float80_layout` namespace at `src/core/audio_file.cpp:864-880` references IEEE 754 80-bit / SANE / AIFF spec; `static_assert` at `:880` verifies bias / exp constants.*
  - [x] Re-audit comment on the function explains why 10 bytes is correct. *Comment block at `src/core/audio_file.cpp:895-898` states "Writes *exactly* 10 bytes to `out`... No bytes beyond `out[9]` are touched."*
- **Surfaced inline scope expansion.** AIFF-INLINE-SCOPE — `build_aiff_header`'s `numSampleFrames` field type corrected from float80 to u32 in same PR; recorded in `docs/deviations.md`. Closed as own backlog item ([x] AIFF-INLINE-SCOPE below).

### C-2 — `Expected<T,E>::value()` is UB when holding error — **RESOLVED in #29 (`083431c`)**

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
  - [x] `Expected<T,E>::value()` has a precondition check (`assert` in
        Debug, `std::terminate` otherwise). *`MWAAC_ASSERT_PRECONDITION` macro at `src/core/audio_file.hpp:47-52` (assert in Debug / std::terminate in Release); applied at `:139, :143` in `value()` accessors. M-14 (#31) preserved the macro on the variant-backed implementation.*
  - [x] Every `audio_file.value()` call in `main.cpp` is guarded by
        `if (!audio_file) { ... return 1; }` on first use. *Canonical pattern at `src/main.cpp:144-152` and `:285-295` (`if (!opened) return ...; auto& audio_file = opened.value();`). C-2 audit-2 grep verified 0 unguarded `.value()` calls in main.cpp.*
  - [x] UBSan passes on full test suite. *Verified across all asan+ubsan CI runs since PR #29 (`083431c`); death-test TEST_CASEs at `tests/test_audio_file.cpp:773, 803, 824` plus subprocess test at `:859` (`main: failed AudioFile::open exits cleanly`).*
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

### M-15 — CLI continues after failed AudioFile::open with total_frames=0 — **SUBSUMED BY C-2**

- **Defect.** `main.cpp:253–260` uses `audio_file ? ... : 0` pattern then
  unconditionally writes tracks, producing `sp.end_sample = -1`.
- **Invariant established.** "Every CLI branch that depends on a successful
  AudioFile::open must short-circuit on failure before using the result."
- **Files touched.** `src/main.cpp`.
- **Tests added.**
  - Covered by C-2's `main: failed AudioFile::open exits cleanly` test.
- **Status.** **Subsumed by C-2 (PR #29, commits `717e705` + `c6611c0`).**
  The C-2 fix-agent rewrote all three CLI branches (reference, blind, tui) to
  the canonical `if (!opened) return 1; auto& audio_file = opened.value();`
  pattern, eliminating the conditional-default antipattern that caused
  `total_frames = 0` and downstream `sp.end_sample = -1`. Both C-2 audit
  passes verified this; an independent grep on the C-2 branch found 0
  unguarded `.value()` calls in `main.cpp`. The originally-planned
  M-15 dispatch was therefore skipped — no separate fix-agent or PR
  needed. The new precondition guards from C-2 (`audio_file.hpp` accessors)
  also abort hard if any future caller regresses, providing belt-and-braces
  protection beyond the textual guards in `main.cpp`.
- **Exit criteria.**
  - [x] Single guard immediately after the `open` call. *(Verified on C-2
        branch: each of the three branches in `main.cpp` opens, checks
        `if (!opened)`, and binds `auto& audio_file = opened.value()`.)*
  - [x] No `audio_file.value()` calls in main.cpp that aren't preceded by a
        validated guard. *(Verified by grep on C-2 branch: 0 unguarded
        `.value()` calls remain.)*

### M-16 — write_track is not atomic on partial write — **RESOLVED in #28 (`82a774a`)**

- **Defect.** `audio_file.cpp:414` writes directly to the output path; a
  partial write leaves a corrupt file masquerading as valid output.
- **Invariant established.** "write_track produces either the complete output
  file or no file — never a partial file at the target path."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `write_track: partial write (disk full simulation) leaves no target
    file` (new, using a constrained filesystem or a hooked ofstream).
- **Exit criteria.**
  - [x] Uses temp-sibling + `std::filesystem::rename` idiom. *`make_temp_sibling_path` helper at `src/core/audio_file.cpp:801`; `std::filesystem::rename` at `:1357`.*
  - [x] Temp file is cleaned on any error path. *Verified by TEST_CASEs `write_track: failure leaves no file at output path (parent dir missing)` at `tests/test_lossless.cpp:1262` and `(target is a directory)` at `:1302`.*
  - [x] Temp-sibling path generation handles filenames up to `NAME_MAX`
        (255 on POSIX) without losing the random uniqueness component.
        Truncation results in `WriteError`, never a non-unique temp path. *NAME_MAX handling at `src/core/audio_file.cpp:822-827` (`constexpr NAME_MAX_BYTES = 255`) with explicit `WriteError` refusal at `:1310-1313`. Regression tests: `write_track: long-filename concurrent writes do not collide` at `tests/test_lossless.cpp:1363` (≥50-char filename + concurrent threads) and `write_track: target filename longer than NAME_MAX returns WriteError` at `:1453`. *Audit-1 finding (REJECTED) on the first M-16 attempt: snprintf into a 64-byte buffer silently truncated the random suffix, producing 40 corrupt target files in 6400 calls under 32-thread stress with a 54-char filename — cured before merge.*

---

## Tier 3 — Contract unification

### M-14 — Collapse LoadResult and Expected — **RESOLVED in #31 (`f052f89`)**

- **Status.** RESOLVED 2026-04-26. PR #31 cure-path (a): `Expected<T,E>`
  rewritten as `std::variant<T,E>`; `LoadResult` removed; `AudioError`
  gained `ResampleError` so the unified taxonomy is a strict superset of
  the previous load-specific values. Audit (APPROVED) verified all 5
  exit criteria + sanitizer-clean Expected access paths + C-2/M-16
  regression-by-name. See merge `f052f89`.
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
  - [x] Backed by `std::variant<T, E>` internally — no reinterpret_cast.
        *Alternative acceptable only if explicitly justified:* keep
        placement-new layout but insert `std::launder` at every accessor
        to cure the latent `[basic.life]/8` UB. Whichever path M-14
        chooses, **the latent UB must be cured in this PR — no further
        deferral.** *Audit-2 of C-2 finding (F-AUDIT2-4): the C-2 fix
        narrows the behavioural hazard but the standard-conformance
        hazard persists; M-14 is its terminal scope.*
  - [x] Implicit conversions from T and E are deliberate and documented.
  - [x] `LoadResult` removed from the tree.
  - [x] `Expected`'s contract docstring states its **thread-safety**
        semantics explicitly: "single-threaded contract; check + access
        must occur on the same thread; concurrent mutation invalidates
        the precondition's TOCTOU window." *Audit-2 of C-2 finding
        (F-AUDIT2-2).*
  - [x] **Move-construction / move-assignment behaviour documented.**
        Audit-2 of C-2 confirmed: `Expected(Expected&& other)` does
        not flip `other.has_value_`, so moved-from `Expected` is still
        considered "valid" and `value()` returns a moved-from `T`. This
        is consistent with `std::optional` / `std::expected`; the M-14
        contract docstring should make it explicit so callers don't
        rely on moved-from `Expected` aborting.

---

## Tier 4 — Parser hardening

### M-3 — WAVE_FORMAT_EXTENSIBLE (0xFFFE) rejected — **RESOLVED in #34 (`70a7745`)**

- **Status.** RESOLVED 2026-04-29. Parser now accepts 0xFFFE when the SubFormat
  GUID identifies PCM (`KSDATAFORMAT_SUBTYPE_PCM`) or IEEE-float
  (`KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`); other SubFormats return
  `UnsupportedFormat`; truncated EXTENSIBLE structures return `InvalidFormat`
  per the local-view rule in `docs/decisions/parser-errors.md`. Four
  `[!shouldfail]` tags lifted in the same PR. Audit APPROVED with no
  must-fix; GUID byte layout verified against Microsoft's canonical
  `mmreg.h`.
- **Note on the original "previously-failing integration tests" exit
  criterion.** The original criterion claimed `Lossless end-to-end: verify
  exported file formats` (`tests/test_integration.cpp:728`) would now pass.
  M-3's fix-agent surfaced that the attribution was wrong — that test fails
  for a test-side arithmetic bug, not a WAVE_FORMAT_EXTENSIBLE parser
  defect. The cure for `:728` is tracked separately as
  `INT-728-FIXTURE-MISMATCH` (below). See `docs/known-failing-tests.md` for
  the audit-pass-discipline note on the 4th catch (first on the
  cure-attribution axis).
- **Defect.** `audio_file.cpp:279–286` returned UnsupportedFormat for the
  format tag most modern DAWs emit. (Line range was stale at dispatch; the
  actual return was at `:302` — not material to the cure.)
- **Invariant established.** "parse_wav_header accepts 0xFFFE when the
  SubFormat GUID identifies PCM or IEEE-float; all other subtypes return
  UnsupportedFormat."
- **Depends on.** FIXTURE-WAVEEXT.
- **Files touched.** `src/core/audio_file.cpp`, `tests/test_audio_file.cpp`,
  `tests/test_lossless.cpp`.
- **Tests added.**
  - `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM accepted` (landed via
    FIXTURE-WAVEEXT, untagged `[!shouldfail]` by M-3).
  - `parse_wav_header: extensible with non-PCM/float subformat rejected`
    (same).
- **Exit criteria.**
  - [x] WAVE_FORMAT_EXTENSIBLE with PCM/IEEE-float SubFormat accepted;
        unknown SubFormat returns `UnsupportedFormat`.
  - [x] Truncated EXTENSIBLE structure returns `InvalidFormat` (parser-errors.md
        local-view rule).
  - [x] The four `[!shouldfail]` tags on the EXTENSIBLE tests
        (`tests/test_audio_file.cpp:151, 168, 185`,
        `tests/test_lossless.cpp:434`) removed in the same PR; tests now pass
        under their normal assertions.
  - [~] Original criterion "the previously-failing integration test
        `Lossless end-to-end: verify exported file formats` now passes" was
        **based on an incorrect cure attribution** — that test was never
        going to be cured by M-3. Re-attributed to `INT-728-FIXTURE-MISMATCH`.

### INT-728-FIXTURE-MISMATCH — `Lossless end-to-end` integration test fails for test-side reason, not parser — **RESOLVED in `3a86871` via option (c)**

- **Decision (recorded 2026-04-30).** Option (c): drop the TEST_CASE.
  Coverage is fully subsumed by `tests/test_lossless.cpp:416–474`
  (`"Lossless: 24-bit 2-ch extensible WAV round-trip preserves bytes"`),
  which checks every metadata dimension `:728` checked plus
  `bits_per_sample`, `frames`, mid-file offset arithmetic, and data-byte
  identity. Plain-PCM bit-depth round-trip is independently in
  `tests/test_lossless.cpp:223` (24-bit byte-identity) and `:357`
  (16/24/32). The dropped TEST_CASE name (`"verify exported file
  formats"`) was always broader than the body (single fixture, plain
  PCM, no format-identity assertion). Subsumption verified line-by-line
  by independent coverage-audit agent and user before merge.
- **Why not (a) or (b).** (a) would have left the TEST_CASE name lying
  about what it tested. (b) required filing a write-side EXTENSIBLE item
  (`M-3-EMIT`) that does not yet exist; deferring (c) on speculation
  about a future item was the wrong cost/benefit.
- **Doc-drift note.** This BACKLOG entry listed three options
  (a, b, and the "third path" of dropping); `docs/known-failing-tests.md`
  listed only (a) and (b). The drift was caught at decision time and is
  recorded as `KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1` in
  `docs/deviations.md`. The orchestrator playbook now requires a
  cross-doc reconciliation pass at decision time for any item
  referenced in multiple governance docs.
- **Defect (historical).** `tests/test_integration.cpp` had a local
  `create_test_wav` overload at `:101` whose 6th parameter was an
  optional `audio_data` vector. The call at `:710` passed 48000 floats
  for a 2-channel / 48000-frame request. libsndfile interpreted 48000
  floats as 24000 stereo frames, so the source file had
  `info.frames = 24000`. `write_track(..., 0, 47999)` then tripped
  `end_sample (47999) >= info.frames (24000)` and returned
  `AudioError::InvalidRange`. The source file was
  `SF_FORMAT_WAV | SF_FORMAT_PCM_24` — plain PCM, not
  WAVE_FORMAT_EXTENSIBLE.
- **Origin.** Surfaced by M-3's fix-agent (PR #34) during
  validation-gate verification. Previously misattributed to
  NEW-WAVEEXT-WRITE / M-3 in `docs/known-failing-tests.md`; corrected at
  orchestrator paperwork alongside M-3's merge.
- **Files touched at resolution.** `tests/test_integration.cpp` only
  (the helper at `:101` stayed because seven other TEST_CASEs call it).
- **Exit criteria.**
  - [x] Architectural option chosen and recorded in
        `docs/deviations.md` (`KNOWN-FAILING-VS-BACKLOG-OPTION-DRIFT-V1`).
  - [x] `test_integration:728` removed; `docs/known-failing-tests.md`
        entry moved to Resolved with merging commit `3a86871`.

### M-4 — RF64 data placeholder confuses chunk walker — **RESOLVED in #35 (`039347e`)**

- **Defect.** `audio_file.cpp:263–317`: chunk_size == 0xFFFFFFFF placeholder
  causes the walker to skip ahead past any subsequent ds64/LIST chunks.
- **Invariant established.** "For RF64 files where ds64 appears after data,
  parse_wav_header still recovers the correct data_size."
- **Depends on.** FIXTURE-RF64.
- **Files touched.** `src/core/audio_file.cpp`, `tests/test_lossless.cpp`.
- **Tests added.**
  - Shared with FIXTURE-RF64's `ds64 after data` case.
- **Exit criteria.**
  - [x] Two-pass scan, or use ds64's RIFF-size when present to cap the
        walker, or break out of the loop after recognising RF64 + data.
        Chosen: two-pass scan with `AudioFile::open` head+tail splice
        (head 64 KiB + last 1 MiB). Walker early-breaks on the
        0xFFFFFFFF data placeholder; pass 2 scans the spliced buffer
        for `ds64`.
  - [x] The helper `rf64_read_full_with_tail` in `tests/test_lossless.cpp`
        revised to return head + last 1 MiB, matching the production
        splice shape (independently transcribed; audit-2 flagged the
        copy-paste-vs-shared-helper trade as moderate signal).
  - [x] Remove the `[!shouldfail]` tag on the `ds64-after-data` test
        (`tests/test_lossless.cpp:744` pre-merge). Tag absent post-merge.

### M-4-FU-TAILSCAN — RF64 tail-scan false-match window narrowing — **RESOLVED in #38 (`e4c572a`)**

- **Defect.** Post-M-4 tail-scan in `parse_wav_header` starts at
  `data_chunk_payload_start` (inside the head 64 KiB), exposing ~64 KiB
  of head sample bytes to false-match against the `ds64` fourcc plus a
  syntactically valid 24-byte trailer. Probability vanishingly small
  (~1 in 4k across a typical RF64 file's spliced bytes per audit-2
  estimate), but the scan window is wider than necessary — the
  legitimate ds64-after-data trailer can only sit in the spliced tail,
  not the head.
- **Origin.** Audit-1 and audit-2 both flagged the false-match risk;
  audit-2 named the mechanical cure ("narrow scan window further, e.g.
  last 1 MiB"). Filed as own backlog item per
  one-item-one-PR-one-audit treatment.
- **Invariant established.** "Tail-scan for ds64 in RF64 inputs only
  examines bytes outside the head window; head-window byte patterns
  cannot trigger a false ds64 match."
- **Files touched.** `src/core/audio_file.cpp` (one-line change at
  `parse_wav_header` tail-scan loop start; replace
  `data_chunk_payload_start` with the head/tail boundary computed from
  the splice metadata).
- **Tests added.**
  - Targeted regression: an RF64 fixture whose head 64 KiB sample bytes
    contain the literal `ds64` fourcc + 24 plausible bytes that pass
    the chunk_size bounds check. Pre-fix: parser silently accepts the
    in-head false ds64 and reports a wrong `data_size`. Post-fix:
    parser ignores the in-head bytes and either resolves to
    `InvalidFormat` (no real ds64 in tail) or to the correct
    `data_size` (real ds64 in tail).
- **Exit criteria.**
  - [x] Tail-scan starts at `kHeadSize` (file-scope `static constexpr` per cure shape (γ)), not `data_chunk_payload_start`. *Implemented at `src/core/audio_file.cpp:71` (lifted constant) + `:593` (loop start).*
  - [x] New regression TEST_CASE exercises the in-head false-match path. *`parse_wav_header: in-head ds64-shaped sample bytes are not false-matched by tail-scan` in `tests/test_lossless.cpp` — inline 128-byte buffer with planted false `ds64` fourcc + 24-byte trailer; pre-fix returns wrong `data_size`, post-fix returns `InvalidFormat`.*

### M-4-FU-COVERAGE — RF64 ds64-after-data via `AudioFile::open` splice path — **RESOLVED via redirect in #39 (`0c1a9cf`); production-pipeline gap re-attributed to M-4-FU-LIBSNDFILE-GATE**

- **Defect (coverage gap, not a code defect).** The cure-attribution
  test for M-4 (`tests/test_lossless.cpp` `"parse_wav_header: RF64 with
  ds64 after data"`) calls `parse_wav_header` directly via the
  `rf64_read_full_with_tail` helper. The production path
  (`AudioFile::open`'s head+tail splice → `parse_wav_header`) is not
  directly tested for the ds64-after-data case. The independently-
  transcribed helper and `AudioFile::open` could diverge silently, and
  the helper-level test would not catch it.
- **Origin.** Audit-1 surfaced as a non-blocking coverage suggestion at
  M-4 close. Test-scaffold-co-evolution risk per audit-2: same logic
  appears in both production (`audio_file.cpp:203-241`) and test helper
  (`tests/test_lossless.cpp:579-613`), independently implemented but
  algorithmically identical.
- **Invariant established.** "An RF64 file with ds64-after-data, opened
  via `AudioFile::open` (production read path), exposes the correct
  `data_offset` and `data_size` through the `AudioInfo` returned by the
  AudioFile."
- **Files touched.** `tests/test_lossless.cpp` (new TEST_CASE; no
  production code change expected).
- **Tests added.**
  - `AudioFile::open: RF64 with ds64 after data exposes correct
    data_size` — opens `rf64_ds64_after.wav` via `AudioFile::open`
    (not via the test helper), reads `audio_file.value().info()`, and
    asserts `data_offset` / `data_size` against the manifest.
- **Resolution note.** The original filing predicted a helper-vs-splice co-evolution risk (audit-2 of M-4). When the COVERAGE fix-agent drafted the TEST_CASE to mandate, it failed on main with `ReadError` from `AudioFile::open`'s libsndfile cross-validation gate — surfacing a different gap-family (libsndfile-gate axis missed by M-4 audits). Re-attributed to **M-4-FU-LIBSNDFILE-GATE** (filed below). PR #39 landed the drafted TEST_CASE under that attribution with `[!shouldfail]` per C-3 precedent; PR #40 (LIBSNDFILE-GATE production fix) un-tagged it atomic with the cure. The TEST_CASE now serves both cure-attribution roles: libsndfile-fallback regression for LIBSNDFILE-GATE, and the helper-vs-splice co-evolution check originally predicted by COVERAGE.
- **Exit criteria.**
  - [x] New TEST_CASE exists and asserts manifest values. *`AudioFile::open: RF64 with ds64 after data exposes correct data_size` in `tests/test_lossless.cpp` (un-tagged as of #40).*
  - [x] If `AudioFile::open`'s splice ever diverges from the helper, the new TEST_CASE catches it before the helper-only test does. *Production-path direct via `AudioFile::open`; helper-direct test at `tests/test_lossless.cpp` `parse_wav_header: RF64 with ds64 after data` unchanged.*

### M-4-FU-LIBSNDFILE-GATE — `AudioFile::open` libsndfile cross-validation discards parser-recovered AudioInfo for RF64 ds64-after-data — **RESOLVED in #40 (`65200b9`)**

- **Defect.** `AudioFile::open` runs two validation steps in sequence: (1) head+tail splice → `parse_wav_header` (M-4-cured), and (2) `sf_open` libsndfile cross-validation at `src/core/audio_file.cpp:343-347`. Libsndfile 1.2.2 returns "Unspecified internal error" on RF64 ds64-after-data files (verified by direct probe on `tests/fixtures/rf64/rf64_ds64_after.wav`; the ds64-before-data fixture is accepted). `AudioFile::open` then returns `AudioError::ReadError` at `:345`, **discarding the parser's recovered `AudioInfo`** (including the M-4-cured `data_offset` / `data_size`) before it reaches the caller. M-4's parser-scoped invariant (INV-RF64-2) holds; the production-pipeline-scoped invariant does not.
- **Origin.** Surfaced by M-4-FU-COVERAGE fix-agent at pre-PR halt-and-surface (mandate-explicit halt clause: "new test FAILS on main" → governance escalation). Audit-1 and audit-2 of M-4 swept the helper-vs-splice axis (closed by M-4-FU-COVERAGE filing) and the post-cure tail-scan window axis (closed by M-4-FU-TAILSCAN filing). The libsndfile-gate axis was not swept until COVERAGE fix-agent's halt caught it. Same gap-family as `KNOWN-FAILING-CURE-ATTRIBUTION-V1` (axis-coverage gap on M-4 close-out paperwork).
- **Invariant established.** "When `parse_wav_header` recovers `data_offset` / `data_size` for an RF64 file but libsndfile rejects the file at `sf_open`, `AudioFile::open` returns the parser's recovered `AudioInfo` rather than discarding it. `info.frames` is derived from `info.data_size / info.bytes_per_frame()`."
- **Cure path (orchestrator + user blessed).** Path (a1) — try libsndfile; on `format == AudioFormat::RF64 && !sf`, fall back to parser truth. Skip the libsndfile-success override block; derive `info.frames = info.data_size / info.bytes_per_frame()`; default `info.subtype` ("PCM" derived from `bits_per_sample`). Non-RF64 libsndfile-failure path unchanged (still `ReadError`). RF64 libsndfile-success path unchanged (still overrides). Estimated ~10–20 lines diff in `audio_file.cpp`.
- **Files touched.** `src/core/audio_file.cpp` (cross-validation block at `:341-356`); `tests/test_lossless.cpp` (un-tag the redirected M-4-FU-COVERAGE TEST_CASE's `[!shouldfail]` tag, atomic with the production fix per (α) — cycle precedent: M-3 four un-tags + M-4 `:744` un-tag, both atomic with cure).
- **Tests added.**
  - The redirected M-4-FU-COVERAGE TEST_CASE (`[!shouldfail]`-tagged in M-4-FU-COVERAGE's PR, attribution citing this item) un-tags in this PR and passes via strict assertion. Becomes both the libsndfile-fallback regression and the cure-attribution test.
  - Optionally (fix-agent's call): a separate TEST_CASE asserting `info.frames` is correctly derived from `data_size` when libsndfile fails, if the COVERAGE redirect's existing assertions don't already cover it.
- **Audit shape.** Single Tier 4 default. Item is not UB-class.
- **Exit criteria.**
  - [x] `AudioFile::open` does not return `ReadError` when `format == AudioFormat::RF64 && parse_wav_header succeeded && sf_open failed`; instead returns the parser's `AudioInfo` with `info.frames` derived. *Implemented at `src/core/audio_file.cpp:344-388` (cross-validation block restructured for RF64 fallback).*
  - [x] Non-RF64 libsndfile-failure still returns `ReadError`. *Verified at `:354`.*
  - [x] RF64 libsndfile-success still uses libsndfile's overrides. *Verified in the `else` arm at `:368-388`.*
  - [x] Redirected M-4-FU-COVERAGE TEST_CASE's `[!shouldfail]` tag removed in the same PR (atomic with the production fix); test passes via strict assertion. *Verified across all 5 test-running CI jobs (Linux Debug + Release, sanitizers, macOS Debug + Release).*

### M-5 — AIFF SSND offset field assumed zero — **RESOLVED in #36 (`a1654a1`)**

- **Defect.** `parse_aiff_header` SSND-handling block (post-M-4 line numbers
  `src/core/audio_file.cpp:587-595`; original mandate cited `:376-381`)
  ignored the 4-byte SSND offset field.
- **Invariant established.** "parse_aiff_header honors SSND offset: data
  begins at SSND_body + 8 + offset, data_size is chunk_size - 8 - offset."
- **Files touched.** `src/core/audio_file.cpp`,
  `tests/fixtures/malformed/manifest.txt` (drop `-pending-M-5` suffix on
  `aiff_ssnd_offset_nonzero.aiff` entry), `tests/test_audio_file.cpp` (new
  TEST_CASEs for the success-path + offset=0 byte-identity guard).
- **Tests added.**
  - `non-zero SSND offset is honored` — inline-synthesized AIFF with
    offset=4, chunk_size=28; asserts post-M-5 `data_offset=58, data_size=16`
    against pre-M-5 `(54, 20)`.
  - `zero SSND offset is byte-identical to pre-M-5` — regression guard for
    the C-1 round-trip surface.
  - The strict FIXTURE-MALFORMED TEST_CASE now picks up
    `aiff_ssnd_offset_nonzero.aiff` (post-M-5: offset=16 > chunk_size-8=4 →
    `InvalidFormat` via local-view rule).
- **Exit criteria.**
  - [x] Read SSND offset; apply to data_offset and data_size.
  - [x] OOB rejection: `ssnd_offset > chunk_size - 8 → InvalidFormat`
        (parser-errors.md local-view rule).

### Mi-1 — parse_aiff_header returns incomplete AudioInfo (sample_rate=0, bits_per_sample at wrong COMM offset) — **RESOLVED in #37 (`b1e9edd`)**

- **Defect.** Two parser-side defects in `parse_aiff_header`'s COMM
  handling, both in violation of the stated invariant:
  1. `sample_rate` is never decoded from the 80-bit float field; comment
     says "libsndfile validates later", but the function contract is
     violated when called directly (it returns 0).
  2. `bits_per_sample` is read from the wrong COMM-body offset.
     `src/core/audio_file.cpp:580` reads at `chunk_offset + 18`, but per
     AIFF 1.3 the COMM body layout is: numChannels(2) + numSampleFrames(4)
     + sampleSize(2) + sampleRate(float80,10), so bits live at
     `chunk_offset + 14`, not `+18`. The current `+18` reads bytes 2-3 of
     the float80 sampleRate slot. Likely stale code from when the writer
     side incorrectly emitted numSampleFrames as a 10-byte float80 (the
     C-1 / AIFF-INLINE-SCOPE bug); the writer was fixed but the reader
     was never updated. Production has not surfaced this because **no
     existing test goes through `AudioFile::open` on an AIFF file and
     asserts `bits_per_sample`** — the AIFF round-trip tests in
     `tests/test_lossless.cpp` either use `build_aiff_header` directly
     plus libsndfile-readback or assert structural fields only, never
     exercising parse_aiff_header → AudioFile.info().bits_per_sample.
     `AudioFile::open` only overrides `sample_rate`, `channels`,
     `frames`, and `format` from libsndfile (`src/core/audio_file.cpp:
     262-269`); `bits_per_sample` is *not* in that override list, so
     once a test does exercise the path, the wrong value will surface.
     Surfaced by M-5 audit (2026-04-30) when the new SSND-offset
     success-path test attempted to assert `bits_per_sample == 16` on
     an inline AIFF and would have failed; the M-5 test commented out
     that assertion as Mi-1 territory and proceeded. (M-5 paperwork
     entry initially stated "libsndfile cross-validates and overrides
     bits_per_sample"; that was wrong — corrected at Mi-1 pre-dispatch
     verification per the verify-scope-claims rule.)
- **Invariant established.** "parse_aiff_header produces a fully-populated
  AudioInfo matching the file header, or returns InvalidFormat. Every
  COMM field — channels, numSampleFrames, sampleSize, sampleRate — is
  decoded at the correct byte offset."
- **Files touched.** `src/core/audio_file.cpp`.
- **Tests added.**
  - `parse_aiff_header: sample_rate decoded from 80-bit float`.
  - `parse_aiff_header: bits_per_sample decoded from correct COMM
    offset` (new, raised by M-5 audit).
- **Exit criteria.**
  - [x] Decode the IEEE 80-bit extended sample-rate field. New
        `decode_float80_to_u64` / `decode_float80_to_u32` static
        helpers in `src/core/audio_file.cpp`, inverse of the existing
        `encode_float80`. Reject NaN/Inf/negative/subnormal/non-integer
        /over-INT32_MAX → `InvalidFormat` per parser-errors.md
        local-view rule.
  - [x] Read `bits_per_sample` from `chunk_offset + 14`, not
        `chunk_offset + 18`.
  - [x] New regression tests assert both fields against inline
        AIFFs built via `build_aiff_header` (genuine
        encoder/decoder round-trip, not parallel encoder); 28
        assertions across the 6 PROJECT_SPEC sample rates and four
        bit depths.

### AIFF-INLINE-SCOPE — confirm `build_aiff_header` `numSampleFrames` fix has no other-caller fallout — **STATUS: [x] closed (work done; follow-up audit done)**

- **Origin.** Promoted from `docs/deviations.md` ("C-1 inline scope
  expansion: `build_aiff_header` `numSampleFrames` field type"). The deviation
  was filed because C-1's stated scope was `encode_float80`'s buffer overrun,
  but the fix-agent corrected `build_aiff_header`'s `numSampleFrames` field
  type (10-byte float80 → 4-byte big-endian u32 per AIFF 1.3 spec) inline.
  Both C-1 audit passes evaluated and accepted the inline scope expansion;
  audit-2 verified `write_track` (`src/core/audio_file.cpp:816`) is the sole
  production caller of `build_aiff_header` and that no path was previously
  emitting an AIFF that downstream tools accepted.
- **Owner-epic.** **Tier 4 — Parser hardening (AIFF write path).** The AIFF
  write path is otherwise touched only by M-5 (SSND offset, parser-side) and
  Mi-1 (sample-rate decode, parser-side), so this item is the only formal
  emit-side AIFF entry. Treat the AIFF write contract as owned by this item
  group going forward.
- **Defect (historical).** `build_aiff_header` declared `comm_size = 18`
  (which arithmetic balances only when `numSampleFrames` is u32: 2 channels
  + 4 numSampleFrames + 2 bits + 10 sampleRate = 18), but emitted the
  `numSampleFrames` field as a 10-byte `encode_float80` value, producing an
  internally-inconsistent COMM chunk. libsndfile and every standards-conformant
  AIFF reader rejected the resulting file.
- **Invariant established.** "Every AIFF file emitted by `write_track` is
  accepted by libsndfile and parses with the same `numSampleFrames` /
  `sampleRate` / `numChannels` / `sampleSize` values it was constructed with,
  for all six PROJECT_SPEC sample rates (44.1, 48, 88.2, 96, 176.4, 192 kHz)."
- **Files touched.** `src/core/audio_file.cpp` (already corrected at
  C-1 PR #27).
- **Tests added.** Six-rate AIFF round-trip test in `tests/test_lossless.cpp`
  (added by C-1; `tests/test_lossless.cpp:349–360`).
- **Audit verdict.** Both C-1 audit passes APPROVED-WITH-FOLLOWUP; audit-2
  also confirmed no other production caller depends on the previous
  (broken) layout. No further audit work outstanding for this item.
- **Phase 4 reconciliation note.** Cite this item by name in Phase 4 reports
  alongside C-1; the AIFF emit-path hardening is not buried in the C-1 PR
  description or deviations log.

---

## Tier 5 — Algorithmic correctness

### DRIFT-MODEL-RATE-TRUNCATION — Rate-conversion truncation in DriftModel::ref_to_vinyl_sample

- **Origin.** Adjacent-entry sweep during C-4 pre-dispatch checklist
  (2026-05-04). Same physical-quantity defect class as C-4 (analysis-rate
  ↔ native-rate sample-index conversion) but in a dormant code path
  that BACKLOG's C-4 scope did not cover. Filed as own Tier 5 item per
  one-item-one-PR pattern; cure mechanism uses C-4's rounding helper at
  two of the three sites.
- **Defect.** `src/core/drift_model.cpp:10, 16, 33` —
  `DriftModel::ref_to_vinyl_sample` converts analysis-rate sample
  indices to native-rate via truncation, introducing the same up-to-
  ~9-sample error at 192 kHz that C-4 cures in `reference_mode.cpp`.
  Two distinct mechanical defect surfaces:
  - Lines 10, 16: pure integer-division truncation
    (`ref_sample * native_sr / analysis_sr` with integral operands;
    the `static_cast<int64_t>` is redundant). Cure: route through
    C-4's integer-arithmetic rounding helper.
  - Line 33: float→int truncation (`vinyl_sample` is `double`
    post-polynomial-evaluation at line 32; the cast to `int64_t`
    truncates toward zero). Cure: `std::llround` on the double
    product, or pre-round `vinyl_sample` to `int64_t` and route
    through the integer helper.
- **Dormancy.** `DriftModel::ref_to_vinyl_sample` has no production
  callers as of 2026-05-04 (`grep -rn ref_to_vinyl_sample src/`
  shows zero hits outside its definition). `DriftModel` itself is
  referenced only as `std::optional<DriftModel>` at
  `src/core/alignment_result.hpp:26`. Defect is latent. Cured here to
  prevent activation of `DriftModel` in any future epic from
  reintroducing the C-4 defect class.
- **Invariant established.** "All analysis-rate ↔ native-rate
  sample-index conversions in `src/core/drift_model.cpp` round, not
  truncate, matching the tolerance C-4 establishes for
  `reference_mode.cpp`."
- **Depends on.** C-4 (the rounding helper must exist before this
  dispatches; sequenced after C-4 audit-2 close).
- **Files touched.** `src/core/drift_model.cpp`,
  `tests/test_drift_model.cpp` (new file — no drift-model tests
  exist on main as of filing).
- **Tests added.**
  - `DriftModel::ref_to_vinyl_sample: empty-segment fast path rounds,
    not truncates` (new; exercises line 10 — integer-arithmetic site).
  - `DriftModel::ref_to_vinyl_sample: max_pos == 0 path rounds, not
    truncates` (new; exercises line 16 — integer-arithmetic site).
  - `DriftModel::ref_to_vinyl_sample: polynomial-applied path rounds,
    not truncates` (new; exercises line 33 — float→int site, with
    a non-zero polynomial offset to engage the line-32 path).
- **Tier rationale.** Tier 5 (Algorithmic correctness): same axis as
  C-4. Filed in Tier 5 rather than deferred to avoid carrying as a
  vestigial finding into Tier 6 (per
  `feedback_close_followups_before_next_epic.md`).
- **Effort.** ≤ 30 lines of code in `drift_model.cpp` + ≤ 60 lines
  of unit-test code (new test file). One PR, one audit.
- **Exit criteria.**
  - [ ] All three sites at `src/core/drift_model.cpp:10, 16, 33` no
        longer truncate. Lines 10 and 16 use C-4's integer-arithmetic
        helper directly; line 33 uses `std::llround` or routes through
        the integer helper after explicit pre-rounding of
        `vinyl_sample` to `int64_t`.
  - [ ] Three new unit tests exercising each site with canonical
        inputs (e.g. `(native_sr=192000, analysis_sr=44100)` and
        `ref_sample` chosen so rounding and truncation produce
        different `int64_t` outputs); assertions are exact-match on
        the rounded output, not tolerance windows.
  - [ ] No new integer-division or float→int truncation paths
        introduced in `src/core/drift_model.cpp`.
  - [ ] Cure mechanism at lines 10, 16 is the same helper C-4 lifts
        (no duplicate helper). Line 33's cure is documented in the
        PR body if it diverges from the helper's signature.

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

### M-REF-ALIGN-UNIT — un-SKIP per-track alignment unit test against landed fixture

- **Origin.** Surfaced during the PR #23 (FIXTURE-REF) rebase audit.
  `tests/test_reference_mode.cpp:14` is a `SKIP()` whose comment says
  *"TODO(test-fixtures): FIXTURE-REF — synthetic vinyl rip with known
  track boundaries is not yet in tests/fixtures/. Will assert that
  align_per_track lands each track within ±N samples of truth."* The
  fixture now exists (delivered by PR #23), but the test body was never
  written. PR #23's BACKLOG scope covers only the three
  `[integration][reference]` cases in `test_integration.cpp`, not this
  unit-level case.
- **Defect.** The unit-level invariant — that `align_per_track` lands
  each track within a named tolerance of ground-truth on the synthetic
  fixture — has no test asserting it. The integration tests assert
  end-to-end behavior; this case isolates the alignment algorithm
  itself, which is a different surface (one passes the integration
  case but not this one if alignment-precision regresses while gap
  detection still works).
- **Invariant established.** "`align_per_track`'s per-track offsets
  land within ±`kRefFixtureToleranceSamples` of the ground-truth
  boundary recorded in the fixture's manifest, for every track in
  `tests/fixtures/ref_v1/refs/`."
- **Files touched.** `tests/test_reference_mode.cpp` (replace SKIP
  body), `tests/CMakeLists.txt` (wire `MWAAC_REF_FIXTURE_V1_DIR`
  through to `test_reference_mode` if not already there).
- **Tests added.** Replace `tests/test_reference_mode.cpp:14` SKIP
  with a real assertion calling `align_per_track` directly, comparing
  against `tests/fixtures/ref_v1/manifest.txt`'s ground-truth
  start samples.
- **Tier rationale.** Tier 5 (Algorithmic correctness): asserts an
  algorithmic precision invariant on `align_per_track`, which is a
  unit-level concern distinct from the pipeline-level integration
  assertions PR #23 already covers.
- **Out of overlap.** Distinct from C-4 (analysis-rate ↔ native-rate
  coordinate-conversion truncation). C-4 is at the conversion layer;
  this is at the alignment-algorithm layer. They are orthogonal —
  neither subsumes the other.
- **Effort.** ≤ 30 lines of test code plus possible CMake plumbing.
  One PR, one audit, no fixture work needed (already landed).
- **Exit criteria.**
  - [ ] `test_reference_mode.cpp:14`'s SKIP replaced with a real
        assertion against the fixture manifest.
  - [ ] Tolerance constant is named (`kRefFixtureToleranceSamples`
        or similar) and matches PR #23's integration-test tolerance
        (consistency check).
  - [ ] `tests/test_reference_mode.cpp:14` SKIP un-tagged; un-SKIP'd
        case passes under the FIXTURE-REF v1 manifest. Mi-17
        independently un-tags `:20`. Binary exit-code flip is C-4's
        cure-signal, not this item's — see
        `docs/known-failing-tests.md` for the cure-attribution split
        across the binary-exit-code axis (C-4) and the SKIP-cluster
        axis (M-REF-ALIGN-UNIT + Mi-17).

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
### Mi-3 — resample_linear divides by zero when sample_rate == 0 — `src/core/audio_buffer.cpp`. — **RESOLVED in #33 (`8af5793`)** via structural cure (deviation from original "early return {}" spec; see `docs/deviations.md` Mi-3 entry for reasoning).
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
### Mi-17 — std::stoll in natural_less can throw — bound digit count + un-SKIP natural-sort unit test

- **Defect.** `natural_less` (in `src/modes/reference_mode.cpp` or its
  header) parses runs of digits with `std::stoll`, which throws on
  digit-runs longer than 19 characters (`std::out_of_range`). A
  pathological filename like `Track 12345678901234567890.wav` triggers
  the throw deep inside the comparator, propagating out of `std::sort`
  and aborting the program.
- **Surfacing item.** PR #23 (FIXTURE-REF) rebase audit. The natural-
  filename-sort SKIP at `tests/test_reference_mode.cpp:20` was
  originally tagged for FIXTURE-REF but is really a unit test of
  `natural_less`'s ordering invariant. Folded into Mi-17 because
  hardening the comparator and asserting its post-fix behavior are
  natural pair-work.
- **Invariant established.** "`natural_less` produces a strict-weak-
  ordering total order on filenames containing arbitrary-length digit
  runs, and never throws."
- **Files touched.** `src/modes/reference_mode.cpp` (or wherever
  `natural_less` lives), `tests/test_reference_mode.cpp` (un-SKIP
  natural-sort case), possibly `tests/test_reference_mode.cpp` (new
  unit test for the bounded-digit case).
- **Tests added.**
  - **Un-SKIP** `tests/test_reference_mode.cpp:20` — replace the SKIP
    with an assertion that `natural_less("Track 2.wav", "Track 10.wav")`
    is true. This was the test case PR #23's audit surfaced as
    misattributed.
  - **New** `natural_less: digit run > 18 characters does not throw`
    (or returns the lexicographic result, depending on the cure
    chosen).
- **Cure options.**
  - (a) Bound the digit-run length: take only the first 18 characters
    of any digit run; fall back to lexicographic compare on the rest.
  - (b) Use `std::from_chars` and check for `errc::result_out_of_range`,
    falling back to lexicographic.
  - (c) Manual digit-by-digit compare, never converting to integer.
  Pick one in the PR; document why.
- **Tier rationale.** Tier 9 (Cleanup / Nit) — single-function
  hardening with a single new unit test.
- **Effort.** ≤ 20 lines of code + 2 unit-test cases. One PR, one
  audit. No fixture or pipeline interaction.
- **Exit criteria.**
  - [ ] `natural_less` does not throw on any input.
  - [ ] `tests/test_reference_mode.cpp:20`'s SKIP replaced with a
        real assertion.
  - [ ] Combined with M-REF-ALIGN-UNIT, `test_reference_mode`'s exit
        status flips to `Passed`.
### F-AUDIT2-1 — C-2 integration test exercises the actual guard end-to-end

- **Defect.** The C-2 subprocess integration test invokes
  `mwAudioAutoChop reference /no/such/file ...`, which fails at
  `analyze_reference_mode` and never reaches the new `AudioFile::open`
  guard the test claims to exercise. Audit-2 of C-2 constructed a
  reproducer: a `WAVE_FORMAT_EXTENSIBLE` WAV with a detectable gap
  loads via libsndfile, passes `analyze_blind_mode`, and only then hits
  the `AudioFile::open` strict-validator rejection — exercising the
  new guard end-to-end.
- **Invariant established.** Same as C-2's: "Every CLI branch that
  depends on a successful AudioFile::open short-circuits on failure
  before using the result." This item makes the integration test
  *prove* the guard runs.
- **Files touched.** `tests/test_audio_file.cpp` (or a sibling integration
  file), `tests/fixtures/waveext/` (reuse the FIXTURE-WAVEEXT corpus
  once that lands).
- **Depends on.** FIXTURE-WAVEEXT (PR #25).
- **Exit criteria.**
  - [ ] New subprocess test variant uses a WAVE_FORMAT_EXTENSIBLE fixture
        that fails specifically at `AudioFile::open`, not earlier.
  - [ ] Existing C-2 subprocess test stays as-is (validates the outer
        clean-exit invariant).

### F-AUDIT2-3 — Move `MWAAC_ASSERT_PRECONDITION` to a shared header

- **Defect.** The macro currently lives in `src/core/audio_file.hpp`.
  As soon as a second consumer needs it (M-14, M-15, future Tier 2/3
  fixes), copy-paste becomes likely.
- **Invariant established.** "Project precondition macros have a single
  definition site referenceable from any TU."
- **Files touched.** Move to `src/core/precondition.hpp` (new). Update
  the `audio_file.hpp` include.
- **Trigger.** Defer until a second consumer arrives — *not now*. When
  triggered, this is a ≤ 10-line change.

### F-AUDIT2-DT — Death-test harness extraction

- **Defect.** C-2's death-test scaffolding (fork + waitpid, signal reset,
  child stdio redirect) lives inline in `tests/test_audio_file.cpp`.
  Audit-1 of C-2 noted the same pattern will be needed by M-14's
  death tests; copy-paste becomes likely.
- **Invariant established.** "Death-test harness is a single,
  test-only utility; not duplicated per test file."
- **Files touched.** New `tests/support/death_test.hpp`. Refactor
  C-2's existing tests to use it.
- **Trigger.** Defer until M-14 adds the next death-test consumer.

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

### MI18-FOLLOWUP-BLIND-ITER — defensive cast on `blind_mode.cpp:73-74` iterator+size_t arithmetic

- **Origin.** Promoted from the Mi-18 audit pass-2 advisory finding (PR #30).
  The audit-agent grepped for `.begin() + .size()` / iterator+`size_t`
  patterns across `src/` while verifying the cured bug at
  `src/modes/reference_mode.cpp:240`. Found one untouched site with the
  same shape: `src/modes/blind_mode.cpp:73-74` does
  `samples.begin() + start_sample` / `samples.begin() + end_sample` where
  both are `size_t`.
- **Defect (latent).** The expression is the same shape as the cured
  `reference_mode.cpp:240` bug. GCC currently does not emit
  `-Werror=sign-conversion` on it (otherwise CI would have failed today,
  and Mi-18 would have caught it). Why GCC tolerates this site and not
  line 240 is unclear — most likely because the bound `samples` here is
  a `std::vector<float>` and `.begin()`'s `difference_type` matches more
  cleanly to `size_t` in the local promotion rules, while the cured site
  used `std::span<const float>::iterator` arithmetic with `.size() / 2`.
  Either way the latent UB on size_t→ptrdiff_t overflow is identical.
- **Invariant established.** "Iterator + integral arithmetic in
  mwaac source uses `static_cast<std::ptrdiff_t>(...)` at the
  iterator-arithmetic site whenever the integral is `size_t`, regardless
  of whether the current compiler flags it."
- **Files touched.** `src/modes/blind_mode.cpp` (lines 73 and 74).
- **Tests added.** None required (defensive cleanup; no observable
  behavior change).
- **Owner-epic / lineage.** **Mi-18 audit follow-up.** Phase 4
  reconciliation should cite this item back to PR #30's audit-2 verdict
  alongside the original `reference_mode.cpp:240` cure.
- **Exit criteria.**
  - [ ] `samples.begin() + start_sample` → `samples.begin() + static_cast<std::ptrdiff_t>(start_sample)` at `blind_mode.cpp:73`.
  - [ ] Same cure at line 74 for `end_sample`.
  - [ ] Build remains green on Linux GCC and macOS Apple Clang.
  - [ ] Single commit, no scope creep beyond the two casts.
- **Effort.** ≤ 5 lines of diff, one commit, no audit needed (mechanical
  defensive cast — the audit framework already covered the broader pattern
  in Mi-18 pass 2).

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
   (call-site guards — subsumed by C-2; no separate dispatch).
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
