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

### C-4 — Rate-conversion truncation breaks "sample-accurate" claim — **RESOLVED in #41 (`e519bf6`)**

- **Defect.** `reference_mode.cpp:1109, 1111` convert analysis-rate sample
  indices to native-rate via integer division, truncating up to ~9 samples
  at 192 kHz.
- **Invariant established.** "Reference mode boundaries are within one
  native-rate sample of the analysis-rate result."
- **Files touched.** `src/modes/reference_mode.cpp`,
  `src/modes/reference_mode.hpp`, `tests/test_reference_mode.cpp`.
- **Tests added.**
  - `Reference mode: native-rate boundary is rounded not truncated` (new,
    unit-level, 4 sections / 15 assertions, exercises the helper directly).
- **Exit criteria.**
  - [x] Conversion uses rounding (`std::llround` or `(a*num + den/2)/den`).
        *Integer arithmetic with explicit half-denominator bias chosen for
        64-bit purity (no IEEE-754 reasoning, no precision loss for large
        indices); see `analysis_to_native_sample` at
        `src/modes/reference_mode.cpp:765-787`.*
  - [x] A dedicated helper function carries the rounding; inline
        multiplications by native_sr/analysis_sr are removed.
        *Helper at `src/modes/reference_mode.cpp:765-787` (declared in
        `src/modes/reference_mode.hpp:50-67`); both former call sites at
        the SplitPoint loop wired through;
        `grep '\* native_sr / analysis_sr' src/modes/reference_mode.cpp`
        returns no hits. Tolerance constant
        `kAnalysisToNativeRoundingTolerance = 1` named at file scope per
        `kHeadSize` cycle precedent.*
  - [x] README's "sample-accurate" claim is reconciled (DOC-1) with the
        achievable tolerance.
        *Resolved INVESTIGATE-only 2026-05-24 via T8-PAPERWORK-SWEEP per
        user judgment via AskUserQuestion. README preserved as-is;
        rationale recorded under DOC-1 below — line 43's in-sentence
        "but … sometimes lands a second or two off" hedge serves as
        the test-bridge for the "sample-accurate" descriptor, and the
        strict tolerance lives in `docs/invariants.md` INV-RATECONV-
        ROUNDED (the source of truth for tech-spec readers).*

### C-5 — `compute_spectral_flatness` unsigned wrap + stub implementation

- **Defect.** `analysis.cpp:141` wraps on `samples.size() < frame_length`,
  requesting a ~2⁶³ allocation. The body also returns all-0.5 placeholder
  values. (Pointer was `:82–84`, stale since Mi-2 (#76) shifted line numbers;
  the guarded sibling sites `compute_rms_energy` / `compute_zero_crossing_rate`
  were cured there, but this site is left for C-5 — it has no corrective guard
  and its cure is behavior-changing, return-empty, not a behavior-preserving reorder.)
- **Cross-reference.** `Mi-18-FU-4b` (Mi-18 follow-up catalog) is the **same
  work** — it flagged this `// TODO: Implement with FFT` stub during the Mi-18
  sweep. Single-tracked here under C-5 to avoid double-listing; the vestigial
  `sample_rate` param FU-4b notes resolves when this implementation lands.
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

### DRIFT-MODEL-RATE-TRUNCATION — Rate-conversion truncation in DriftModel::ref_to_vinyl_sample — **RESOLVED in #42 (`718330c`)**

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
  - [x] All three sites at `src/core/drift_model.cpp:10, 16, 33` no
        longer truncate. Lines 10 and 16 use C-4's integer-arithmetic
        helper directly; line 33 uses `std::llround` or routes through
        the integer helper after explicit pre-rounding of
        `vinyl_sample` to `int64_t`.
        *Lines 10 and 16 cured via `analysis_to_native_sample` calls
        post-merge at `src/core/drift_model.cpp:11` and `:17` (line
        numbers post-cure include the inserted block comment); line 33
        cured via `std::llround` at `:50-52` (choice (a) per BACKLOG
        — single-step round-half-away-from-zero on the double product;
        rationale at `drift_model.cpp:36-49`).*
  - [x] Three new unit tests exercising each site with canonical
        inputs (e.g. `(native_sr=192000, analysis_sr=44100)` and
        `ref_sample` chosen so rounding and truncation produce
        different `int64_t` outputs); assertions are exact-match on
        the rounded output, not tolerance windows.
        *`tests/test_drift_model.cpp` (new file, 128 lines): three
        TEST_CASEs under `[drift_model]`, 21 total exact-match `==`
        assertions on `int64_t` outputs. Each TEST_CASE engages its
        target branch via construction (segment_offsets empty /
        back().first==0 / 1000 with non-empty coefficients);
        polynomial-path expected values re-derived independently by
        audit via Python. Each input chosen so rounded ≠ truncated,
        proving each test would fail under pre-cure code.*
  - [x] No new integer-division or float→int truncation paths
        introduced in `src/core/drift_model.cpp`.
        *`grep '\* native_sr / analysis_sr' src/core/drift_model.cpp`
        post-merge returns only the cure block-comment text mention
        at the top of the file; no live code expressions.*
  - [x] Cure mechanism at lines 10, 16 is the same helper C-4 lifts
        (no duplicate helper). Line 33's cure is documented in the
        PR body if it diverges from the helper's signature.
        *`#include "modes/reference_mode.hpp"` added (root-relative
        to `src/`, matching `reference_mode.cpp`'s
        `#include "core/audio_buffer.hpp"` convention; no circular
        dep). Line 33's `std::llround` divergence documented in
        the PR body and at `drift_model.cpp:36-49`.*

### M-9 — std::clamp with hi < lo when vinyl is empty — **RESOLVED in #43 (`7969aec`)**

- **Defect.** `reference_mode.cpp:994` clamp upper bound may be -1.
  *Site drifted from `:994` (BACKLOG-cite) to `:1068` post-cure
  (post-C-4 helper additions and post-M-9 guard-block insertion);
  per `KNOWN-FAILING-SCHEMA-V2`, line is drift-tolerant nav hint.*
- **Invariant established.** "align_per_track skips tracks against empty
  vinyl rather than invoking std::clamp with invalid bounds."
- **Files touched.** `src/modes/reference_mode.cpp`,
  `tests/test_reference_mode.cpp`.
- **Tests added.**
  - `align_per_track: empty vinyl returns empty offsets, no UB` (new).
- **Exit criteria.**
  - [x] Guard at top of per-track loop.
        *Cure landed as **function-entry guard** (option (a)) at
        `src/modes/reference_mode.cpp:858-860` rather than literal
        top-of-loop (option (b)). Mathematically equivalent for the
        empty-vinyl invariant the criterion targets:
        `vinyl.samples.size()` is never mutated during the loop, so
        an in-loop guard would never have a different effect than
        function-entry. Function-entry is the stricter version —
        cleaner contract ("empty vinyl ⇒ empty offsets" with no
        per-track loop body executed) and avoids conflating
        "this track skipped" with "no vinyl at all". (a)-vs-(b)
        rationale documented in cure comment at
        `src/modes/reference_mode.cpp:843-857`. New TEST_CASE
        `align_per_track: empty vinyl returns empty offsets, no UB`
        at `tests/test_reference_mode.cpp:101-118` exercises the
        guard with empty vinyl + non-empty tracks (loop would run
        pre-cure); `REQUIRE(offsets.empty())` exact-match;
        UBSan-clean.*

### M-10 — compute_zero_crossing_rate divides by zero — **RESOLVED in #44 (`6a8c805`)**

- **Defect.** `analysis.cpp:68` when `end - start == 1`.
- **Invariant established.** "ZCR is defined as 0 for frames of length
  less than 2."
- **Files touched.** `src/core/analysis.cpp`, `tests/test_analysis.cpp`.
- **Tests added.**
  - `compute_zero_crossing_rate: single-sample frame returns 0, not NaN`.
- **Exit criteria.**
  - [x] Guard and test.
        *Cure: per-frame in-loop guard at `src/core/analysis.cpp:73-76`
        — `if (end - start < 2) { zcr[i] = 0.0f; continue; }` — sits
        adjacent to the offending divisor at `:80` (formerly `:68`
        pre-cure; +12-line drift from cure-comment block insertion at
        `:60-72`). Choice (a) in-loop guard over (b) function-entry
        early-return per defense-in-depth: (a) pins the BACKLOG
        invariant verbatim ("ZCR is defined as 0 *for frames* of
        length less than 2") at per-frame granularity; (b) would
        conflate frame-length with input-length and leave a latent
        gap if a non-empty signal produced a degenerate trailing
        frame. Defensive double-zero: `< 2` correctly subsumes both
        `end - start == 1` (reachable via `samples.size() == 1` →
        short-signal branch) and the unreachable-but-defensive
        `end - start == 0`. Rationale documented in cure comment at
        `analysis.cpp:60-72`. New TEST_CASE
        `compute_zero_crossing_rate: single-sample frame returns 0,
        not NaN` at `tests/test_analysis.cpp:66-77` exercises the
        guard with `samples = {1.0f}, frame_length = hop_length = 1`;
        three independent assertions (size, exact-match `== 0.0f`,
        NaN exclusion). Pre-cure both content REQUIREs would fail
        (NaN ≠ 0.0f, isnan true). UBSan-clean.*

### Mi-4 — Naive cross_correlate normalization documentation — **RESOLVED in #45 (`c8db84b`)**

- **Defect.** The naive impl uses a global norm factor, which is not Pearson
  NCC per-lag. The docstring doesn't say so.
- **Invariant established.** "The naive `cross_correlate` is a verification
  shim for the FFT implementation; callers treating its peak value as a
  probability are using it wrong."
- **Files touched.** `src/core/correlation.hpp`, `src/core/correlation.cpp`.
- **Tests added.**
  - `cross_correlate and cross_correlate_fft agree on lag` (already in
    test suite; just re-verify after comment) — actual TEST_CASE name
    in source is `"FFT correlation agrees with naive implementation"`
    at `tests/test_correlation.cpp:83-108`; cross-checks lag selection
    via `REQUIRE(fft_result.lag == true_lag)` (`:101`) and
    `REQUIRE(naive_result.lag == true_lag)` (`:102`). Re-verified
    post-cure.
- **Exit criteria.**
  - [x] Header docstring notes the normalization difference explicitly.
        New `NORMALIZATION CAVEAT` paragraph at
        `src/core/correlation.hpp:28-34` explicitly contrasts naive's
        single GLOBAL norm factor (`sqrt(total_ref_energy * total_tgt_energy)`,
        applied uniformly at every lag) against `cross_correlate_fft`'s
        per-lag Pearson NCC, and documents that the naive's `peak_value`
        is not bounded to `[-1, 1]` for arbitrary inputs.
  - [x] Consider marking `[[deprecated]]` or `/* testing-only */`. Chose
        prose framing ("testing-only verification shim for cross_correlate_fft")
        in the docstring at `src/core/correlation.hpp:19-23` rather than
        `[[deprecated]]`. Rationale: `[[deprecated]]` would emit a warning
        at the regression-guard test callsite (`tests/test_correlation.cpp:99`),
        forcing either a `-Werror` break or a localized `#pragma`
        suppression there with no semantic gain. The function is not
        deprecated — it is a verification shim by design — so the prose-
        framing branch of the "or" criterion is the correct cure shape.
        Mi-4 audit (single-audit per Tier 5 governing prompt) caught two
        same-file adjacent-axis findings the fix-agent's naive-side-only
        sweep missed and folded into the merge: (1) `CorrelationResult.peak_value`'s
        field comment had an unconditional `// (0-1)` range claim,
        rewritten at `src/core/correlation.hpp:12-15` to enumerate per-impl
        ranges; (2) `cross_correlate_fft`'s docstring at
        `src/core/correlation.hpp:76-80` claimed peak is "directly comparable
        to the naive version", scoped to lag selection only since peak
        magnitudes use different normalizations.

### M-REF-ALIGN-UNIT — un-SKIP per-track alignment unit test against landed fixture — **RESOLVED in #46 (`4d542d3`)**

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
  - [x] `test_reference_mode.cpp:14`'s SKIP replaced with a real
        assertion against the fixture manifest. Post-Mi-4 close-out the
        SKIP sits at `:18` (line drift from C-4's added passing
        TEST_CASE); this item replaces the body at `tests/test_reference_mode.cpp:28-108`
        with an `align_per_track`-direct assertion that loads vinyl
        and refs at native 44100 Hz via `mwaac::load_audio_mono` and
        the production `load_reference_tracks` loader, parses the flat
        KEY=VALUE manifest, and CHECKs each track's `start_sample`
        against the manifest's `track<i>_start_sample` within the named
        tolerance. Calls `align_per_track` directly (NOT the full
        `analyze_reference_mode` pipeline) so an alignment-precision
        regression that still passes the integration tests' gap-detection
        path shows up here as a localized failure on this item's surface.
  - [x] Tolerance constant is named (`kRefFixtureToleranceSamples`
        or similar) and matches PR #23's integration-test tolerance
        (consistency check). Constant declared at
        `tests/test_reference_mode.cpp:97` as
        `constexpr int64_t kRefFixtureToleranceSamples = (50LL * 44100) / 1000;`
        (= 2205 samples), matching `tests/test_integration.cpp:54-55` verbatim.
        `static_assert(kRefFixtureToleranceSamples == 2205, ...)` at
        `:98-99` pins the value at compile time so any silent drift
        between the unit-level and integration-level tolerance constants
        produces a build failure rather than a runtime divergence.
  - [x] Binary exit-code flip on the `test_reference_mode` ctest
        binary is C-4's cure-signal, not this item's — see
        `docs/known-failing-tests.md` for the cure-attribution split
        across the binary-exit-code axis (C-4 added a passing
        TEST_CASE alongside the SKIPs; Catch2 returns 0 when at least
        one case passes) and the SKIP-cluster axis (this item +
        Mi-17, which collectively replace both SKIPs in
        `tests/test_reference_mode.cpp` with real assertions). Mi-17
        independently handles the `:20` un-SKIP (natural-sort
        filename ordering); this item handles `:14` (per-track
        alignment under the FIXTURE-REF v1 manifest, criterion 1
        above). The two surfaces are orthogonal cures of the same
        `known-failing-tests.md` entry along different axes.

---

## Tier 6 — API hygiene

### M-6 — score_gap units are ambiguous — **RESOLVED in #52 (`76899fc`)**

- **Defect.** `blind_mode.cpp:57–93` takes sample indices; `detect_gaps`
  returns frame indices. Call sites multiply by hop_length to bridge.
- **Invariant established.** "Sample-index and frame-index types are not
  implicitly convertible."
- **Files touched.** `src/modes/blind_mode_indices.hpp` (NEW — internal
  header with phantom-typed `mwaac::detail::SampleIdx` and `FrameIdx` +
  `frame_to_sample` bridge + 6 static_assert contracts; **subsequently
  hoisted to `src/core/frame_sample_bridge.hpp` in M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE
  (PR #53, `0806db3`)** as the bridge gained a second consumer in
  `src/core/music_detection.cpp` — rename-only, types and contracts
  byte-identical, only docstring updated to acknowledge the shared
  scope. M-6's PR landed the original at the `modes/` path; the
  rename followed immediately in the next cycle item),
  `src/modes/blind_mode.cpp` (analyze_blind_mode gap-iteration loop
  uses the typed bridge at the conversion sites),
  `tests/test_blind_mode.cpp` (2 new TEST_CASEs: STATIC_REQUIREs
  mirroring the in-header static_asserts + runtime bridge correctness).
  Public `src/modes/blind_mode.hpp` is INTENTIONALLY unchanged — the
  user-authorized cure shape ("Tagged types (scoped)") explicitly
  scoped the cure to blind-mode internals, so the public API stays
  byte-identical to pre-cure.
- **Tests added.** Compile-time tests that mixing units fails:
  six `static_assert`s, originally at `src/modes/blind_mode_indices.hpp:80-97`
  in M-6's PR and now at `src/core/frame_sample_bridge.hpp:94-111`
  post-M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE hoist (PR #53), mirrored at
  TU boundary by two `STATIC_REQUIRE` blocks in
  `tests/test_blind_mode.cpp:152-181` + a runtime bridge-correctness
  TEST_CASE at `:183-205`. Audit-1 verified the static_asserts fire
  recognisably if `explicit` is stripped from the ctors.
- **Exit criteria.**
  - [x] `SampleIndex`/`FrameIndex` tagged int types, or at minimum
        unambiguous parameter names + a header comment stating units.
        Cure shape: **tagged int types (scoped)** per user-authorized
        choice (presented as 3-option AskUserQuestion 2026-05-17; user
        chose "Tagged types (scoped)" over the full project-wide
        option and the minimum-blast naming-only option). Compromise
        position: compile-time safety where it matters
        (`gap.first/gap.second * hop_length` bridge inside
        `analyze_blind_mode`) without project-wide API churn.
        Audit-1 CLEAN, audit-2 CONCERNS — but the audit-2 CONCERN was
        on the close-out paperwork side (file sibling items in
        adjacent files before declaring Tier 6 done), not on M-6's PR
        shape. Sibling items filed pre-merge as
        M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE and M-REF-FRAME-SAMPLE-BRIDGE
        in commit `214151e`.

### M-7 — score_gap ignores sample_rate parameter — **RESOLVED in #50 (`02eef0c`)**

- **Defect.** Parameter marked `[[maybe_unused]]`.
- **Invariant established.** "Public APIs do not carry dead parameters."
- **Files touched.** `src/modes/blind_mode.hpp`, `src/modes/blind_mode.cpp`,
  `tests/test_blind_mode.cpp` (signature update).
- **Exit criteria.**
  - [x] Either use sample_rate (spectral-flatness scoring) or remove it.
        Chose **remove** (YAGNI). Spectral flatness is C-5's scope
        (`BACKLOG.md` Tier 2 — `compute_spectral_flatness` unsigned wrap
        + stub implementation); folding C-5's hypothetical use into M-7
        would have violated single-function scope discipline AND tied
        M-7's cure to C-5's currently-stub implementation. If a future
        caller needs spectral-flatness scoring, the parameter can be
        re-added at that point with a meaningful implementation rather
        than `[[maybe_unused]]` cruft. 6-LOC change across 3 files:
        `src/modes/blind_mode.hpp:73-78` (declaration + 5-line M-7
        commentary explaining the spectral-flatness deferral),
        `src/modes/blind_mode.cpp:61` (definition), `:213-216`
        (analyze_blind_mode callsite), `tests/test_blind_mode.cpp:29`
        (test callsite). Subsumes Mi-7 ("Duplicate of M-7 / same
        resolution" per the Mi-7 BACKLOG entry).

### M-8 — Blind mode returns error on single-track rips — **RESOLVED in #51 (`5c533da`)**

- **Defect.** `NoGapsFound` is a legitimate outcome, not an error.
- **Invariant established.** "Blind mode returns a single-split result on a
  gap-free input, with confidence reflecting the absence of evidence."
- **Files touched.** `src/modes/blind_mode.cpp` (early-return removal +
  cure-rationale comment), `src/modes/blind_mode.hpp` (`NoGapsFound`
  enum value removal), `src/main.cpp` (switch arm removal),
  `tests/test_blind_mode.cpp` (new TEST_CASE + temp-WAV helper),
  `CMakeLists.txt` (test_blind_mode gains mwaac_sndfile link).
- **Tests added.**
  - `analyze_blind_mode: single-track (gap-free) input returns 1 split`
    in `tests/test_blind_mode.cpp:69-141`. **Fixture-choice critical:**
    a 1-second tone at 22050 Hz (NOT 5 seconds) — must be shorter than
    `min_gap_seconds = 2.0` to force `detect_gaps` to drop the candidate
    via the gap-length check. Audit-1 caught the original 5 s variant
    passing via the score-rejection path rather than the M-8 cure path
    (gap_rms == signal_reference_rms → score 0 → below 0.6 threshold);
    that test would have passed equally well with the M-8 fix reverted.
    Empirical regression-guard check: with cure reverted via
    `git checkout main -- src/modes/blind_mode.{cpp,hpp}`, the test
    FAILS with `result.error() == BlindError::NoGapsFound`; with cure
    restored, 16 assertions all pass. Second-axis guard: assertion that
    `metadata["num_gaps_found"] == 0.0` catches the regression if a
    future fixture change accidentally falls into the score-rejection
    path.
- **Exit criteria.**
  - [x] No error return on empty `gaps`. Cure shape: minimum-blast
        deletion of the early-return short-circuit at
        `src/modes/blind_mode.cpp:182-185` pre-cure. Function now falls
        through into the existing single-split construction (first-track-
        at-zero SplitPoint + zero-iteration gaps for-loop + post-loop
        end_sample fill-in), yielding a 1-split result with confidence
        1.0 spanning the entire input. `BlindError::NoGapsFound` enum
        value removed; switch in `src/main.cpp:264-273` now exhaustive
        on 2-value enum (compiler-verified, no `-Wswitch` warning).
        Verbose log line kept (downgraded WARNING → INFO).
        Confidence-value interpretation (1.0 = "single-track assertion
        is well-supported by absence of gap evidence") documented in
        cure-comment at blind_mode.cpp:182-191 and in the new
        INV-BLIND-SINGLE-TRACK INV doc entry per audit-1 finding 6.
        Audit-1 surfaced a CONCERNS verdict on the original test
        fixture (5 s tone) that was test-passes-via-wrong-path — fix
        landed in follow-up commit `7ba40dc` per audit's prescribed
        repair. Adjacent-entry sweep flagged
        `ReferenceError::NoTracksFound` as structurally similar but
        likely a true user-config error; filed as M-REF-NO-TRACKS-OUTCOME
        (Tier 6, commit `e8261d8`) for investigation rather than
        folding into M-8.

### M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE — adjacent frame×hop_length untyped multiplication in detect_music_start — **RESOLVED in #53 (`0806db3`)**

- **Origin.** Surfaced during M-6 audit-2 (PR #52 audit-agent finding 1,
  2026-05-17). Independent grep for `* hop_length` patterns across
  `src/` after M-6's typed bridge landed flagged the structurally
  similar untagged multiplication at `src/core/music_detection.cpp:75`.
- **Defect.** `detect_music_start` at `src/core/music_detection.cpp:75`
  read `return static_cast<int64_t>(i) * hop_length;` where `i` is the
  loop iterator into `is_music` (frame-indexed) and `hop_length` is the
  frame stride. Same shape as the M-6 defect on `gap.first * hop_length`
  in `analyze_blind_mode`: a future edit could silently swap `i` for a
  same-typed but semantically-different frame variable (e.g.
  `min_music_frames` or `frame_length`) and produce wrong-by-a-factor
  sample offsets.
- **Invariant established.** Extends M-6's INV-INDEX-TYPE-DISJOINT scope
  from one TU to two — the same typed bridge now guards the frame-to-
  sample crossing in both `analyze_blind_mode` and `detect_music_start`.
- **Files touched.** `src/core/frame_sample_bridge.hpp` (RENAMED from
  `src/modes/blind_mode_indices.hpp` via `git mv` — pure architectural
  hoist; types and contracts byte-identical, only docstring updated to
  acknowledge the shared-scope use; bridge now lives in `core/` so it
  is reachable from both `core/music_detection.cpp` and
  `modes/blind_mode.cpp` without the architectural inversion of `core/`
  including from `modes/`). `src/core/music_detection.cpp` (cure-site:
  `detect_music_start` now goes through
  `detail::frame_to_sample(detail::FrameIdx{i}, hop_length).value`
  at the single frame-to-sample crossing in the function).
  `src/modes/blind_mode.cpp` + `tests/test_blind_mode.cpp` (include-path
  + comment updates for the rename; behavioural code unchanged).
  `tests/test_music_detection.cpp` (2 new TEST_CASEs — `STATIC_REQUIRE`
  contract block mirroring the in-header static_asserts at the
  music-detection TU boundary, plus a smoke-test that the returned
  sample-index is divisible by hop_length to catch a hypothetical
  regression that returned the frame index `i` directly without the
  multiplication).
- **Tests added.** See `tests/test_music_detection.cpp:56-72` (contract
  block) and `:74-112` (smoke-test). The contract block mirrors the
  pattern from `tests/test_blind_mode.cpp:152-181` so a future
  regression that strips `explicit` from the index ctors fires
  recognisably in both test files (not just at the internal-header
  build error).
- **Bridge-location decision.** Pre-PR the BACKLOG entry left open
  whether to leave the bridge in `modes/` and have `core/` include
  from `modes/` (an architectural inversion against the intended
  `core/ ← modes/` layering — see existing vestigial inversion at
  `src/core/drift_model.cpp:2`) or to hoist the bridge to `core/` so
  both TUs include downward. Resolved by hoist; the bridge is shared
  vocabulary by definition (both TUs operate on the same RMS-frame ×
  hop_length semantics), so the natural layer is the lowest TU that
  uses it. The header docstring (post-audit-1 finding 3) is softened
  from "cannot include from modes/" to "the project's intended
  include-graph layering puts core/ below modes/" to avoid overstating
  the technical constraint while still steering reviewers correctly.
- **Audit-cardinality.** Two-audit per
  `feedback_audit_cardinality_two_axes.md`: sharp-hook axis said
  single-audit (pure adoption of an existing bridge; CI alone catches
  multiplication-correctness regressions) but blast-radius axis flagged
  multi-axis because the rename touches every TU that included
  `blind_mode_indices.hpp` AND the docstring asserts cross-tier
  intent. Belt-and-braces paid off: audit-1 CONCERNS surfaced 5
  findings, of which finding 1 (in-PR stale comment references at
  `blind_mode.cpp:213` and `test_blind_mode.cpp:154` to the old path)
  and finding 3 (overstated "cannot include" docstring claim) were
  fixed in commit `c0a0d70` pre-merge. Audit-2 CLEAN.
- **Audit-2 deferred findings (carried into this close-out).**
  - Finding 2 — stale `src/modes/blind_mode_indices.hpp` path references
    in `BACKLOG.md` M-6 entry (lines 969, 981) and
    `docs/invariants.md` INV-INDEX-TYPE-DISJOINT (lines 605, 614, 618).
    Fixed in this close-out commit alongside line-number updates
    (static_asserts moved from `:80-97` in the old file to `:94-111`
    in the hoisted file).
  - Finding 4 — verify PR numbers in the post-hoist `frame_sample_bridge.hpp`
    History section reference the right PRs (`M-6 (PR #52)` and
    `M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE (PR #53)`). Verified against
    merge log: PR #52 → `76899fc`, PR #53 → `0806db3`. Both correct.
  - Finding 5 — smoke-test in `test_music_detection.cpp:74-112`
    technically duplicates the existing "Music start detection finds
    loud region" TEST_CASE's coverage of `detect_music_start`'s
    return-value correctness; the divisibility-by-hop_length axis is
    novel (catches frame-index-returned-as-sample-index specifically)
    but the broader sample-magnitude assertions overlap. Acknowledged
    as redundancy-with-purpose — the divisibility check is the
    targeted regression-guard for the bridge contract; surrounding
    sample-magnitude assertions document the intended test fixture
    and would surface a fixture-drift bug in either case. Net-positive
    despite the overlap.
- **Exit criteria.**
  - [x] `detect_music_start:75` adopts the typed bridge so the
        frame-to-sample multiplication is no longer untagged.
        Implementation at `src/core/music_detection.cpp:75-87` —
        `return detail::frame_to_sample(detail::FrameIdx{i}, hop_length).value;`.
        Bridge identity preserved across the hoist (rename-only on
        the types, no semantic change).
  - [x] Compile-time test mirrors the M-6 `static_assert` style on
        whatever bridge namespace is used. `tests/test_music_detection.cpp:56-72`
        mirrors the same six `STATIC_REQUIRE`s the M-6 TEST_CASE pins
        from the blind-mode TU, so the contract is enforced from
        every TU that adopts the bridge — same `mwaac::detail::SampleIdx`/
        `FrameIdx` namespace as M-6 (bridge identity preserved across
        the hoist).

### M-REF-FRAME-SAMPLE-BRIDGE — adjacent frame×frame_size untyped multiplications in reference_mode envelope path — **RESOLVED in #54 (`a09af8f`)**

- **Origin.** Surfaced during M-6 audit-2 (PR #52 audit-agent findings
  2-3, 2026-05-17). Same-shape sibling of M-6 in a different mode.
- **Defect.** Originally filed at two sites (`:253`, `:332` per pre-PR
  line numbers); cure scope was expanded to four sites in PR #54 after
  audit-1 noted same-shape inner-loop base computations adjacent to
  the originally-filed return statements (without the expansion, the
  function would have been half-cured — return guarded, loop-base
  unguarded). All four sites have the same defect shape as M-6's
  `gap * hop_length`: a future edit could swap a frame variable for a
  same-typed sample variable and produce wrong-by-a-factor offsets.
- **Cure shape (user-authorized).** Sibling struct `EnvFrameIdx`
  alongside `FrameIdx` in `src/core/frame_sample_bridge.hpp`, with its
  own `env_frame_to_sample(EnvFrameIdx, int64_t frame_size)` bridge.
  Mutually disjoint from FrameIdx (5 negative + 1 positive
  static_asserts mirror the M-6 set; close-out NIT added a 7th — the
  matching `!std::is_constructible_v<SampleIdx, EnvFrameIdx>` — for
  set-shape symmetry with the FrameIdx/SampleIdx pair). Chosen over
  "Templatized FrameIdx<Tag>" (more churn for marginal API uniformity)
  and "Reuse FrameIdx" (would silently allow cross-mode mixing) via
  AskUserQuestion 2026-05-17. The `int64_t frame_size` parameter (vs
  `int hop_length` on the FrameIdx bridge) matches the reference_mode
  local type (`std::max<int64_t>(1, sample_rate * frame_ms / 1000.0)`),
  avoiding a narrowing cast at the bridge boundary.
- **Invariant established.** INV-INDEX-TYPE-DISJOINT extended to cover
  both bridges — see `docs/invariants.md`. "Sample-, frame-, and
  envelope-frame-index types are not implicitly convertible to each
  other or to raw integers; the only supported conversions are the
  two bridges (`frame_to_sample`, `env_frame_to_sample`)."
- **Files touched.** `src/core/frame_sample_bridge.hpp` (new
  EnvFrameIdx struct + env_frame_to_sample bridge + 7 contract
  static_asserts; docstring rewritten to introduce the second bridge
  as a peer of the first), `src/modes/reference_mode.cpp` (include +
  4 cure sites; see Bridge-adoption sites below),
  `tests/test_reference_mode.cpp` (2 new TEST_CASEs mirroring the
  M-6 / M-MUSIC-DETECT pattern).
- **Bridge-adoption sites in `src/modes/reference_mode.cpp`** (pre-PR
  line numbers; post-cure shifted slightly by include + comment
  additions):
  - `:200` — `measure_fade_in_samples` rms loop inner-base: hoisted
    `f * frame_size` out of inner loop as a bridge-routed `base`
    (also a micro-opt — pre-cure the multiplication recurred every
    inner-loop iteration via `samples[f * frame_size + i]`).
  - `:253` — `measure_fade_in_samples` return: envelope-frame fade
    end → sample offset via bridge.
  - `:272` — `compute_rms_envelope` rms loop inner-base: routes
    existing `base = f * frame_size` through bridge.
  - `:332` — `envelope_refine_start` return: correlation-lag →
    sample offset via bridge. Includes cast-safety comment
    documenting why `static_cast<std::size_t>(r.lag)` is safe —
    `cross_correlate_fft`'s loop at `src/core/correlation.cpp:340`
    guarantees a non-negative lag in
    `[0, vinyl_env.size()-ref_env.size()]`, and the early-return
    guard above ensures `vinyl_env > ref_env`.
- **Adjacent-site sweep.** `:659-660` (`skip_leading_silence`) reviewed
  and judged out-of-scope: `i` is a head-iteration count (0..3)
  bounded by the literal `< 4` loop bound, not an envelope-vector
  index, and `frame_size` there is a function-local 50 ms stride
  distinct from the envelope frame_size. Routing through the envelope
  bridge would conflate iteration-count and frame-index semantics.
  Audit-1 axis-4 independently confirmed this exclusion is correct.
- **Cross-tier finding from audit-2.** `src/core/analysis.cpp:26`
  (`compute_rms_energy`) and `:57` (`compute_zero_crossing_rate`)
  have the same `i * static_cast<std::size_t>(hop_length)` shape
  producing a sample-domain offset. Same defect class as M-MUSIC-DETECT
  but in `analysis.cpp` rather than `music_detection.cpp`. Filed as
  **M-ANALYSIS-FRAME-SAMPLE-BRIDGE** (separate Tier 6 item) per
  `feedback_tier_boundary_preservation.md` — does NOT fold into PR #54.
- **Tests added.**
  - `tests/test_reference_mode.cpp` "M-REF-FRAME-SAMPLE-BRIDGE:
    EnvFrameIdx contract is disjoint from FrameIdx" — STATIC_REQUIREs
    mirror the in-header static_asserts at the reference-mode TU
    boundary (mirrors the M-6 / M-MUSIC-DETECT pattern). Audit-1
    verified the test is 1:1 with the in-header contracts; audit-2
    verified mutual-disjointness holds in all three spot-test
    directions (`frame_to_sample(EnvFrameIdx{...}, ...)`,
    `env_frame_to_sample(FrameIdx{...}, ...)`, and
    `SampleIdx{EnvFrameIdx{...}}` all fail to compile).
  - `tests/test_reference_mode.cpp` "M-REF-FRAME-SAMPLE-BRIDGE:
    env_frame_to_sample multiplies by frame_size" — exercises the
    bridge at representative envelope frame_sizes (50 ms and 100 ms
    at sr=44100) + constexpr-evaluation STATIC_REQUIRE.
- **Audit-cardinality.** Two-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook axis said
  single-audit (pure adoption pattern, established cure family) but
  blast-radius axis flagged multi-axis on header API expansion (new
  EnvFrameIdx type + new bridge function + 7 new static_asserts).
  Audit-1 CONCERNS — paperwork-only (exit-criterion scope-expansion
  and INV stale references, both folded into this close-out commit).
  Audit-2 CONCERNS — 1 NIT (symmetric SampleIdx-from-EnvFrameIdx
  assert, added in this close-out commit) + 1 cross-tier finding
  (M-ANALYSIS-FRAME-SAMPLE-BRIDGE, filed as separate Tier 6 item).
  Both audits confirmed cure correctness clean.
- **Tier rationale.** Tier 6 (API hygiene). Same shape, same tier as
  M-6 / M-MUSIC-DETECT.
- **Exit criteria.**
  - [x] `envelope_refine_start:332` adopts the typed bridge so the
        envelope-frame → sample multiplication is no longer untagged.
        Implementation: `window_start + detail::env_frame_to_sample(
        detail::EnvFrameIdx{static_cast<std::size_t>(r.lag)},
        frame_size).value`.
  - [x] `measure_fade_in_samples:253` adopts the typed bridge.
        Implementation: `detail::env_frame_to_sample(
        detail::EnvFrameIdx{static_cast<std::size_t>(fade_end_frame)},
        frame_size).value`.
  - [x] `measure_fade_in_samples:200` rms-loop inner-base adopts the
        typed bridge (scope-expansion: same-shape sibling adjacent
        to `:253`; audit-1 axis-1 confirmed appropriate).
  - [x] `compute_rms_envelope:272` rms-loop inner-base adopts the
        typed bridge (scope-expansion: same-shape sibling adjacent
        to `:332`'s envelope domain).
  - [x] Compile-time test mirrors the M-6 `static_assert` style on
        `EnvFrameIdx` — see `tests/test_reference_mode.cpp`'s
        "M-REF-FRAME-SAMPLE-BRIDGE: EnvFrameIdx contract is disjoint
        from FrameIdx" TEST_CASE.

### M-ANALYSIS-FRAME-SAMPLE-BRIDGE — untyped frame×hop_length in compute_rms_energy and compute_zero_crossing_rate — **RESOLVED in #55 (`48263d1`)**

- **Origin.** Surfaced during M-REF-FRAME-SAMPLE-BRIDGE audit-2 (PR #54
  audit-agent, 2026-05-17). Independent grep across `src/` for the
  bridge-class defect pattern (untagged `frame_index * stride` →
  sample-domain offset) after M-REF landed flagged two structurally
  similar sites in `src/core/analysis.cpp` that weren't on the M-6
  audit-2 sweep's radar.
- **Defect.** Two sites in `src/core/analysis.cpp` perform the same
  `frame_index * hop_length` → sample-offset untagged arithmetic that
  M-6 / M-MUSIC-DETECT cured at the blind_mode and music_detection
  sites:
  - `:26` — `size_t start = i * static_cast<std::size_t>(hop_length);`
    inside `compute_rms_energy`'s frame loop. `i` is a frame index
    into the (then-empty) `rms` vector; `hop_length` is the frame
    stride; the result is a sample-domain offset into `samples`.
  - `:57` — same shape inside `compute_zero_crossing_rate`'s frame
    loop. Same defect class.
  Both have the same shape as M-6's `gap * hop_length` defect: a
  future edit could swap a frame variable for a same-typed sample
  variable and produce wrong-by-a-factor offsets. The defect is
  particularly significant here because `compute_rms_energy` is the
  upstream of every other site already cured by the bridge family
  (`analyze_blind_mode`, `detect_music_start`, `estimate_noise_floor`,
  `compute_rms_envelope`); a regression here would cascade through
  the entire analysis pipeline.
- **Invariant established.** Same as M-6 / M-MUSIC-DETECT: "frame-
  indexed and sample-indexed quantities are not implicitly
  convertible at the bridge site." Both sites use the same RMS-frame
  semantics as the existing FrameIdx (50 ms frame, 12.5 ms hop at
  analysis_sr — see `src/modes/blind_mode.cpp:116-117` for the canonical
  setting), so the existing `FrameIdx` and `frame_to_sample` apply
  directly. No new types needed.
- **Files touched.** `src/core/analysis.cpp` (cure both `:26` and
  `:57` via existing `frame_to_sample` bridge), include of
  `core/frame_sample_bridge.hpp`. `tests/test_analysis.cpp` (one new
  TEST_CASE block: STATIC_REQUIRE the FrameIdx contract from the
  analysis TU + smoke-test that the cure preserves arithmetic).
- **Tests added.** Compile-time + runtime: mirror the M-MUSIC-DETECT
  pattern at `tests/test_music_detection.cpp:56-112`.
- **Tier rationale.** Tier 6 (API hygiene). Same shape, same tier as
  M-6 / M-MUSIC-DETECT / M-REF-FRAME-SAMPLE-BRIDGE.
- **Effort.** ≤ 25 LOC (pure adoption of existing bridge — no new
  types, no new bridge function) + 1-2 unit-test cases. One PR, one
  audit (single-audit defensible: pure adoption of an established
  bridge with no new API surface, unlike M-REF which added EnvFrameIdx).
  But blast-radius may still flag two-audit on the
  cascade-through-pipeline observation above; decide at dispatch.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` — the
  finding surfaced during M-REF-FRAME-SAMPLE-BRIDGE audit on different
  files in the same defect class. Filing as own Tier 6 item rather
  than folding into M-REF preserves single-PR scope and lets the
  M-REF merge proceed on the original two-mode scope.
- **Audit-cardinality.** Two-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook said
  single-audit (pure adoption of established bridge, no new API
  surface) but blast-radius axis flagged multi-axis on the upstream-
  cascade observation (compute_rms_energy is upstream of every site
  already cured by the bridge family). Audit-1 CONCERNS: 1 MEDIUM
  finding (correlation.cpp:145 same-class candidate — see
  M-CORRELATION-FRAME-SAMPLE-BRIDGE filed below) + 1 LOW
  (smoke-test cure-attribution framing, accepted as established
  family pattern) + 1 NIT (trailing newline missing on
  test_analysis.cpp — fixed in this close-out). Audit-2 CLEAN with
  one meta-observation: the bridge family has reached fixed-point on
  the frame→sample axis after 4 PRs (this is the first PR in the
  family where audit-2's exhaustive sibling sweep returned ZERO new
  same-class sites, modulo audit-1's disputed correlation.cpp:145
  classification — see below).
- **Bridge family fixed-point.** After 4 PRs (M-6, M-MUSIC-DETECT,
  M-REF-FRAME-SAMPLE-BRIDGE, M-ANALYSIS), every untagged
  `frame_index × stride` → sample-domain offset site in `src/` is
  now either bridge-routed or explicitly reviewed and excluded with
  documented rationale (the single
  `skip_leading_silence:659-660` site). The dispatch tail length (4
  PRs) was a function of pre-dispatch grep incompleteness, not
  genuine code growth — audit-2 promoted that observation to
  `feedback_dispatch_grep_for_typed_bridge_family.md`.
- **Exit criteria.**
  - [x] `compute_rms_energy:26` adopts the typed bridge so the
        frame-to-sample multiplication is no longer untagged.
        Implementation at `src/core/analysis.cpp:25-37` —
        `const size_t start = static_cast<std::size_t>(
        detail::frame_to_sample(detail::FrameIdx{i}, hop_length).value);`.
        Same FrameIdx + frame_to_sample as M-MUSIC-DETECT; RMS-frame
        semantics shared across the family.
  - [x] `compute_zero_crossing_rate:57` adopts the typed bridge.
        Implementation at `src/core/analysis.cpp:60-66` (line shifted
        post-cure by the new comment block).
  - [x] Compile-time test mirrors the M-6 `static_assert` style at
        the analysis TU boundary —
        `tests/test_analysis.cpp` "M-ANALYSIS-FRAME-SAMPLE-BRIDGE:
        SampleIdx/FrameIdx contract holds in analysis TU" pins 6
        STATIC_REQUIREs from the analysis TU (mirror of
        test_blind_mode and test_music_detection contract blocks).
        Complemented by the bridge-correctness smoke test
        ("compute_rms_energy frame stride is hop_length") using a
        polarity-flip square-wave fixture aligned to hop_length;
        audit-1 noted (and the in-test comment acknowledges) the
        smoke test is behavior-preservation verification, not a
        regression-guard against revert (the contract block IS the
        regression-guard via failed-compile-on-strip-explicit). Same
        established pattern as M-MUSIC-DETECT's divisibility smoke
        test.

### M-CORRELATION-FRAME-SAMPLE-BRIDGE — disputed: `i * factor` in downsample (inter-lattice vs frame×stride classification) — **RESOLVED INVESTIGATE-ONLY 2026-05-18 (audit-2 classification confirmed)**

- **Origin.** Surfaced during M-ANALYSIS-FRAME-SAMPLE-BRIDGE audit-1
  (PR #55, 2026-05-18). `src/core/correlation.cpp:135-153` —
  `downsample` helper performs `size_t start = i * static_cast<std::size_t>(factor);`
  where `i` iterates `[0, output_size)` and `factor` is the
  downsampling ratio.
- **Defect classification — disputed by the two audits.**
  - **Audit-1 framing (same-defect-class, file for cure).** `i` is a
    frame-index into a vector where each element represents a
    `factor`-sized block of input samples (an
    "averaged-block index"); `factor` is the per-element stride;
    `start` is a sample-domain offset into `samples`. Structurally
    identical to `compute_rms_energy`'s pre-cure shape — the only
    difference is `result[i]` stores a mean (downsampled value)
    rather than an RMS (energy value). A future edit could
    accidentally swap `i` for `output_size`, or refactor to take a
    frame_size from elsewhere, and the type discipline would catch
    the bug.
  - **Audit-2 framing (not-a-match, don't file).** Both `samples`
    and `result` are sample-domain (a sample-lattice signal at two
    different rates). The conversion `start = i * factor` is an
    inter-lattice mapping from output-lattice sample-positions to
    input-lattice sample-positions, not a frame→sample crossing.
    In DSP terms, downsampling preserves the "sample stream" nature
    of the signal; calling `i` a "frame index" is a structural-shape
    abstraction not a semantic-role match.
- **Orchestrator gate-eval.** Both framings defensible on close
  reading. Audit-2's lattice analogy is structurally sound; audit-1's
  "could be confused with other size_t variables" argument is the
  exact rationale that motivated the original M-6 cure. Filing as
  INVESTIGATE-only — the cure-vs-not-cure decision benefits from
  user judgment. Preliminary lean toward audit-2's classification
  (downsample's output IS semantically a sample stream, just at a
  lower rate); but if the user lands on audit-1's framing the cure
  shape would be either (a) a new `DownsampleFrameIdx` sibling type
  (~25 LOC, mirrors EnvFrameIdx pattern), or (b) reuse `FrameIdx`
  if the user accepts that downsample frames are "RMS-frame-like
  enough" (~5 LOC, no new type).
- **Investigation outcome (2026-05-18).** **Option (a) confirmed —
  audit-2's not-a-match classification is correct; no code cure
  needed.** Reasoning:
  - **`downsample` is rate conversion, not frame extraction.** Both
    the input `samples` parameter and the output `result` are
    sample-domain arrays. The function converts an N-sample signal
    at rate R into an (N/factor)-sample signal at rate R/factor by
    averaging `factor`-sized blocks. `result[i]` is a SAMPLE in the
    downsampled lattice — semantically the same KIND of value as
    `samples[k]`, not a feature-aggregate of a different kind (as
    `compute_rms_energy`'s `rms[i]` is — an energy value distinct
    from the input samples).
  - **`:214` is the structural confirmation.** Inside the same TU,
    `coarse_lag = best_coarse_lag * downsample_factor` is explicitly
    named as a lag conversion (downsampled-lattice lag → full-
    resolution lag). Both operands are sample-domain lag-quantities.
    The whole `cross_correlate_fast` function family treats
    downsample's input and output as sample-domain at different
    rates — the `* factor` and `/ factor` operations are inter-
    lattice mappings, not frame×stride crossings.
  - **Type-discipline value would be negative.** Introducing a
    `DownsampleFrameIdx` would mislabel the downsampled signal as
    "frames" when it is semantically samples at a lower rate. The
    cure pattern that worked for M-6 / M-MUSIC-DETECT / M-REF /
    M-ANALYSIS depends on the index domain being a DIFFERENT KIND
    of thing from the sample domain (RMS-frame indices, envelope-
    frame indices). For downsample, the index isn't a different
    kind of thing — it's a sample position in a different lattice.
  - **Confusion path is closed.** In-scope size_t variables at
    `correlation.cpp:143` are `output_size`, `samples.size()` — both
    sample-COUNTS (cardinality), not sample-POSITIONS. The loop
    structure `for (size_t i = 0; i < output_size; ++i)` makes
    `i`'s role unambiguous; there's no realistic future-edit
    confusion path the typed bridge would prevent.
- **Generalization — inter-lattice vs frame×stride classification.**
  The bridge family cures untagged `frame_index × stride`
  multiplications producing sample offsets. The defect class is
  specifically "X-domain index × per-X-element stride → Y-domain
  offset, where X and Y are semantically different kinds." A pattern
  that LOOKS structurally similar but is actually "X-domain index ×
  ratio → X-domain offset at different rate" (inter-lattice mapping)
  is NOT in the bridge family's defect class. Future bridge-family
  sweeps should classify each candidate site along this distinction
  rather than fire reflexively on structural similarity. (Recorded
  in this BACKLOG entry's resolution, not promoted to a feedback
  memory — single example so far; promote if a second similar
  classification question arises.)
- **Files touched.** None (paperwork-only close). The audit-1 finding
  was the right thing to file — preserving the open classification
  question rather than reflexively expanding M-ANALYSIS scope — and
  the investigation confirmed audit-2's framing was the correct
  classification.
- **Tier rationale.** Tier 6 (API hygiene). Same tier as M-ANALYSIS;
  classification outcome is "not a member of the bridge family
  defect class."
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` —
  the finding surfaced during M-ANALYSIS audit-1 on a different file
  in a structurally-similar shape. Filing as its own Tier 6 item
  rather than expanding M-ANALYSIS scope (which the cure family had
  already declared at fixed-point per audit-2) preserved the option
  while not blocking M-ANALYSIS merge on an unresolved classification
  question — pattern confirmed productive.

### M-REF-NO-TRACKS-OUTCOME — `ReferenceError::NoTracksFound` may misclassify a legitimate outcome as an error — **RESOLVED INVESTIGATE-ONLY 2026-05-18**

- **Origin.** Surfaced during M-8 audit-1 (PR #51 audit-agent finding 5,
  2026-05-16). Adjacent-entry sweep on the BlindError enum after M-8
  removed `NoGapsFound` flagged the structurally similar
  `ReferenceError::NoTracksFound` at `src/modes/reference_mode.hpp:24`.
- **Defect (provisional — needs investigation).** `load_reference_tracks`
  at `src/modes/reference_mode.cpp:846-862` returns
  `ReferenceError::NoTracksFound` when the reference directory contains
  no audio files. Unlike blind-mode's `NoGapsFound` (which M-8 cured
  because the algorithm "correctly finding zero gaps" is a legitimate
  outcome, not an error), `NoTracksFound` is most likely a true
  user-config error — the user pointed `analyze_reference_mode` at the
  wrong directory or one with non-audio files. **The structural
  similarity to NoGapsFound does not by itself imply M-8-style cure
  applies.** Investigation needed.
- **Investigation outcome (2026-05-18).** **Option (a) confirmed —
  `NoTracksFound` is a true input-validation failure; no code cure
  needed.** Reasoning:
  - **Algorithm-vs-input distinction.** `NoGapsFound` (M-8) was raised
    after `compute_rms_energy`, `estimate_noise_floor`, and
    `detect_gaps` all successfully executed on a valid audio input
    and produced a meaningful zero-gap result — a legitimate
    algorithmic outcome that the M-8 cure converted to a degenerate
    single-split result. `NoTracksFound` is raised at
    `src/modes/reference_mode.cpp:861-862` BEFORE any alignment
    algorithm runs, when the reference directory contains zero audio
    files. There is no algorithm to run; there is no degenerate
    `AnalysisResult` to build because reference-mode alignment is
    *defined as* "align vinyl regions to reference tracks" and there
    are no reference tracks.
  - **Hybrid option (c) already addressed.** Audit's option (c)
    proposed distinguishing "wrong directory" from "empty directory";
    in fact the enum already does this. `src/modes/reference_mode.cpp:850-852`
    returns `ReferenceError::ReferenceLoadFailed` when the path is
    not a directory; `:861-862` returns `NoTracksFound` only when
    the directory exists but is empty (or contains only non-audio
    files). The two cases give distinct error codes — option (c) is
    a no-op.
  - **Streaming reference workflow** (option (b) hypothetical) does
    not exist in the codebase. There is no async / poll-for-files /
    watcher pattern around `analyze_reference_mode`; the call at
    `src/main.cpp:127` is synchronous one-shot.
  - **Generalization for future enum sweeps.** The "algorithm-finds-
    nothing-legitimately" vs "user-misconfigured-input" distinction
    is the real axis. NoGapsFound was the former; NoTracksFound is
    the latter. Future enum-value adjacent sweeps after an M-8-style
    cure should classify each candidate along this axis rather than
    cure all "no Foo found" enum values reflexively. (Recorded in
    this BACKLOG entry's resolution, not promoted to a feedback
    memory — single example so far; promote if a second instance
    fires.)
- **Files touched.** None (paperwork-only close). Original audit
  finding was the right thing to file as an investigation item;
  investigation concluded no code change is appropriate.
- **Tier rationale.** Tier 6 (API hygiene). Same tier as M-8 because
  the surface is enum-value-as-error-vs-outcome classification.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` — the
  finding surfaced during M-8 audit on a different function in the
  same enum-classification axis but a separate file. Filing as its own
  Tier 6 item rather than folding into M-8 preserved single-function
  scope and let the investigation proceed without rushing the M-8
  merge — pattern confirmed productive.

### M-11 — LoadResult default-constructed state is ambiguous

- **Defect.** Default ctor sets error but also default-constructs the value.
- **Invariant established.** "No default construction leaves a result
  wrapper in an ambiguous state."
- **Files touched.** Resolved by M-14.
- **Exit criteria.** Closed as a duplicate of M-14 once M-14 lands.

### Mi-7 — score_gap drops sample_rate — **RESOLVED in #50 (`02eef0c`) via M-7**

- Duplicate of M-7 / same resolution. Cured in PR #50 alongside M-7
  (removal of the `[[maybe_unused]] int sample_rate` parameter from
  `score_gap`).

### NEW-BLIND-GAP — Blind mode returns only 1 split on clear 2-track fixture — **RESOLVED in #48 (`7c0bc4a`)**

- **Defect.** Surfaced by Phase 0.5 at `test_integration.cpp:479` and `:762`
  (post-PR-#23 line drift to `:525` and `:769`; gate identifies tests by
  TEST_CASE name per `docs/known-failing-tests.md`). Blind mode on a clear
  tone + 3s-silence + tone fixture returns only 1 split.
- **Invariant established.** "Blind mode on a clean 2-track fixture with a
  silence ≥ min_gap_seconds returns ≥2 splits."
- **Files touched.** `src/modes/blind_mode.{hpp,cpp}` (parameter rename +
  caller-side estimator). `src/core/music_detection.cpp` was a candidate per
  the original mandate but was not touched: noise-floor estimation works
  correctly; the cure was the score_gap caller passing the wrong reference
  level.
- **Tests added.** Already present; fix made them pass.
- **Exit criteria.**
  - [x] Root cause traced. Identified as a parameter-semantic mismatch
        between `score_gap` (whose 5th parameter `noise_floor_rms` was
        misnamed — the formula `1 - gap_rms / ref` only yields meaningful
        confidence when `ref` is a SIGNAL reference level, not a noise
        floor) and the caller `analyze_blind_mode` (which passed the
        noise-floor estimate). On a fixture where silence dominates the
        signal duration (~42% in the failing fixture), the 10th-percentile
        noise-floor estimate equals the gap RMS by construction; the
        formula degenerates to `1 - 1 = 0`; every detected gap is
        rejected by the `confidence >= 0.6` gate. Empirical diagnostic
        confirmed pre-cure: `tone1 RMS=0.495`, `gap RMS=0.000346`,
        `noise_floor=0.000343`, `score_gap(noise_floor)=0`.
        Counter-evidence that the parameter is semantically a signal
        reference: `tests/test_blind_mode.cpp:21-32` ("Gap scoring based
        on energy") passes 0.5 (the LOUD level of its samples) as the
        5th argument and asserts `score > 0.9` — encoding through
        assertion that the parameter is a signal reference level. The
        caller in `analyze_blind_mode` had been passing the wrong thing.
        Cure: rename `noise_floor_rms → signal_reference_rms` in the
        score_gap signature with a 25-line docstring documenting the
        formula's actual semantics + the previous bug; in
        `analyze_blind_mode`, compute `signal_reference_rms` as the p90
        of sorted frame RMS values (sits in the music region for any
        fixture where music ≥ 10% of signal duration — true of all
        realistic vinyl rips) and pass that to score_gap. Noise-floor
        estimation, threshold computation, and detect_gaps unchanged.
        Dispatched as two-audit per `feedback_audit_cardinality_two_axes.md`
        (sharp-hook flagged on algorithm-semantic shift); both audits
        returned CLEAN with merge.
  - [x] Two integration tests pass — actual gating tests are
        `Blind mode pipeline: gap detection` (`test_integration.cpp:492`,
        previously-failing assertion at `:525`) and `Combined workflow:
        reference then blind analysis` (`:712`, assertion at `:769`).
        Note: original BACKLOG criterion text said "(`clear silence
        detection`, `combined workflow`)" — `clear silence detection`
        at `:528` uses soft `if`-conditional checks rather than hard
        CHECKs, so it never failed the binary even pre-cure
        (cross-doc reconciliation slip caught by audit-1 and tightened
        in this paperwork commit). Both gating tests pass post-merge:
        empirical CI baseline `7c0bc4a` reports
        `test_integration: 11/11 cases / 73 assertions, all pass` on
        every CI variant (Linux Debug/Release, macOS Debug/Release,
        sanitizers). **First fully-green `test_integration` binary in
        the remediation cycle.** `clear silence detection` (`:528`)
        and `split point positions` (`:577`) also pass post-cure with
        their soft conditions firing correctly.

### M-REF-RATE-VALIDATION — `analysis_to_native_sample` precondition checks compile out in Release — **RESOLVED in #49 (`3e20e26`)**

- **Origin.** Surfaced as audit-2 finding F3 during C-4 dispatch
  (2026-05-04). Latent — not yet exercised in production. Filed
  per close-followups-before-next-epic rule
  (`feedback_close_followups_before_next_epic.md`) so the finding
  doesn't carry into Tier 6 dispatch as a vestigial.
- **Defect.** `src/modes/reference_mode.cpp` (post-C-4): the
  `analysis_to_native_sample` helper guards preconditions with
  `assert(native_sr > 0); assert(analysis_sr > 0);`. C++ runtime
  `assert` compiles out in Release builds (`NDEBUG` defined), so a
  future caller passing zero or negative `analysis_sr` would trigger
  integer division-by-zero (UB) instead of a clean abort. Production
  callers in `analyze_reference_mode` derive these from
  `AudioBuffer::sample_rate` and the function-parameter `analysis_sr`
  (default 22050) — current call paths are safe by construction, but
  the helper's contract is enforced asymmetrically across build types
  while it lives in the public header.
- **Invariant established.** "`analysis_to_native_sample`'s
  precondition checks are enforced symmetrically in Debug and
  Release builds; zero or negative sample rates produce a documented
  abort or error path, never integer division-by-zero UB."
- **Files touched.** `src/modes/reference_mode.cpp`,
  `src/modes/reference_mode.hpp`,
  `tests/test_reference_mode.cpp`.
- **Tests added.**
  - `analysis_to_native_sample: zero or negative sample rate is
    rejected, not divided by` (new; Release-mode UB-adjacent
    regression test — must run under both Debug and Release CI
    lanes to verify symmetry).
- **Tier rationale.** Tier 6 (API hygiene): the helper is exposed in
  the public header `src/modes/reference_mode.hpp` for unit-test
  access (per C-4's audit-2 precedent-setting decision). A public
  API with debug-only precondition validation is API-hygiene-class.
- **Cure options.**
  - (a) Promote `assert` to unconditional check + `std::abort()` on
        invalid input. Simple, matches the helper's `noexcept`
        signature, smallest blast radius.
  - (b) Change return type to `std::optional<int64_t>`; callers
        handle invalid-rate paths explicitly. Wider blast radius
        (every caller updates).
  - (c) Validate at the helper's callers in `analyze_reference_mode`
        only; leave the helper's contract debug-only-checked but
        document explicitly that callers must pre-validate. Smaller
        code change; weaker guarantee. Inconsistent with public-
        header exposure.
  Pick one when M-REF-RATE-VALIDATION dispatches; document choice in
  the PR body.
- **Out of overlap.** Distinct from C-4 (establishes the helper and
  its rounding behaviour) and from `DRIFT-MODEL-RATE-TRUNCATION`
  (extends C-4's helper to `src/core/drift_model.cpp`'s three sites).
  M-REF-RATE-VALIDATION addresses the helper's precondition contract,
  orthogonal to the conversion semantics.
- **Effort.** ≤ 15 lines of code + 1 unit test. One PR, one audit.
- **Exit criteria.**
  - [x] `analysis_to_native_sample`'s precondition checks fire in
        Release builds, not just Debug. Cure: replaced
        `assert(native_sr > 0); assert(analysis_sr > 0);` at
        `src/modes/reference_mode.cpp:801-802` with
        `MWAAC_ASSERT_PRECONDITION(...)` from
        `src/core/audio_file.hpp:46-53` (Release-effective —
        `std::terminate()` with `[[unlikely]]`; Debug — `assert((cond))`).
        Cure option (a)-variant chosen: matches the project's existing
        precondition convention (introduced by C-2), reuses the
        `[[unlikely]]` hint, preserves the function's `noexcept`
        signature (std::terminate is noexcept-compatible). Header
        docstring at `src/modes/reference_mode.hpp:60-69` updated to
        explain the asymmetric-assert defect, name the macro, and
        reaffirm the noexcept-with-terminate contract.
        Audit-1 ran both Debug and Release variants empirically and
        confirmed the cure path fires under both modes.
  - [x] New unit tests exercise the invalid-rate paths. Three new
        death-test TEST_CASEs at `tests/test_reference_mode.cpp` (file
        end after the M-9 case): one each for `native_sr == 0`,
        `analysis_sr == 0`, `native_sr < 0`. Per-precondition
        TEST_CASEs (not SECTIONs) so a regression that re-introduces
        raw `assert()` on only one precondition fails with isolated
        attribution. Fork-based scaffolding (`#if defined(__unix__) ||
        defined(__APPLE__)` + `prepare_child_for_death_test`) duplicated
        from `tests/test_audio_file.cpp:14-21,748-770` per
        `feedback_tier_boundary_preservation.md` — second instance
        documents the pattern; F-AUDIT2-DT (BACKLOG.md) remains the
        proper home for shared-harness extraction when a third use
        surfaces. Audit-noted asymmetry: no `analysis_sr < 0` case;
        acceptable because both parameters take the same macro at
        adjacent source lines and `native_sr < 0` exercises the
        negative-rate code path. 8 cases / 56 assertions total in
        test_reference_mode post-cure; all pass on every CI variant.
  - [ ] Header-side comment in `src/modes/reference_mode.hpp`
        documents the precondition-enforcement guarantee
        (Debug + Release symmetric).

---

## Tier 7 — TUI invariants

### Mi-8 — TUI marker nudge breaks start≤end invariant — **RESOLVED in #58 (`0980606`)**

- **Defect.** `tui/app.cpp:191–209` `+`/`=`/`]` handlers incremented
  both `start_sample` and `end_sample` by 1 with NO bounds clamping;
  `-`/`_`/`[` handlers had a single `start_sample > 0` guard but no
  clamping against sibling markers or `total_samples`.
- **Invariant established.** "For every SplitPoint:
  `0 ≤ start_sample ≤ end_sample ≤ total_samples - 1`" — preserved
  trivially because nudges shift both edges by the same delta
  (block-shift semantics; see Mi-MARKER-NUDGE-SEMANTIC below for the
  audit-2 UX-implication finding). Cross-marker no-gap invariant
  `markers[i].end_sample < markers[i+1].start_sample` from the
  blind_mode / reference_mode pipelines is also maintained by the
  sibling clamp.
- **Cure shape.** New file pair `src/tui/app_handlers.{hpp,cpp}` —
  establishes the state-mutator harness Mi-8 / Mi-9 BACKLOG entries
  identified as a prerequisite. Pure free functions
  `nudge_marker_right(AppState&)` / `nudge_marker_left(AppState&)`
  in `mwaac::tui` namespace; FTXUI event-handler closures in
  `app.cpp` collapse to one-line dispatches. Mutators no-op on
  degenerate input (empty split_points, out-of-range selected_marker,
  empty audio) and on attempts to nudge past the global or sibling
  bounds (refuses-not-saturates to preserve `duration_samples()`).
- **Files touched.** `src/tui/app.cpp` (handler bodies reduced to
  dispatches), `src/tui/app_handlers.hpp` (NEW — mutator
  declarations), `src/tui/app_handlers.cpp` (NEW — mutator
  implementations), `tests/test_app_handlers.cpp` (NEW — second
  test target linking mwaac_tui after `test_waveform`, with 11
  TEST_CASEs covering normal/clamped/degenerate paths +
  duration-preservation invariant), `CMakeLists.txt` (wired new
  source + test target).
- **Tests added.** 11 TEST_CASEs in `tests/test_app_handlers.cpp`.
  Audit-1 classified 7 as Mi-8 regression-guards (fail with cure
  reverted) and 4 as invariant locks / defensive documentation
  (pass pre-cure); classification annotated in the test-file
  header comment per audit-1 finding 2.
- **Audit-cardinality.** Two-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook single
  (small cure), blast-radius flagged on the first state-mutator
  harness establishing precedent for Mi-9 and beyond. Both audits
  returned CONCERNS with explicit "merge + file separately"
  recommendations:
  - Audit-1: cursor_col adjacent finding (filed as Mi-CURSOR-COL-CLAMP
    below) + test-classification clarity note (annotated post-merge
    in test-file header).
  - Audit-2: block-shift vs boundary-shift UX semantic discovery
    (filed as Mi-MARKER-NUDGE-SEMANTIC below for user judgment).
- **Exit criteria.**
  - [x] Nudge handlers clamp against sibling markers and global
        limits. Implementation in `src/tui/app_handlers.cpp`; tests
        confirm the cure path for all clamp boundaries (global upper,
        global lower, sibling upper, sibling lower, last-marker-no-next).

### Mi-CURSOR-COL-CLAMP — TUI ArrowRight cursor_col unbounded increment — **RESOLVED in #60 (`55b2aa4`)**

- **Origin.** Surfaced during Mi-8 audit-1 (PR #58 audit-agent
  finding 1, 2026-05-19). Adjacent-entry sweep in `src/tui/app.cpp`
  flagged `:259` `cursor_col++` as same-defect-class to Mi-8 (TUI
  state-mutator bounds-clamping omission).
- **Defect.** `src/tui/app.cpp:259` (ArrowRight handler) increments
  `cursor_col` with no upper bound; `ArrowLeft` at `:255` correctly
  clamps the lower bound at 0 via `std::max(0, cursor_col - 1)`.
  Pre-cure: indefinite right-arrow presses produce `cursor_col`
  values that overflow the terminal width and the audio sample
  count; the value flows into `render_waveform` as `cursor_pos`
  parameter at `:62` of app.cpp.
- **Reachability dormancy.** Not exploited today because the cursor
  is used for visual display only (the bug doesn't corrupt state
  beyond the cursor position itself); render_waveform's per-column
  loop bounds it by `peaks.size() == width`, so an out-of-range
  cursor just doesn't show. But the unbounded-mutation shape is
  the same defect class Mi-8 cured for split-point markers.
- **Invariant established.** "0 ≤ cursor_col ≤ display_width - 1
  (or whatever upper bound the TUI imposes); ArrowRight clamps
  against the upper bound symmetrically to ArrowLeft."
- **Files touched.** `src/tui/app_handlers.{hpp,cpp}` (add
  `move_cursor_right(AppState&, int display_width)` mutator;
  symmetric extraction to Mi-8's nudge pattern). `src/tui/app.cpp`
  (ArrowRight handler dispatches to new mutator). `tests/test_app_handlers.cpp`
  (new TEST_CASEs for the cursor-clamp invariant).
- **Tier rationale.** Tier 7 (TUI invariants). Same shape as Mi-8.
- **Effort.** ≤ 20 LOC + 2-3 TEST_CASEs.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` —
  the finding surfaced during Mi-8 audit-1 on a different handler
  in the same tier and file. Filing as its own item rather than
  expanding Mi-8 scope preserves the single-defect-per-PR cycle
  pattern. May be dispatched after Mi-9 since `cursor_col` shares
  some state with view bounds and a unified mutator pass might be
  more efficient — decide at dispatch.
- **Audit-cardinality.** Single-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook clear
  (pure adoption of well-established Mi-8/Mi-9 harness pattern); blast-
  radius small (5 LOC mutator + 1-line dispatch + 6 TEST_CASEs). Audit
  retried once after transient API 529; returned CLEAN.
- **Cure shape divergence from Mi-8/Mi-9 pattern.** `cursor_col` lives
  in `run_tui`'s local scope (NOT in AppState), so the mutator
  signatures take `int& cursor_col` rather than `AppState&`. FTXUI's
  `Terminal::Size()` query stays in app.cpp; the mutator itself is
  pure (no FTXUI dependency in the test path). The asymmetric
  extraction (left side is extraction-only since pre-cure already
  clamped; right side is extraction + clamp-fix) is documented in
  the cure comment.
- **Exit criteria.**
  - [x] ArrowRight handler clamps `cursor_col` against display_width
        upper bound. Implementation: `cursor_col = std::min(cursor_col + 1,
        std::max(0, display_width - 1));` in
        `src/tui/app_handlers.cpp:move_cursor_right`. The `std::max(0, ...)`
        defends against degenerate `display_width <= 0` (terminal
        width 1 produces `dimx - 2 = -1` in the handler).
  - [x] Mutator extracted into `app_handlers.{hpp,cpp}`. Same pattern
        as Mi-8's `nudge_marker_*` modulo the by-reference signature
        adaptation.
  - [x] Tests in `test_app_handlers.cpp` exercise both ends of the
        cursor range. 6 new TEST_CASEs: 4 regression-guards (would
        FAIL with cure reverted) + 2 invariant locks / documentation.

### Mi-MARKER-NUDGE-SEMANTIC — block-shift vs boundary-shift cure semantic (Mi-8 audit-2 UX discovery) — **RESOLVED in #62 (`d20c899`) with boundary-shift re-cure**

- **Origin.** Surfaced during Mi-8 audit-2 (PR #58 audit-agent,
  2026-05-19). The cure landed in #58 satisfies the Mi-8 invariant
  ("0 ≤ start ≤ end ≤ total - 1" + sibling clamp) faithfully — but
  the audit's invariant trace discovered a UX implication that
  warrants user judgment.
- **Defect (UX, not correctness).** Mi-8's cure preserves the
  pre-cure **block-shift semantic**: a `+` nudge shifts both
  `start_sample` and `end_sample` of the selected marker by 1
  sample, leaving adjacent markers untouched. Combined with the
  cure's sibling-clamp (don't push end past next.start - 1) and
  blind_mode's algorithmic-output invariant
  (`end[i] = start[i+1] - 1`, i.e. gap-of-exactly-1 between adjacent
  tracks), the cure makes interior markers in blind-mode output
  **universally no-op** on both `+` and `-` nudges: nudging right
  requires gap-of-2 ahead (only first marker on a non-adjacent
  layout can move right; only last marker can move left). The
  feature is preserved for reference-mode (where tracks are
  independently positioned and gaps are data-dependent) but
  degraded for blind-mode's dominant workflow.
- **Two cure-shape options.**
  - **(a) Block-shift (current, landed in #58).** Nudge shifts the
    entire track block as a unit; gaps between tracks grow or
    shrink as a side effect. Refuses-to-nudge rather than altering
    gaps below 1 sample. Invariant: every track's duration is
    preserved across nudges. **Cost**: on blind-mode output (the
    dominant cycle workflow), interior markers cannot be nudged at
    all because the algorithmic-output gap is exactly 1.
  - **(b) Boundary-shift (alternative).** Nudge moves the BOUNDARY
    between adjacent tracks; `markers[i].end_sample` and
    `markers[i+1].start_sample` shift in lockstep. Adjacent tracks
    resize. Invariant: every nudge preserves the total covered range
    and the inter-track gap; durations of the two adjacent tracks
    change inversely. **Cost**: more complex cure (must mutate two
    markers per nudge, with bounds checks against both edges); the
    "selected marker" semantic becomes ambiguous (does `+` move the
    boundary AFTER the selected marker, or BEFORE?).
- **What needs user input.** Which semantic matches your intent for
  marker editing? Block-shift is the conservative continuation of
  pre-cure behavior; boundary-shift is what a user familiar with
  DAW track-boundary editing would expect. The BACKLOG Mi-8 entry
  as originally written did not disambiguate; the cure went with
  block-shift to preserve the pre-cure behavior modulo clamping.
- **Possible outcomes.**
  - (a) **Block-shift confirmed.** Close as INVESTIGATE-only;
    document the gap-of-1 → no-op trade-off in
    INV-MARKER-NUDGE-BOUNDS as expected behavior; future users
    can work around by inserting a gap before nudging.
  - (b) **Boundary-shift authorized.** Re-cure Mi-8 with the
    boundary-shift semantic. Estimated effort: ~50 LOC + tests
    (the harness from #58 still applies; just different mutator
    logic).
  - (c) **Hybrid.** Add a key-combination (e.g. `Shift+`/`Shift+-`)
    for the alternative semantic. Most flexible; most complex.
- **Tier rationale.** Tier 7 (TUI invariants). Same tier as Mi-8.
- **Effort.** Investigation: ~10 minutes (user decision). Cure (if
  any): bounded by outcome above.
- **Filed timing.** Per `feedback_escalation_framing_governance_not_technical.md`
  — surface as audit-verdict + recommendation; user adjudicates.
  Filed as a separate item rather than blocking Mi-8 PR per
  audit-2's explicit "do NOT block this PR" recommendation. The
  Mi-8 cure is correct as specified; this item resolves the
  underspecification.
- **Resolution (2026-05-20).** User authorized option (b)
  **boundary-shift** via AskUserQuestion. Re-cure landed in PR #62
  (`d20c899`):
  - `src/tui/app_handlers.cpp`: rewrote `nudge_marker_right` and
    `nudge_marker_left` from block-shift to boundary-shift. `+`/`-`
    on selected marker N moves the BOUNDARY between
    `markers[N-1]` and `markers[N]`. Both `markers[N-1].end_sample`
    and `markers[N].start_sample` shift by ±1 in lockstep. Adjacent
    track durations change inversely. Inter-track gap preserved.
  - First marker (`N == 0`) no-ops — no boundary before file start.
  - Refusal: zero-duration collapse on the marker whose duration
    would shrink (selected on right-nudge; previous on left-nudge).
  - 11 Mi-8 TEST_CASEs (block-shift assertions) rewritten as 12
    boundary-shift TEST_CASEs. Audit-1 follow-ups in same PR added
    2 more (last-allowed-step coverage on both directions) +
    updated stale call-site comment in `app.cpp`.
  - Two audits CLEAN (cure correctness + semantic match). Two
    optional follow-up items filed (see below — both are forward-
    looking defensive design questions, not bugs).
- **Re-cure follow-ups (audit-2 optional findings).**
  - **Mi-NUDGE-EVIDENCE-STALENESS** — boundary-shift leaves
    `prev.evidence` and `sel.evidence` maps describing the pre-edit
    sample ranges (the evidence was attached by the algorithmic
    pipeline at the time the marker was constructed). Audit-2
    classified as "descriptive provenance, not a coordinate
    contract" — nothing in `src/` consumes evidence post-export.
    Filed for documentation only.
  - **Mi-NUDGE-DEFENSIVE-TOTAL-CLAMP** — audit-2 noted that the
    re-cure dropped Mi-8's `total_samples - 1` clamp. The hole is
    unreachable through any current code path (no TUI mutator
    extends `sel.end_sample`; algorithmic pipelines cap at
    `total - 1`). Forward-compat precondition assertion would
    catch a future end-extending mutator. Filed for forward-compat.

### Mi-NUDGE-EVIDENCE-STALENESS — boundary-shift leaves marker evidence describing pre-edit ranges — **RESOLVED in #63 (`9d4d124`)** via option (b) (clear-on-nudge)

- **Origin.** Surfaced during Mi-MARKER-NUDGE-SEMANTIC audit-2
  (PR #62, 2026-05-20).
- **Defect (descriptive, not invariant).** When the user nudges
  marker boundaries via `+`/`-`, the cure shifts `prev.end_sample`
  and `sel.start_sample` but does NOT touch either marker's
  `evidence` map. The evidence was attached by the algorithmic
  pipeline at the time the marker was constructed (e.g.
  `blind_mode.cpp:255-256` sets `gap_start_frame` /
  `gap_end_frame` per the original gap location). After several
  nudges, the evidence keys point to sample positions outside
  the current marker range — they describe where the gap *was
  found*, not where the marker *is now*.
- **Reachability.** No consumer in `src/` reads `evidence` post-
  export — `export_tracks` (`src/tui/app.cpp:310`) writes
  `start_sample`/`end_sample` to the output WAV file only;
  `reaper_export.cpp` similarly reads positions, not evidence.
  So the staleness is cosmetic / for future tooling.
- **Possible outcomes.**
  - (a) Document in `INV-SPLITPOINT-ORDER` or `split_point.hpp`
    that evidence is descriptive provenance from the original
    algorithmic detection and may become stale under TUI editing.
    Paperwork-only close.
  - (b) Clear evidence on every nudge (treat any edit as
    invalidating the algorithmic provenance). Minor cost; consumer
    impact depends on whether future tooling needs the evidence.
  - (c) Update evidence keys to track the post-edit positions.
    More work; questionable value if nothing reads it.
- **Tier rationale.** Tier 7 (TUI hygiene) or Tier 8 (documentation),
  depending on outcome chosen.
- **Effort.** Investigation: ~10 min. Cure (if needed): ≤ 10 LOC.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` —
  the audit surfaced the question as descriptive provenance during
  the boundary-shift re-cure; not a blocker.
- **Status / Resolution.** RESOLVED via Mi-NUDGE-FOLLOWUPS close-out
  PR #63 (commit `9d4d124`). Cure direction: option (b) (clear-on-nudge),
  chosen by user via AskUserQuestion 2026-05-20. Both
  `nudge_marker_right` and `nudge_marker_left` in
  `src/tui/app_handlers.cpp` now call `prev.evidence.clear()` and
  `sel.evidence.clear()` after the successful boundary-shift commit;
  refused nudges (`selected_marker == 0` no-op; would-collapse
  refusal) `return` before the commit lines, so evidence is preserved
  on refused calls. The cure treats any user edit of a marker
  boundary as invalidating the algorithmic provenance, so post-nudge
  the evidence map is either empty (cleared by user edit) or in-sync
  with the current marker range (set at construction by
  `blind_mode.cpp` / `reference_mode.cpp`, never touched). Locked by
  three regression-guard TEST_CASEs in `tests/test_app_handlers.cpp`
  (clears-on-success right, clears-on-success left, refused-preserves
  covering both directions and both refusal modes) plus a
  `split_point.hpp` comment on the `evidence` field and an
  enforcement bullet in `docs/invariants.md` INV-SPLITPOINT-ORDER.

### Mi-NUDGE-DEFENSIVE-TOTAL-CLAMP — forward-compat: re-cure dropped Mi-8's total_samples clamp — **RESOLVED INVESTIGATE-only** via option (b) (defer; invariant chain holds via algorithmic pipelines)

- **Origin.** Surfaced during Mi-MARKER-NUDGE-SEMANTIC audit-2
  (PR #62, 2026-05-20).
- **Defect (latent / forward-compat).** Mi-8's block-shift cure
  included a `total_samples - 1` clamp on `nudge_marker_right` and
  a `0` lower-bound clamp on `nudge_marker_left`. The boundary-shift
  re-cure dropped both, relying on the algorithmic pipelines'
  invariant that markers always sit within `[0, total_samples - 1]`
  (blind_mode.cpp:268-270, main.cpp:303-305 cap end_sample at
  `total - 1`; reference_mode similarly).
- **Reachability dormancy.** Unreachable through current code paths:
  - No TUI mutator extends `sel.end_sample` (the boundary-shift
    `nudge_marker_right` only grows `prev.end` and `sel.start` by 1
    in lockstep; the global edges of `sel` and `prev` are untouched).
  - Algorithmic pipelines guarantee `markers[*].end_sample <= total - 1`
    at construction time.
- **Forward-compat concern.** If a future TUI mutator extends
  marker edges (e.g., "stretch marker" feature), the dropped clamp
  becomes a real hole. A precondition assertion on the boundary-
  shift mutator (`MWAAC_ASSERT_PRECONDITION(sel.end_sample <
  total_samples)` per the Mi-3-family pattern) would catch the
  invariant violation at the bridge boundary rather than producing
  out-of-buffer offsets downstream.
- **Possible outcomes.**
  - (a) Add the precondition assertion now (~5 LOC). Defensive;
    documents the invariant chain for future readers.
  - (b) Defer until a future end-extending mutator is filed. The
    invariant chain holds today; assertion adds noise.
- **Tier rationale.** Tier 7 (TUI invariants). Same tier as Mi-8.
- **Effort.** ≤ 10 LOC if option (a).
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` —
  forward-compat audit finding; not a current bug.
- **Status / Resolution.** RESOLVED INVESTIGATE-only via
  Mi-NUDGE-FOLLOWUPS close-out PR #63 (commit `9d4d124`). Cure
  direction: option (b) (defer assertion), chosen by user via
  AskUserQuestion 2026-05-20. No code change. Rationale: the
  invariant `markers[*].end_sample <= total_samples - 1` is
  established at construction by `blind_mode.cpp:268-270`,
  `main.cpp:303-305`, and the reference_mode equivalents. No current
  TUI mutator extends `sel.end_sample` — `nudge_marker_right` only
  shifts `prev.end` and `sel.start` in lockstep by ±1, leaving
  `sel.end` invariant; `nudge_marker_left` is symmetric. The
  forward-compat hole reopens if-and-only-if a future TUI mutator
  extends marker edges (e.g., a hypothetical "stretch marker"
  feature). Re-open trigger: file a new entry if any end-extending
  TUI mutator is filed.

### Mi-9 — TUI view bounds can invert — **RESOLVED in #59 (`815f278`)**

- **Defect.** `tui/app.cpp:208–241` zoom + pan handlers performed
  ad-hoc clamping. On empty audio (`total_samples == 0`): `zoom_out`
  and `pan_*` produced `view_start == view_end == 0` (zero range,
  violates INV-VIEW-NON-INVERTED's strict-less-than). `zoom_in`
  didn't clamp `view_end` against `total_samples` and computed
  arithmetic that could yield negative view_start values.
- **Invariant established.** INV-VIEW-NON-INVERTED:
  `0 ≤ view_start < view_end ≤ total_samples`. Strict-less-than
  enforced by construction in the new normalization helper.
- **Cure shape (per BACKLOG exit criterion "post-handler
  normalization helper").** 4 new mutators in `app_handlers.{hpp,cpp}`
  reusing the Mi-8 harness pattern (zoom_in, zoom_out, pan_to_start,
  pan_to_end). Each mutator computes a proposed (view_start, view_end)
  and commits via the new `commit_normalized_view(state, proposed_start,
  proposed_end, total)` helper that:
  - No-ops on empty audio (`total <= 0`); no valid view exists
  - Clamps `proposed_end` to `[1, total]` THEN `proposed_start` to
    `[0, proposed_end - 1]` (order matters — clamping start first
    against end could pin it at total - 1 even when caller wanted a
    tighter view)
  - Strict-less-than `view_start < view_end` guaranteed by
    construction (post-clamp `end >= 1`, `start <= end - 1`).
- **Files touched.** `src/tui/app_handlers.hpp` (4 new mutator
  declarations), `src/tui/app_handlers.cpp` (mutator implementations
  + private `commit_normalized_view()` + `resolve_view_range()`
  helpers), `src/tui/app.cpp` (4 event-handler closures collapsed to
  one-line dispatches; same pattern as Mi-8), `tests/test_app_handlers.cpp`
  (7 new TEST_CASEs).
- **Sentinel handling.** `AppState::view_end{0}` is the "auto-stretch
  to file end" sentinel (resolved at the Renderer site,
  `src/tui/app.cpp:40` — `view_end > 0 ? view_end : total_samples`).
  The mutators resolve the sentinel BEFORE any mutation logic via
  `resolve_view_range(state, total)`. **Sentinel-once consumption**:
  after any successful mutator commit, `view_end >= 1` (the literal
  sentinel is consumed). The user cannot return to the auto-stretch
  state via these four handlers — only to an equivalent explicit
  `[0, total)` range via `pan_to_start + zoom_out`-until-capped or
  `pan_to_end`. Audit-1 of #59 flagged this as semantic-doc-worthy
  but not a defect (the resolved view is observably equivalent).
- **Tests added.** 7 new TEST_CASEs in `tests/test_app_handlers.cpp`
  (total now 18 = 11 Mi-8 + 7 Mi-9):
  - Normal-behavior: zoom_in halves, zoom_out doubles with cap,
    pan_to_start jumps to [0, range), pan_to_end mirror.
  - Empty-audio regression-guards on all four mutators (pre-cure
    would produce inverted/zero-range view; post-cure no-ops).
  - INV-VIEW-NON-INVERTED holds across a 200-call (50 outer ×
    4 mutators) mutation sequence — locks the post-normalization
    guarantee against future regression.
  - view_end == 0 sentinel resolution test.
- **Audit-cardinality.** Single-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook clear
  (pure adoption of Mi-8's established harness pattern; pre-cure
  bugs are well-spec'd); blast-radius small (extends existing TU
  pair, no new files). Audit returned CONCERNS with 3 LOW findings,
  all merge-acceptable per audit's recommendation:
  - Finding 1: `zoom_out` near-boundary clamp-shift produces
    narrower-than-requested range. Pre-cure same shape (no
    regression). Filed as **Mi-VIEW-ZOOM-BOUNDARY-SHIFT** below.
  - Finding 2 (governance): orchestrator-self attribution of a
    "150 LOC / two-domains" split heuristic to Mi-8 audit-2.
    Heuristic exists in Mi-8 audit-2's result message but was NOT
    promoted to BACKLOG.md or memory; treating it as ratified
    governance was a slip. Captured as new memory rule
    `feedback_audit_suggestions_need_ratification.md`.
  - Finding 3: sentinel-once consumption undocumented in INV.
    Addressed in this close-out (above).
- **Exit criteria.**
  - [x] Post-handler normalization helper. See
        `commit_normalized_view()` in `src/tui/app_handlers.cpp`;
        called by all four mutators on commit; enforces
        INV-VIEW-NON-INVERTED by construction.

### Mi-VIEW-ZOOM-BOUNDARY-SHIFT — `zoom_out` near boundaries produces narrower-than-requested range (Mi-9 audit-1 finding 1) — **RESOLVED in #61 (`60c23ff`)**

- **Origin.** Surfaced during Mi-9 audit-1 (PR #59 audit-agent
  finding 1, 2026-05-19).
- **Defect (UX, not invariant).** When `zoom_out` computes a new
  range that would extend past the audio boundaries, the
  `commit_normalized_view` helper clamps the offending edge but
  does NOT shift the opposite edge to preserve the intended range.
  Example: `cur_view = [0, 100), total = 100000`, `zoom_out` targets
  range = 200 (doubled); `center = 50`, `new_start = 50 - 100 = -50`,
  `new_end = 50 + 100 = 150`. After clamp: `new_start = 0` (clamped
  up from -50), `new_end = 150` (untouched). Effective range = 150
  instead of the requested 200. Symmetric issue at the
  `cur_end` near `total` boundary.
- **Reachability dormancy.** Pre-cure `zoom_out` had the SAME shape
  (`std::max<int64_t>(0, center - new_range / 2)` then
  `std::min(start + new_range, total)`). Mi-9 cure preserved
  behavioral parity — this is not a Mi-9 regression. Affects only
  zoom-out near audio boundaries; INV-VIEW-NON-INVERTED holds
  either way (strict-less-than guaranteed by clamp construction).
- **Invariant established.** "zoom_out preserves the user-requested
  range whenever it fits within `[0, total_samples]`; near boundaries,
  the range shifts (clamped edge anchors, opposite edge moves to
  preserve range) rather than narrowing."
- **Cure shape.** Extend `commit_normalized_view` (or add a sibling
  `commit_normalized_view_preserving_range`) to: if `proposed_start`
  is clamped up, shift `proposed_end` by the same delta and re-clamp;
  if `proposed_end` is clamped down, shift `proposed_start` by the
  same delta. ~10 LOC + 2 TEST_CASEs.
- **Files touched.** `src/tui/app_handlers.cpp`, `tests/test_app_handlers.cpp`.
- **Tier rationale.** Tier 7 (TUI invariants). Same tier as Mi-9.
- **Effort.** ≤ 20 LOC + 2 TEST_CASEs.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md` —
  the finding surfaced during Mi-9 audit on the same file/function
  but a different defect surface (UX-shape, not invariant-shape).
  Filing as own item rather than expanding Mi-9 scope per audit's
  explicit "ACCEPT for Mi-9, file follow-up" recommendation.
- **Audit-cardinality.** Single-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook clear
  (pure extension of Mi-9's `commit_normalized_view`); blast-radius
  small (~12 LOC + 4 TEST_CASEs). Audit returned CLEAN on all 4 axes.
- **Cure shape.** Shift-shift policy in `commit_normalized_view`:
  detect each edge's overflow before the final clamp pass and shift
  the opposite edge by the same delta. Order matters (left-then-right)
  so requested range > total collapses cleanly to `[0, total]` after
  the second shift triggers the final clamp.
- **Exit criteria.**
  - [x] `zoom_out` near `[0, total]` boundary produces a view with
        the requested range (not a narrowed range), with `view_start`
        or `view_end` pinned at the boundary as appropriate.
        Implementation: shift-shift block in
        `src/tui/app_handlers.cpp:commit_normalized_view`.
  - [x] TEST_CASEs exercise both boundary cases (start at 0, end
        at total). Both regression-guards present in
        `tests/test_app_handlers.cpp`; verified to FAIL with cure
        reverted (audit-1 axis 4).

### Tier 7 cleanup-tail close (Mi-MARKER-NUDGE-SEMANTIC close-out)

Tier 7 originally-planned items closed 2026-05-19 across PRs #56
(M-WAVEFORM-CLAMP-UB), #57 (Mi-10), #58 (Mi-8), #59 (Mi-9). Dispatch-
tail follow-ups closed 2026-05-20 across PRs #60 (Mi-CURSOR-COL-CLAMP),
#61 (Mi-VIEW-ZOOM-BOUNDARY-SHIFT), and #62 (Mi-MARKER-NUDGE-SEMANTIC
re-cure of Mi-8 with boundary-shift semantic per user judgment).

**Total Tier 7 cleanup: 8 PRs.** All originally-planned items closed.
All dispatch-tail follow-ups closed. Both forward-looking items
filed during the Mi-MARKER-NUDGE-SEMANTIC audit also closed in
PR #63 (Mi-NUDGE-FOLLOWUPS, 2026-05-20, commit `9d4d124`): Mi-NUDGE-
EVIDENCE-STALENESS RESOLVED via option (b) (clear-on-nudge); Mi-NUDGE-
DEFENSIVE-TOTAL-CLAMP RESOLVED INVESTIGATE-only via option (b) (defer
assertion; invariant chain holds via algorithmic pipelines). User
ratified both cure directions via AskUserQuestion. **Tier 7 closes
zero-residue.**

### Mi-10 — run_tui exit-code documentation — **RESOLVED in #57 (`f359e19`)**

- **Defect.** `tui/app.cpp:271` returned `quit ? 0 : 1`. The `quit`
  sentinel was set only by the 'q'/'Q' handler at `:154-158`, so
  Ctrl-C and any other non-Q exit path returned 1, inverting the
  documented contract.
- **Invariant established.** "run_tui returns 0 on normal exit
  (Q, Ctrl-C); non-zero only on initialization failure." Per
  Mi-10 audit-1 (PR #57), the "non-zero only on init failure"
  clause is vacuously satisfied — FTXUI's `Fullscreen()` and
  `Loop()` are best-effort with no throwing failure modes
  (verified against vendored FTXUI source, zero `throw` in
  screen_interactive.cpp), so initialization failure manifests
  as a degraded loop rather than a non-zero return.
- **Files touched.** `src/tui/app.cpp` (cure: `:271` returns 0
  unconditionally; `quit` sentinel removed; cure-rationale comments
  added to the Q-handler and the return statement),
  `src/tui/app.hpp` (docstring rewritten to make the contract
  explicit — audit-1 catch softened the FTXUI throw-path claim).
- **Audit-cardinality.** Single-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook clear
  (trivial 2-LOC behavioral cure + docstring), blast-radius small
  (2 file edits). Audit-1 CONCERNS (docstring overspecified FTXUI
  throw semantics; cure itself CLEAN) — fixed in-PR before merge.
- **Tests added.** None. `run_tui` blocks on terminal input via
  `screen.Loop()` which cannot be invoked from a unit test without
  the headless TUI state-mutator harness Mi-8 / Mi-9 will need.
  Cure is verification-by-code-review; INV-RUN-TUI-EXIT-CODE
  becomes test-enforceable when the Mi-8/Mi-9 harness lands.
  Documented explicitly in INV doc and in this BACKLOG entry.
- **Exit criteria.**
  - [x] Header docstring restated. See `src/tui/app.hpp:35-46`
        post-cure — contract is now explicit about both the
        always-0 return and the FTXUI no-throw initialization
        semantics.
  - [x] Exit code matches doc. `src/tui/app.cpp:271` returns 0
        unconditionally; documented intent and actual behavior
        aligned.

### M-WAVEFORM-CLAMP-UB — `render_waveform` `std::clamp` UB on `height == 1` input — **RESOLVED in #56 (`3650fe2`)**

- **Origin.** Adjacent-entry sweep during M-9 pre-dispatch checklist
  (2026-05-04). Same defect class as M-9 (empty-container `std::clamp`
  with `hi = container_size - 1` going to -1 when size is 0) but in a
  TUI code path rather than reference-mode. Cross-tier finding;
  filed under Tier 7 (TUI invariants) per cycle's tier-boundary
  discipline rather than expanded into Tier 5's algorithmic-
  correctness scope. Cycle precedent on tier-boundary preservation:
  C-3 deferred from Tier 5 because it's Tier-4-shape; same logic
  here in the opposite direction.
- **Defect.** `src/tui/waveform.cpp:68-69` —
  ```
  min_row = std::clamp(min_row, 0, waveform_height - 1);
  max_row = std::clamp(max_row, 0, waveform_height - 1);
  ```
  When `waveform_height == 0`, `hi = -1 < lo = 0`; per cppreference,
  `std::clamp` with `hi < lo` is undefined behavior. Reachable input
  trace: `render_waveform(..., height=1, ...)` passes the early-return
  guard at `:53` (`if (peaks.empty() || height <= 0) return {};`),
  computes `waveform_height = height - 1 = 0` at `:59`, then enters
  the per-column loop at `:61` and trips the UB at `:68`/`:69`. The
  `height == 1` case is a degenerate but valid input (very small TUI
  pane); the guard at `:53` admits it.
- **Reachability dormancy.** TUI tests are absent on main as of
  2026-05-04 (per Mi-8 and Mi-9 BACKLOG entries which both note
  "TUI tests are currently absent; add a headless unit test at the
  state-mutator level"); the UB has not been observed in production
  use because (a) production TUI panes are typically much taller
  than 1 row, and (b) sanitizers don't run against TUI render paths
  without a test harness. Cured here to prevent the defect surfacing
  the moment Tier 7's TUI test infrastructure exists.
- **Invariant established.** "`render_waveform` does not invoke
  `std::clamp` with `hi < lo` for any valid `height > 0` input;
  the `waveform_height == 0` degenerate case is guarded explicitly."
- **Files touched.** `src/tui/waveform.cpp`, plus the Tier 7 TUI
  test harness (new file or per-tier test target — exact wiring
  depends on what Tier 7's TUI test infrastructure work establishes;
  Mi-8 and Mi-9 BACKLOG entries both call out the same harness gap).
- **Tests added.**
  - `render_waveform: height == 1 does not invoke std::clamp with
    hi < lo` (new; engages the degenerate-height path with peaks
    non-empty). Sanitizer-clean run is the primary signal; functional
    assertion is on the returned rows being well-formed.
- **Cure shape (cycle precedent).** Mirrors M-9's pattern: top-of-
  loop guard before the std::clamp call, or pre-loop guard that
  early-returns if `waveform_height == 0`. Two valid shapes:
  - (a) Tighten the early-return at `:53` to also guard
    `height < 2` (since `height == 1` produces zero usable waveform
    rows). Single-line change; semantically: "render_waveform returns
    empty for inputs that have no waveform rows to draw."
  - (b) Guard inside the loop at `:67` with `if (waveform_height == 0)
    continue;` (or break — the entire loop iterates over peaks but
    produces no per-column rows when waveform_height is 0).
  Pick one when M-WAVEFORM-CLAMP-UB dispatches; (a) is the cleaner
  contract (fail-fast at the input boundary).
- **Tier rationale.** Tier 7 (TUI invariants): the cure lives in
  `src/tui/`, the test depends on TUI test infrastructure that
  Tier 7 establishes (Mi-8, Mi-9 both depend on the same harness),
  and the invariant ("render_waveform on degenerate height returns
  cleanly") is TUI-shape, not algorithmic-correctness shape. Cross-
  tier expansion into Tier 5 was considered and rejected per
  tier-boundary discipline; user-adjudicated.
- **Out of overlap.** Distinct from M-9 (same defect class, different
  file/area, different test surface). M-9's cure on
  `src/modes/reference_mode.cpp:1048` is independent of this; both
  items use the same guard pattern but cure different code paths.
  Distinct from Mi-8 / Mi-9 (which address other TUI invariants —
  marker nudge bounds, view bounds inversion). Distinct from
  Mi-10 (TUI exit-code documentation).
- **Effort.** ≤ 5 lines of code (single guard) + 1 unit test. One
  PR, one audit. Test infrastructure dependency adds work if
  Tier 7's TUI harness is not yet established when this dispatches.
- **Audit-cardinality.** Single-audit per
  `feedback_audit_cardinality_two_axes.md` — sharp-hook clear (third
  instance of established defensive-cure pattern from M-9 / M-10;
  cure is a single-line guard widening) and blast-radius small (5 LOC
  cure + 1 new additive test file + 1 CMakeLists line). Audit
  returned CLEAN on all 4 axes (cure correctness, test correctness,
  sibling sweep, paperwork forward-look).
- **Pre-dispatch sibling sweep — family fixed point reached.** Standing
  grep across `src/` for `std::clamp(` patterns surfaced 5 sites in
  addition to the cured one; audit independently re-verified each
  classification:
  - `src/core/correlation.cpp:232` — safe (uses
    `std::max(int64_t{0}, max_valid_lag)` to ensure `hi >= 0 >= lo`).
  - `src/modes/blind_mode.cpp:92` — safe (constants `0.0f, 1.0f`).
  - `src/modes/reference_mode.cpp:147` — safe (enclosed in
    `extra > SMALL_GAP` branch which guarantees
    `natural_end_excl > ref_end`, so `hi > lo`).
  - `src/modes/reference_mode.cpp:1124` — safe (M-9 function-entry
    early-return at `:915-917` guards against `vinyl.samples.empty()`).
  - `src/tui/waveform.cpp:77-78` — cured by this PR.
  No new same-class sites; the `std::clamp` UB cure family (M-9,
  M-10, M-WAVEFORM-CLAMP-UB) has reached fixed point.
- **Exit criteria.**
  - [x] `src/tui/waveform.cpp:68-69` no longer invoke `std::clamp`
        with `hi < lo` for any reachable input. Implementation:
        input-boundary guard at `:53` widened from `height <= 0` to
        `height < 2`; the `height == 1` path now returns empty before
        entering the per-column loop.
  - [x] Guard added per (a) (fail-fast at the input boundary —
        `render_waveform` requires `height >= 2` to produce any
        usable output). Cure choice documented in the PR body and
        in the cure comment at `src/tui/waveform.cpp:53`.
  - [x] New unit test exercises `render_waveform(..., height=1, ...)`
        with non-empty peaks; sanitizer-clean run (CI `sanitizers
        (asan+ubsan)` job pass); functional assertion `rows.empty()`.
        See `tests/test_waveform.cpp` "M-WAVEFORM-CLAMP-UB regression
        test".
  - [x] No regression in `render_waveform`'s behavior for `height >= 2`
        inputs — exercised by the new `height == 2` no-regression test;
        also confirmed by full 13/13 ctest suite passing locally and
        in CI (no existing test failures introduced).

### Tier 7 — TUI infrastructure follow-up notes (from M-WAVEFORM-CLAMP-UB close-out)

- **First mwaac_tui test target landed.** PR #56 added the first test
  target (`test_waveform`) to link `mwaac_tui`. The wedge is scoped
  to pure-function tests of `render_waveform`; it does NOT establish
  the headless state-mutator harness that Mi-8 / Mi-9 BACKLOG entries
  call for. Future Tier 7 dispatches for Mi-8 / Mi-9 will need to
  introduce a separate harness (FTXUI provides a screen abstraction
  that can be driven without a real terminal — exact design deferred
  to those items).

---

## Tier 8 — Documentation, attribution, hygiene

### M-12 — FFTW3 is dead — **RESOLVED INVESTIGATE-only in #64 (`760f19e`)** via T8-PAPERWORK-SWEEP

- **Defect.** Already resolved in Phase 0.3 (CMake + CI). Close on
  audit-agent verification.
- **Status / Resolution.** RESOLVED INVESTIGATE-only 2026-05-21 via
  T8-PAPERWORK-SWEEP. Verified empirically against the working tree:
  the only occurrence of `FFTW` / `fftw` in the build system is
  `CMakeLists.txt:122`, an explanatory comment confirming the
  dependency was explicitly excluded ("of this project used FFTW3 —
  keep that out of the dependency graph"). No live `find_package`,
  `target_link_libraries`, or `include_directories` reference to
  FFTW remains. Phase 0.3 cure stands.

### M-13 — pocketfft attribution — **RESOLVED in #65 (`4e1bedd`)** via M-13-POCKETFFT-ATTRIBUTION

- **Defect.** Vendored pocketfft_hdronly.h has no LICENSE/attribution in-tree.
- **Invariant established.** "Every third-party file in-tree is accompanied
  by attribution satisfying its license."
- **Files touched.** `THIRD_PARTY_LICENSES.md` (new), `README.md`
  (acknowledgments).
- **Exit criteria.**
  - [x] pocketfft BSD-3 text reproduced; author + URL listed.
        *Exit criterion met by `THIRD_PARTY_LICENSES.md` containing
        the full multi-party copyright lines (Max-Planck-Society
        2010-2024, Peter Bell 2019-2020, Matteo Frigo + MIT 2003 /
        2007-14 for DCT-IV, Tan Ping Liang + Peter Bell 2024 for
        prev_good_size, Cris Luengo 2024 for safeguards), the
        authors line ("Martin Reinecke, Peter Bell"), the upstream
        URL (`https://gitlab.mpcdf.mpg.de/mtr/pocketfft`), and the
        complete BSD-3-Clause license text reproduced verbatim from
        the file header.*
- **Status / Resolution.** RESOLVED in #65 (commit `4e1bedd`) via
  M-13-POCKETFFT-ATTRIBUTION close-out. `THIRD_PARTY_LICENSES.md`
  created at repository root;
  `README.md` Acknowledgments section gains a bullet pointing to
  `THIRD_PARTY_LICENSES.md`. Verified empirically: pocketfft is the
  only in-tree file with a Copyright header (`find src -name '*.h*'
  -exec grep -l 'Copyright' {} \;` returns only `src/core/pocketfft_hdronly.h`),
  so the invariant "Every third-party file in-tree is accompanied by
  attribution satisfying its license" is satisfied in full by this
  one entry. Externally-fetched dependencies (Catch2, FTXUI,
  libsndfile) are not vendored and are linked from the README
  acknowledgments rather than reproduced in `THIRD_PARTY_LICENSES.md`,
  consistent with the "vendored" qualifier in the invariant text.
- **Adjacent finding (filed-separately).** Surfaced during this
  dispatch's pre-cure state check: `README.md:205` references a root
  `LICENSE` file ("MIT — see [LICENSE](LICENSE).") that does not
  exist in the repository tree. Filed as `T8-LICENSE-FILE-MISSING`
  under Tier 8 below per `feedback_tier_boundary_preservation.md`
  in-tier-but-different-defect filing rather than fold-in (the
  missing-LICENSE concern is project-self attribution, distinct from
  M-13's third-party-vendored attribution).

### Mi-5 — Magic threshold soup in reference mode — **RESOLVED in #67 (`348ede5`)**

- **Defect.** `reference_mode.cpp:584–585` (and ~12 other sites — actual
  empirical site count was broader, see Resolution below) have
  unexplained numeric thresholds.
- **Invariant established.** "Every decision threshold is a `constexpr` at
  top of translation unit with a comment citing the observation or corpus
  that produced it."
- **Files touched.** `src/modes/reference_mode.cpp`.
- **Exit criteria.**
  - [x] No magic numbers remain in the per-track loop bodies.
- **Status / Resolution.** Cure landed on branch `mi-5-magic-thresholds`
  (PR pending audit + merge as of 2026-05-31). User-ratified strict scope
  (promote ALL decision thresholds in `reference_mode.cpp`, not just the
  per-track loop bodies) over conservative (only the BACKLOG-cited
  `584–585` cluster).
  - **45 file-scope `constexpr` declarations total: 1 pre-existing
    (C-4's `kAnalysisToNativeRoundingTolerance` at line 30) + 44 new
    Mi-5 promotions in the catalog block immediately below it.** Many
    constants are used at multiple sites; the catalog count counts
    *constants*, not *sites*. (Per PR #67 audit: catalog table sums to
    6+5+6+2+3+9+5+8 = 44 new; `grep -cE "^static constexpr"` returns
    45 total. Initial PR-body narration said "43 new" — off by one;
    corrected here.)
  - **Catalog structure (semantic groups, top-to-bottom):**
    - Track-end / flip-gap detection: `kTrackEndSmallGapSeconds`,
      `kTrackEndTailCapSeconds`, `kTrackEndFlipMinSilenceSeconds`,
      `kTrackEndTailPadSeconds`, `kTrackEndSilenceFloorDb`,
      `kTrackEndMinSilenceRunSeconds` (6).
    - Lead-in fade-detection: `kFadeInMaxSearchSeconds`,
      `kFadeInMinFadeSeconds`, `kFadeInMaxFadeSeconds`,
      `kFadeInSteadyStateStartFrame`, `kFadeInTargetRatioMinus10Db` (5).
    - RMS envelope / envelope-refine: `kEnvelopeDefaultFrameMs`,
      `kEnvelopeRefineDefaultRadiusSeconds`,
      `kEnvelopeRefineCallsiteRadiusSeconds`,
      `kEnvelopeRefineMinTrackSeconds`, `kEnvelopeRefineMinConfidence`,
      `kEnvelopeRefineMaxShiftSeconds` (6).
    - Digital-silence / noise-floor: `kDigitalSilenceLinearThreshold`,
      `kNoiseFloorPercentile` (2).
    - Find-music-onset (multi_snippet_refine call): `kMusicOnsetSearchSeconds`,
      `kMusicOnsetThresholdDb`, `kMusicOnsetMinSustainMs` (3).
    - Snippet correlation / multi-snippet voting: `kSnippetDefaultSeconds`,
      `kSnippetVoteRadiusSeconds`, `kSnippetVote2PositionNum/Den`,
      `kSnippetVote3PositionNum/Den`, `kVoteConfidenceMin`,
      `kVote1TrustConfidence`, `kVote23RecoveryAgreementSeconds` (9).
    - `align_per_track` inline policy: `kAlignMinCorrelationConfidence`,
      `kAlignCoarseMarginSeconds`, `kAlignCoarseDownsampleFactor`,
      `kAlignMultiRefineAcceptConfMin`,
      `kAlignMultiRefineWeakSpreadSeconds` (5).
    - `skip_leading_silence` defaults + in-loop overrides:
      `kSkipSilenceDefault{ThresholdDb,MinSkipSeconds,MaxSkipSeconds,MinMusicMs}`,
      `kAlignSkipSilence{ThresholdDb,MinSkipSeconds,MaxSkipSeconds,MinMusicMs}` (8).
  - **Citation comments** are ported from the existing in-source
    explanations (e.g., `// -10 dB` next to `0.316`, the inline
    rationales above `compute_track_ends`'s local block, the comment
    above the downsample-factor 100). Where no inline rationale existed,
    the catalog comment describes the semantic role and the
    empirical-corpus citation is deferred for a later audit pass per
    the dispatch mandate.
  - **Sites NOT promoted (formula / math constants):**
    - `10.0` and `20.0` in `std::pow(10.0, db / 20.0)` (dB-to-linear
      conversion math, 3 sites).
    - `0.1` and `10` in `fade_end_s = fade_end_frame * 0.1` /
      `pre_frames = min_fade_seconds * 10` (100 ms-frame to seconds
      conversion math).
    - `sample_rate / 10`, `sample_rate / 100`, `sample_rate / 20`
      (frame-size derivation from sample rate at 100/10/50 ms).
    - `den / 2` rounding bias in `analysis_to_native_sample` (C-4
      round-half-away-from-zero formula).
    - `-120.0` empty-window fallback in `estimate_noise_floor_db` and
      `1e-9` log-domain safety floor: defensive defaults rather than
      tunable thresholds, and the function is `[[maybe_unused]]`
      (dormant).
    - Time-formatting `/ 60` and `% 60` (seconds-to-minutes).
  - **Behavior preservation evidence.** ctest 14/14 binaries pass on
    `build-mi-5` Debug after the refactor (same as pre-cure baseline).
    Refactor is mechanical; the existing test suite is the
    regression-guard.

### DOC-1 — README "sample-accurate" claim reconciliation — **RESOLVED INVESTIGATE-only in #64 (`760f19e`)** via T8-PAPERWORK-SWEEP

- **Defect.** README uses "sample-accurate" in a context where the code
  rounds ±1 native-rate sample (post-C-4 fix).
- **Invariant established.** "Every README claim is either enforced by a
  test or rewritten to match behavior."
- **Files touched.** `README.md`.
- **Exit criteria (original).**
  - [~] Claim reworded to match the tolerance guaranteed by C-4's new test.
        *Original spec called for a strict rewording. User-judgment
        close on T8-PAPERWORK-SWEEP redirected to INVESTIGATE-only —
        see Status / Resolution below.*
- **Status / Resolution.** RESOLVED INVESTIGATE-only 2026-05-24 via
  T8-PAPERWORK-SWEEP. User judgment via AskUserQuestion: the README's
  "sample-accurate" descriptor is preserved as written. Rationale:
  - **Line 43 is self-qualifying.** "The alignment gets most tracks
    sample-accurate, but difficult material (gradual fade-ins,
    continuous DJ mixes, heavy rhythmic repetition) sometimes lands
    a second or two off." The "but … sometimes lands a second or two
    off" clause is in the same sentence and qualifies the
    "sample-accurate" descriptor against the worst case. The user-facing
    claim is therefore not unqualified.
  - **Line 19 is user-facing rhetorical language.** The "sample-accurate"
    in the "Reference mode" bullet is the README's high-level
    description of intent; the technical specification of the tolerance
    (≤ 1 native-rate sample for the algorithmic path, larger for
    difficult material) lives in `docs/invariants.md` under
    INV-RATECONV-ROUNDED (`docs/invariants.md:312`) where tech-spec
    readers go.
  - **The "reword to match" invariant ("Every README claim is either
    enforced by a test or rewritten to match behavior") is honored**
    by treating line 43's in-sentence hedge as the test-bridge: the
    claim that survives ("most tracks sample-accurate, some lands a
    second or two off") IS matched by behavior. Strict rewording was
    determined to be over-precise for user-facing prose.
  - **Re-open trigger.** If the README's intent or tolerance description
    becomes misleading (e.g., a future change widens the tolerance
    substantially, or removes the self-qualifying clause on line 43),
    file a new DOC-1-style entry. Tolerance text in
    `docs/invariants.md` INV-RATECONV-ROUNDED is the source of truth
    for tech-spec readers; README is intentionally looser.
- **Files touched (close-out).** None. INVESTIGATE-only close;
  README.md preserved as-is.

### DOC-2 — PROJECT_SPEC.md reconciliation — **RESOLVED in #66 (`cd98294`)** via DOC-2-SPEC-RECONCILIATION

- **Files touched.** `PROJECT_SPEC.md`.
- **Exit criteria.** Spec and CMakeLists.txt agree on warning flags, standard,
  and dependencies.
- **Status / Resolution.** RESOLVED in #66 (commit `cd98294`) via
  DOC-2-SPEC-RECONCILIATION close-out. `PROJECT_SPEC.md` updated on
  the three axes named by the
  exit criterion:
  - **Standard** — already agreed pre-cure (`PROJECT_SPEC.md` line 64
    "C++20 or C++23"; `CMakeLists.txt:5` sets `CMAKE_CXX_STANDARD 20`).
    No change required.
  - **Warning flags** — Quality-Gates bullet rewritten from the
    shorthand `-Wall -Wextra -Werror` claim to the full non-MSVC
    warning set actually applied by the `mwaac_apply_flags` helper
    (12 flags), plus the MSVC equivalent (`/W4 /permissive-`), plus
    the `MWAAC_WERROR` toggle semantics and the scope note that the
    warning set is applied to first-party targets only (not
    third-party `FetchContent` deps). Also added the `MWAAC_SANITIZE`
    option (the dedicated CI sanitizer job) which the spec was silent
    on.
  - **Dependencies** — "(suggested)" subsection retitled to "(actual,
    post-implementation)" and rewritten to match `CMakeLists.txt`:
    libsndfile (system pkg-config preferred, FetchContent 1.2.2
    fallback, normalized through `mwaac_sndfile` interface target);
    pocketfft vendored in-tree (with cross-reference to
    `THIRD_PARTY_LICENSES.md` per M-13 close-out); FTXUI FetchContent
    v5.0.0; Catch2 FetchContent v3.5.2. Removed FFTW3/KissFFT (M-12
    closed out; explicitly excluded per `CMakeLists.txt:120-122`).
    Removed Eigen (originally suggested as optional; never
    implemented; correlation/drift/envelope code operates directly
    on `std::vector<float>` / `std::span<const float>` without a
    matrix library).
- **Adjacent finding (filed-separately).** Surfaced during pre-cure
  state check: the spec's Architecture section (`PROJECT_SPEC.md`
  lines 73-91) cites a `src/` layout that doesn't match the actual
  tree — `src/cli/`, `src/utils/`, `alignment.hpp`, `editor.hpp`
  don't exist; `audio_buffer`, `drift_model`, `music_detection`,
  `reaper_export`, `app_handlers`, `pocketfft_hdronly.h`,
  `split_point.hpp` aren't listed; `split_points.hpp` is singular in
  the actual tree (`split_point.hpp`). Outside the strict DOC-2 exit
  criterion (which names warning flags, standard, and dependencies).
  Filed as `T8-SPEC-ARCH-DRIFT` below per
  `feedback_tier_boundary_preservation.md` in-tier-but-different-defect
  filing rather than fold-in (the architecture-drift cure has multiple
  user-judgment options: match-spec-to-tree, reorganize-tree-to-spec,
  or remove-diagram-entirely).

### DOC-3 — docs/invariants.md living document — **RESOLVED INVESTIGATE-only in #64 (`760f19e`)** via T8-PAPERWORK-SWEEP

- **Invariant established.** "Every invariant named in this backlog has an
  entry in docs/invariants.md citing the enforcement site(s)."
- **Files touched.** `docs/invariants.md` (new).
- **Exit criteria.** File exists, maintained by invariant-agent every 3–5
  completed items.
- **Status / Resolution.** RESOLVED INVESTIGATE-only 2026-05-21 via
  T8-PAPERWORK-SWEEP. Verified empirically: `docs/invariants.md`
  exists (61 KB, ~1090 lines), with 30+ `INV-*` entries covering
  every real invariant named in BACKLOG.md. Cross-referenced the
  BACKLOG-side set of `INV-*` identifiers
  (INV-BLIND-SINGLE-TRACK, INV-INDEX-TYPE-DISJOINT, INV-RUN-TUI-EXIT-CODE,
  INV-SPLITPOINT-ORDER, INV-VIEW-NON-INVERTED, INV-RF64-*) against
  the docs/invariants.md `### INV-*` headers — every real invariant
  has an entry citing enforcement sites. One counterfactual outlier
  (INV-MARKER-NUDGE-BOUNDS, BACKLOG.md:1908) is a forward-referenced
  name from the discarded "block-shift confirmed" branch of the
  Mi-MARKER-NUDGE-SEMANTIC decision tree — the user chose boundary-
  shift (option b), so this name never landed as a real invariant;
  the actual cure landed under INV-SPLITPOINT-ORDER which is present
  in docs/invariants.md. The living-document discipline has been
  exercised across Tier 5/6/7 (12+ Status flips and enforcement-bullet
  additions through the cycle), confirming the per-3-to-5-item
  maintenance cadence.

### T8-CLANG-TIDY-BASELINE — allowed-red clang-tidy baseline carried across Tier 5/6/7 — **RESOLVED in #73 (`c5939d1`)** via T8-CLANG-TIDY-BASELINE-CLEANUP option (a)

- **Origin.** Filed 2026-05-21 during T8-PAPERWORK-SWEEP (#64,
  `760f19e`). The
  clang-tidy CI job has been failing on `main` since at least
  Tier 7 open (PR #56, 2026-05-19) and is documented as
  allowed-red in the cycle's merge-gate precedent (5/6 green
  excluding clang-tidy on every Tier 7 PR #56–#63).
- **Defect (project hygiene, not correctness).** The clang-tidy
  workflow runs `clang-tidy -p build --warnings-as-errors='*'`
  across `src/**/*.cpp` and produces errors on multiple source
  files. The 5 other CI jobs (build × Linux/macOS × Debug/Release,
  plus asan+ubsan sanitizers) remain green throughout. No production-
  correctness impact; this is a style-quality gate.
- **Affected files (14, from PR #63 audit log).**
  - `src/core/analysis.cpp`, `src/core/audio_buffer.cpp`,
    `src/core/audio_file.cpp`, `src/core/correlation.cpp`,
    `src/core/drift_model.cpp`, `src/core/music_detection.cpp`,
    `src/core/test_deps.cpp`, `src/main.cpp`,
    `src/modes/blind_mode.cpp`, `src/modes/reaper_export.cpp`,
    `src/modes/reference_mode.cpp`, `src/tui/app.cpp`,
    `src/tui/app_handlers.cpp`, `src/tui/waveform.cpp`.
- **Top error classes by volume (~28 distinct classes total in log;
  top-4 shown).**
  - `readability-braces-around-statements` — 72 sites. Single-line
    `if`/`for`/`while` bodies should be braced.
  - `readability-uppercase-literal-suffix` — 58 sites. Float literals
    with lowercase `f` suffix (`0.0f`, `0.5f`).
  - `modernize-return-braced-init-list` — 41 sites. `return T{...}`
    should be `return {...}` where the type is deducible.
  - `modernize-use-auto` — 39 sites. `T x = static_cast<T>(...)`
    initializations should be `auto x = static_cast<T>(...)` (verified
    ≥6 sites in `app_handlers.cpp` alone).
  - Other classes present in the log include
    `cppcoreguidelines-avoid-c-arrays` (13),
    `readability-implicit-bool-conversion` (9),
    `modernize-use-emplace` (5),
    `cppcoreguidelines-pro-type-reinterpret-cast` (5),
    plus ~20 long-tail classes. Full per-class breakdown to be refreshed
    at cure-dispatch time against then-current `main` log.
- **Reachability.** None of these classes affect runtime behavior;
  all are style-only diagnostics. The cycle's merge-gate has been
  treated as "5/6 green excluding clang-tidy" per
  `feedback_halt_on_red_baseline.md` gate-eval (single-check
  allowed-red is not "across the board").
- **Possible outcomes.**
  - (a) Mechanical batch-fix across all affected files. Single PR;
    audit cardinality likely two (sharp-hook sharp; blast-radius
    medium across ~10 source files). ~50 LOC mechanical edits.
  - (b) Loosen the clang-tidy config (`NOLINT` per site, or disable
    the noisy checks in `.clang-tidy`). Faster, lower-value;
    silences the signal rather than fixing it.
  - (c) Status quo: keep the allowed-red baseline. No code change.
- **Tier rationale.** Tier 8 (project hygiene). Same shape as
  DOC-1/DOC-2/DOC-3 (doc / hygiene work, no correctness impact).
- **Effort.** ≈ 250 LOC mechanical fix-up across 14 files for option (a)
  (revised from initial ≤ 50 LOC estimate per PR #64 audit finding —
  initial estimate undercounted both file list (10→14) and class
  taxonomy (3 named → ~28 in log)).
- **Filed timing.** Per `feedback_close_followups_before_next_epic.md`
  the discovery was captured during PR #63 gate-eval and filed at
  Tier 8 open (this PR) so the scope is tracked. Cure dispatch
  deferred until the documented Tier 8 items (M-13, Mi-5, DOC-2)
  close, to keep T8-PAPERWORK-SWEEP minimal-scope.
- **Audit-cardinality (forward).** When dispatched: two-audit by
  `feedback_audit_cardinality_two_axes.md` because blast-radius is
  medium (~10 files touched, multiple error classes).
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block; the cycle's allowed-red baseline note in
  `project_tier5_state.md` is updated to "no longer allowed-red".
- **Status / Resolution.** RESOLVED via T8-CLANG-TIDY-BASELINE-CLEANUP,
  option (a) mechanical batch-fix across all affected files. Pre-cure
  baseline: ~270 `[*,-warnings-as-errors]` diagnostics across the 14
  files. Post-cure local clang-tidy on `src/**/*.cpp` (excluding
  vendored `pocketfft_hdronly.h` per workflow's `-not -path
  '*/pocketfft*'`): **0 errors**, matching the CI workflow's expected
  exit status (**after PR #73 audit-fix-up** — initial commit `c41cb2c`
  shipped with one `readability-implicit-bool-conversion` site at
  `reference_mode.cpp:983` that Ubuntu CI's clang-tidy caught but the
  fix-agent's local Homebrew LLVM 20 missed due to degraded
  `<cstdint>`/`<cctype>` resolution. Both parallel audits independently
  converged on the same single-site HALT. Per
  `feedback_fix_agent_stale_baseline.md` extended to a new tool axis:
  fix-agent's "local clang-tidy 0 errors" claim was not verified
  against CI's Ubuntu toolchain before PR-body propagation. Fix-up
  commit `ede6782` adds `!= 0` to convert the `std::isdigit` int
  return to bool explicitly. Audit 2 also caught a stale `(dead;
  Mi-11 pending deletion)` annotation at `PROJECT_SPEC.md:94` —
  cross-doc drift per `feedback_cross_doc_reconciliation.md`; removed
  in the same fix-up. **Post-fix-up CI: 6/6 green including clang-tidy
  — first all-green PR since the allowed-red baseline was set in
  PR #56 (2026-05-19).**)
  14/14 ctest binaries pass on `build-clang-tidy/` Debug
  config (no test modifications). Files modified: the 13 remaining
  `.cpp` files from the BACKLOG list (test_deps.cpp deleted; see Mi-11
  below), plus `src/core/audio_file.hpp` (signature change matched a
  ctor body fix-up; see "header touched" note).
  - **Cure technique mix.**
    - Auto-fix via `clang-tidy --fix --fix-errors` cleared the four
      high-volume mechanical classes
      (`readability-braces-around-statements`,
      `readability-uppercase-literal-suffix`,
      `modernize-return-braced-init-list`,
      `modernize-use-auto`) plus several long-tail classes
      (`modernize-use-ranges`, `modernize-use-designated-initializers`,
      `readability-implicit-bool-conversion`,
      `readability-math-missing-parentheses`,
      `modernize-use-integer-sign-comparison`,
      `cppcoreguidelines-init-variables`,
      `modernize-use-emplace`, `modernize-pass-by-value`, etc.).
    - **Hand-corrections.** `modernize-use-integer-sign-comparison`
      auto-fix produced malformed expressions at 3 sites
      (`reference_mode.cpp`, `app.cpp`, `app_handlers.cpp`) by losing
      a `static_cast` wrapping paren; corrected manually preserving
      the `std::cmp_*` rewrite (the cure-intent of the check). Build
      failure caught the fault; post-cure ctest 14/14.
    - **C-array → std::array conversion (audio_file.cpp magic
      bytes).** 11 file-scope `static constexpr uint8_t NAME[]`
      declarations became `static constexpr std::array<uint8_t, 4>
      NAME`. `compare_bytes` helper retyped to take
      `std::span<const uint8_t>` (drops the explicit `len` param at 17
      call sites). Added `<span>` include. One additional c-array site
      in `audio_file.cpp` (`std::byte float80[10]`) and one in
      `reaper_export.cpp` (`static const char hex[] = "..."`)
      converted to `std::array<std::byte, 10>` and
      `std::string_view` respectively.
    - **`bugprone-misplaced-widening-cast` (2 sites).** Cast moved
      inside the addition: `static_cast<int64_t>(chunk_offset + 8)` →
      `static_cast<int64_t>(chunk_offset) + 8` (size_t is already
      64-bit on the target platforms so behavior preserved).
    - **`bugprone-implicit-widening-of-multiplication-result` (1
      site).** `kTailWindow = 1 * 1024 * 1024` →
      `size_t{1} * 1024 * 1024` (widens at the first factor).
    - **`performance-enum-size` (1 site).** Local `enum class
      AudioFormat` retyped to `: std::uint8_t` (internal-only enum,
      no ABI concern).
    - **`readability-avoid-nested-conditional-operator` (1 site).**
      Nested ternary `sx < sy ? -1 : (sx > sy ? 1 : 0)` expanded into
      explicit `if`-return sequence.
    - **`clang-analyzer-deadcode.DeadStores` (1 site).** Removed dead
      `fade_end_s = max_fade_seconds;` write in
      `reference_mode.cpp:436` (variable not read after; only
      `fade_end_frame` flows downstream — confirmed by grep). Caught
      by the analyzer; not in the original error-class taxonomy in
      this BACKLOG entry but surfaced once the high-volume mechanical
      noise was removed.
  - **NOLINT annotations added (15 total).** Per check class:
    - `cppcoreguidelines-pro-type-reinterpret-cast` (5 sites) — all
      load-bearing: 3 `fstream::read`/`write` calls on `uint8_t`/
      `std::byte` buffers (binary IO is the canonical Core
      Guidelines exception), 1 `reinterpret_cast<uintptr_t>` for
      intentional pointer-bits-as-entropy in RNG seed
      (`audio_file.cpp:828`).
    - `readability-function-cognitive-complexity` (4 functions) —
      `parse_wav_header`, `main`, `align_per_track`, `run_tui`.
      All are pipeline/wiring functions whose decomposition is a
      separate refactor (annotation explicitly cites "tracked
      separately").
    - `bugprone-exception-escape` (1 site, `main`) — paired with
      cog-complexity NOLINTBEGIN/END block. Wrapping `main` in
      try/catch is the standard cure but out of scope for this
      mechanical fix-up.
    - `bugprone-branch-clone` (3 NOLINTs covering 4 logical sites)
      — explicit default-to-WAV fallback in `write_output_file`
      and 2 placeholder TUI rendering branches in `waveform.cpp`
      (intent is to preserve the structure for future
      glyph/column-format differentiation).
    - `modernize-return-braced-init-list` (1 site,
      `analysis.cpp:140`) — `return std::vector<float>(num_frames,
      0.5F)` cannot be safely rewritten as `return {num_frames,
      0.5F}` because that would be parsed as a 2-element
      `initializer_list<float>` rather than the (size, value)
      constructor.
  - **`src/core/test_deps.cpp` decision (Mi-11).** **Deleted.** The
    file was not referenced by `CMakeLists.txt`, was not compiled
    into any target, and its only content was a header-availability
    probe (`<sndfile.h>`, `<ftxui/component/component.hpp>`,
    `<catch2/catch_test_macros.hpp>`) without any executable code.
    Per Mi-11 ("test_deps.cpp is dead — delete or compile"),
    deletion was the user-ratified-equivalent cleaner option.
    Side-effect: removes the `clang-tidy` `clang-diagnostic-error`
    that the file produced because it was absent from
    `compile_commands.json` (the file had no compile DB entry,
    so clang-tidy could not resolve the FTXUI include path).
    **This PR therefore also resolves Mi-11**; orchestrator
    decides whether to record the Mi-11 closure in the same
    paperwork close-out or split.
  - **Header touched.** `src/core/audio_file.hpp:172` — clang-tidy's
    `modernize-pass-by-value` auto-fix changed the
    `AudioFile::AudioFile` ctor parameter from `const
    std::filesystem::path&` to `std::filesystem::path` (matching
    the existing `: path_(std::move(path))` in the body). One-line
    signature change; the corresponding `.cpp` ctor definition was
    updated in the same auto-fix pass. Mandate authorised
    "you may touch the corresponding .hpp file in the same scope"
    so this fits the scope envelope.
  - **Pre-existing local LLVM-20-only noise.** Brew Homebrew LLVM
    20.1.4 surfaces 5 `clang-analyzer-cplusplus.NewDeleteLeaks`
    warnings against `src/core/pocketfft_hdronly.h:2659,2666,2668,
    2696,2703` (vendored third-party FFT, M-13). These appear in
    BOTH pre-cure and post-cure local logs and are NOT in the CI
    workflow's clang-tidy baseline (the workflow excludes pocketfft
    .cpp via `-not -path '*/pocketfft*'` but the HeaderFilterRegex
    `^src/.*\.(hpp|h)$` does technically match the header; Ubuntu
    CI's older clang-tidy LLVM doesn't enable
    `clang-analyzer-cplusplus.NewDeleteLeaks` by default for this
    code path). Mandate explicitly forbids touching pocketfft;
    these errors are deferred to future tier if Ubuntu CI ever
    starts reporting them.
  - **Pre-dispatch checklist re-verification.**
    - Scope-claim (14 files): verified by `find src -name '*.cpp'
      -not -path '*/pocketfft*'` against working tree.
    - Test-identity (existing tests are the regression-guard):
      verified by ctest 14/14 pass pre-cure and post-cure on
      `build-clang-tidy/` Debug config without test modifications.
    - Cure-attribution sweep on `docs/known-failing-tests.md`:
      Active section remained `(empty)`; no entries to update.
    - Adjacent-entry sweep: Mi-11 (test_deps.cpp) is in-scope per
      mandate explicit allowance; surfaced and closed by this PR.
      No other adjacent items expanded scope.

### T8-LICENSE-FILE-MISSING — root LICENSE file referenced but absent — **RESOLVED in #68 (`90e8fc3`)** via T8-DEFERRED-PAPERWORK-SWEEP option (a)

- **Origin.** Filed 2026-05-26 during M-13-POCKETFFT-ATTRIBUTION
  pre-cure state check (#65, `4e1bedd`). Surfaced via `ls LICENSE LICENSE.md` returning
  no matches at the repository root while `README.md:205` reads
  "MIT — see [LICENSE](LICENSE)."
- **Defect (project hygiene + claim-vs-reality).** The README's
  License section asserts an MIT license and links to a `LICENSE`
  file at the repository root, but no such file exists in the tree.
  The MIT badge (`README.md:4`) makes the same assertion. Readers
  following the link land on a 404 (GitHub) or a missing-file error
  (local checkout). Distinct from M-13 (which is third-party
  vendored attribution); this entry is project-self attribution.
- **Invariant established.** "Every README link to a project file
  resolves to an actual file in the repository tree." (Mirrored
  shape of M-13's third-party invariant — applied to project-self.)
- **Files touched (forward).** `LICENSE` (new, root) OR `README.md`
  (revise claim and remove broken link), depending on cure direction.
- **Possible outcomes.**
  - (a) Add a root `LICENSE` file with standard MIT text + the
    project's author/year. ~25 LOC. Aligns repo with README claim.
  - (b) Revise `README.md` — drop the MIT badge and the License
    section's link, or change the License section to inline-state
    the license without a link. User-judgment territory similar to
    DOC-1's INVESTIGATE-only close (user preferred preserving
    README copy in DOC-1).
  - (c) INVESTIGATE-only close: document the discrepancy without
    cure. Weakest option; leaves the broken link in place.
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Same shape as DOC-1 (claim-vs-reality on user-facing README)
  combined with M-13 (license file attribution).
- **Effort.** ≤ 25 LOC for option (a); ≤ 5 LOC for option (b);
  paperwork only for option (c).
- **Filed timing.** Per `feedback_tier_boundary_preservation.md`
  in-tier-but-different-defect filing — surfaced during M-13
  state-check; filed as own item rather than folded into M-13's
  third-party scope. M-13 closure remains sharp.
- **Audit-cardinality (forward).** Single-audit by both axes.
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED via T8-DEFERRED-PAPERWORK-SWEEP
  close-out, option (a) (add root MIT LICENSE file). New file
  `LICENSE` at repository root contains the canonical MIT license
  text from `https://opensource.org/license/mit` with "Copyright (c)
  2026 Matt Woolly" (year from project's first commit `2026-04-12`;
  author from `git config user.name` + the project's prior README
  acknowledgments line citing `audio-auto-chop` by `mattWoolly`).
  `README.md:4` MIT badge claim and `README.md:205` link
  "MIT — see [LICENSE](LICENSE)" both now resolve to a real file in
  the tree. Adjacent-entry sweep verified no other broken
  README/PROJECT_SPEC relative links — `THIRD_PARTY_LICENSES.md`
  (M-13) and `docs/invariants.md` (DOC-3) both exist and resolve.
  **Audit fix-up note:** PR #68's first commit `ddbd4e7` shipped the
  LICENSE with the widespread X11/classic-MIT variant phrasing
  ("WHETHER IN CONTRACT, TORT OR OTHERWISE..."), which is
  SPDX-recognized as MIT and legally near-equivalent to OSI canonical
  but did NOT match the cited source. Single-audit HALT on
  fidelity-to-cited-source axis; cure: 3-word insertion of "AN ACTION
  OF" so paragraph 3 now reads "WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE..." matching the cited canonical OSI text
  verbatim. Recorded here per cycle pattern (PR #63/#64/#66/#67
  audit-finding inline fix-ups) so future audits see the provenance.

### T8-SPEC-ARCH-DRIFT — PROJECT_SPEC.md architecture diagram doesn't match the actual src/ tree — **RESOLVED in #72 (`070446e`)** via T8-SPEC-ARCH-DRIFT-CLEANUP option (a)

- **Origin.** Filed 2026-05-28 during DOC-2-SPEC-RECONCILIATION
  pre-cure state check (#66, `cd98294`). Surfaced by direct read of `PROJECT_SPEC.md` lines
  73-91 (the "Architecture" code-block diagram) against `ls src/**/*`.
- **Defect (project hygiene + claim-vs-reality).** The spec's
  Architecture section diagrams a `src/` layout that does not match
  the actual repository tree:
  - **Empty placeholder directories (exist but unpopulated; spec
    implies populated):** `src/cli/` and `src/utils/` both exist
    with only `.gitkeep` files (no source files). The spec implies
    CLI parsing files live in `src/cli/` and `dsp.hpp` lives in
    `src/utils/`, but neither directory contains source code.
    These are scaffold leftovers from the project's initial layout
    that never got populated; CLI parsing was ultimately implemented
    directly in `src/main.cpp`, and the DSP helpers were folded
    into `src/core/*` modules.
  - **Files in the spec but NOT in the tree:** `src/utils/dsp.hpp`,
    `src/core/alignment.hpp`, `src/tui/editor.hpp`. (`split_points.hpp`
    is listed in the spec as plural but actually exists as
    `src/core/split_point.hpp` singular — naming drift, not a
    missing file.)
  - **Files in the tree but NOT in the spec diagram:**
    `src/core/audio_buffer.{cpp,hpp}`, `src/core/drift_model.{cpp,hpp}`,
    `src/core/music_detection.{cpp,hpp}`, `src/core/pocketfft_hdronly.h`
    (vendored, M-13), `src/core/test_deps.cpp`,
    `src/modes/reaper_export.{cpp,hpp}` (REAPER integration),
    `src/tui/app_handlers.{cpp,hpp}` (Tier 7 state-mutator harness).
  - **Naming-convention drift:** spec uses `.hpp`-only stubs
    (`reference.hpp`, `blind.hpp`); tree uses `_mode.cpp`+`.hpp`
    pairs (`reference_mode.cpp`, `blind_mode.cpp`).
- **Invariant established.** "Every section of `PROJECT_SPEC.md` is
  either currently true against the working tree, or marked
  explicitly aspirational." (Mirrored shape of DOC-1's
  claim-vs-reality invariant — applied to PROJECT_SPEC.md instead of
  README.md.)
- **Files touched (forward).** `PROJECT_SPEC.md` (Architecture
  section) OR `src/` (reorganize to match spec) — depending on cure
  direction.
- **Possible outcomes.**
  - (a) Match spec to current `src/` tree. Rewrite the Architecture
    code-block to enumerate the actual files/dirs as of cure time.
    Cure-time sub-decision: keep the empty `src/cli/` and `src/utils/`
    placeholder dirs and annotate them as "reserved for future
    expansion" in the diagram, OR remove the empty dirs from disk
    (`git rm`) and from the diagram together. ~30 LOC of doc.
    Simplest; treats spec as descriptive of current state.
  - (b) Reorganize `src/` to match the spec. Populates the existing
    empty `src/cli/` and `src/utils/` placeholder dirs (currently
    `.gitkeep`-only), renames `*_mode.{cpp,hpp}` → `*.{cpp,hpp}`,
    consolidates `app_handlers.cpp` into `editor.hpp` etc. Code
    refactor; large blast-radius across CMakeLists.txt and #include
    paths. Treats spec as authoritative aspiration.
  - (c) Remove the Architecture diagram entirely. The diagram is not
    load-bearing for any tool/agent (the cycle has been running
    against the actual tree, not the spec diagram). Replace with a
    one-line pointer to `ls src/` or to `CMakeLists.txt`'s
    `CORE_SOURCES` / `REFERENCE_SOURCES` / `TUI_SOURCES` lists.
    INVESTIGATE-only-shape close.
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Same shape as DOC-1's claim-vs-reality concern, scoped to the
  spec's architecture section instead of the README's tolerance
  claim.
- **Effort.** ≤ 30 LOC for option (a) or (c); large refactor for
  option (b).
- **Filed timing.** Per `feedback_tier_boundary_preservation.md`
  in-tier-but-different-defect filing — surfaced during DOC-2
  state-check; filed as own item rather than folded because the
  cure direction is user-judgment (3 distinct options with very
  different scope shapes).
- **Audit-cardinality (forward).** Single-audit for options (a) or
  (c); two-audit for option (b) (code-refactor blast-radius across
  CMakeLists.txt + #include paths).
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED in #72 (commit `070446e`) via
  T8-SPEC-ARCH-DRIFT-CLEANUP close-out, option (a) (match spec to
  current tree). User-ratified
  via AskUserQuestion 2026-06-02. The `PROJECT_SPEC.md` Architecture
  section was rewritten to enumerate the actual `src/` tree as of
  cure time:
  - **`src/cli/` and `src/utils/` placeholder dirs preserved.**
    Annotated as `(reserved placeholder; .gitkeep-only)` in the
    diagram. Sub-decision: kept rather than `git rm`'d, matching
    DOC-1's preserve-by-default precedent for benign tree state.
    Notes section below the diagram explains CLI parsing landed
    in `main.cpp` and DSP helpers folded into `src/core/*`.
  - **Tree files added to diagram:** `alignment_result.hpp`,
    `analysis.{cpp,hpp}`, `analysis_result.hpp`,
    `audio_buffer.{cpp,hpp}`, `audio_file.{cpp,hpp}`,
    `audio_info.hpp`, `correlation.{cpp,hpp}`, `drift_model.cpp`,
    `frame_sample_bridge.hpp`, `music_detection.{cpp,hpp}`,
    `pocketfft_hdronly.h` (cross-ref to THIRD_PARTY_LICENSES.md per
    M-13), `split_point.hpp`, `verbose.hpp`, `app_handlers.{cpp,hpp}`
    (Tier 7 state-mutator harness), `reaper_export.{cpp,hpp}`.
  - **Spec stubs removed:** `alignment.hpp`, `editor.hpp`,
    `src/utils/dsp.hpp`, `src/core/split_points.hpp` (plural; the
    actual file is `split_point.hpp` singular).
  - **Naming-convention drift resolved:** `reference.hpp` / `blind.hpp`
    stubs replaced with `*_mode.{cpp,hpp}` matching tree.
  - **Dead-file cross-references:** `core/core.hpp` annotated
    `(dead; Mi-12 pending deletion)`; `core/test_deps.cpp` annotated
    `(dead; Mi-11 pending deletion)`. The annotations make the
    Tier 9 cleanup items discoverable from the architecture diagram.
  - **Legacy-diagram notes section** appended explaining the
    placeholder dirs, the absorbed/never-required stubs, and the
    naming-convention transition. Provides narrative continuity
    for readers of older revisions of PROJECT_SPEC.md.
  - **Drift-detection bias** preserved by leading with "describes
    current state, not aspiration" — future drift between spec
    and tree should be cured by updating this section (matching
    DOC-3's living-document discipline), not by re-introducing
    aspirational stubs.
  - **Audit fix-up note.** PR #72's first commit `4603b22` shipped
    the diagram with two struct-location slips: `drift_model.cpp`
    was annotated "struct in modes/reference_mode.hpp" and
    `reference_mode.{cpp,hpp}` annotated "+ DriftModel struct", but
    `grep -n DriftModel src/` shows `struct DriftModel` is declared
    at `src/core/alignment_result.hpp:9` with zero references in
    `reference_mode.hpp`. Also `alignment_result.hpp` was
    mis-annotated "AlignPerTrackResult struct" — actual contents
    are `AlignmentResult` + `TrackOffset` + `DriftModel`. Single-
    audit HALT on the very claim-vs-reality axis the PR exists to
    close. Fix-up reconciled inline: line 81 annotation lists all
    three structs in alignment_result.hpp; line 89 points
    drift_model.cpp's struct-location to alignment_result.hpp;
    line 99 drops the false DriftModel claim from
    reference_mode.{cpp,hpp}. Same fix-up pattern as PR #68 LICENSE
    fidelity HALT (recorded in BACKLOG so future audits see the
    cure's correction provenance). Mirrors
    `feedback_user_concrete_detail_paraphrase.md` — orchestrator
    paraphrased struct locations from memory rather than greping
    the source; audit's empirical verification caught the slip.

### Mi-5-BLIND — magic threshold soup in `blind_mode.cpp` (Mi-5 sibling) — **RESOLVED in #69 (`1ca7249`)** via Mi-5-BLIND-CLEANUP

- **Origin.** Filed 2026-05-31 during PR #67 (Mi-5) audit. Surfaced
  by audit-agent's adjacent-entry sweep on `src/modes/blind_mode.cpp`
  while verifying Mi-5's `reference_mode.cpp` cure. User-ratified
  filing via AskUserQuestion 2026-05-31.
- **Defect (project hygiene).** `src/modes/blind_mode.cpp` contains
  decision thresholds with the same pattern Mi-5 cured in
  `reference_mode.cpp`: in-body magic numbers without file-scope
  `constexpr` declarations. Audit cited at least two sites:
  `noise_floor * 2.0f` (line ~137, "6 dB above noise floor" gap-detect
  policy) and a `confidence >= 0.6` policy threshold. There are likely
  more — Mi-5's empirical site count (44 promotions) outpaced the
  BACKLOG's "~12 sites" paraphrase by ~3×.
- **Invariant established.** Inherited from Mi-5: "Every decision
  threshold is a `constexpr` at top of translation unit with a comment
  citing the observation or corpus that produced it." Applied to
  `blind_mode.cpp`.
- **Files touched.** `src/modes/blind_mode.cpp`.
- **Exit criteria.** No magic numbers remain in `blind_mode.cpp`
  decision sites (same shape as Mi-5).
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Direct sibling of Mi-5.
- **Effort.** Unknown until empirical site survey; likely 5-15 sites
  (smaller than `reference_mode.cpp`'s 44 given `blind_mode.cpp`'s
  smaller scope and pipeline structure).
- **Filed timing.** Per `feedback_tier_boundary_preservation.md`
  in-tier-but-different-file filing rather than fold-in (Mi-5's mandate
  was strictly `reference_mode.cpp`). Per
  `feedback_audit_suggestions_need_ratification.md`, audit's "file a
  sibling" suggestion was surfaced to user for ratification before
  filing; user ratified.
- **Audit-cardinality (forward).** Single-audit by both axes (sharp-
  hook clear; blast-radius small-to-medium given smaller scope).
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED in #69 (commit `1ca7249`) via
  Mi-5-BLIND-CLEANUP close-out.
  Empirical site survey: 7 sites across 2 files (4 in
  `src/modes/blind_mode.hpp` BlindModeConfig defaults; 3 in
  `src/modes/blind_mode.cpp` in-body). Catalog structure:
  - `blind_mode.hpp` (BlindModeConfig defaults, 4 constants):
    `kBlindDefaultMinGapSeconds` (2.0 s), `kBlindDefaultMaxGapSeconds`
    (30.0 s), `kBlindDefaultConfidenceThreshold` (0.6, score_gap gate
    per NEW-BLIND-GAP), `kBlindDefaultAnalysisSampleRate` (44100 Hz).
    `inline constexpr` at file scope (header) so the struct's member
    defaults can reference them without ODR issues across TUs.
  - `blind_mode.cpp` (in-body decisions, 3 constants):
    `kBlindAnalysisFrameSeconds` (0.05, 50 ms envelope frame),
    `kBlindGapThresholdNoiseFloorMultiplier` (2.0×, 6 dB above noise
    floor), and the percentile pair
    `kBlindSignalReferencePercentileNumerator/Denominator` (9/10 =
    p90 per NEW-BLIND-GAP's "signal reference RMS, not noise floor"
    cure). `static constexpr` at file scope above the first function.
  Citations on each constant ported from existing inline comments
  (`// 50ms`, `// 6 dB above noise floor`, the NEW-BLIND-GAP
  rationale block at lines 139-153 pre-cure) so the comment chain
  matches Mi-5's pattern. Behavior-preserving: `cmake --build`
  clean, ctest 14/14 (matches pre-cure baseline at `af3a259`).
- **Adjacent finding (filed-separately).** Surfaced during pre-cure
  state check: `src/core/music_detection.cpp:31` contains
  `sorted_rms.size() / 10` (10th-percentile noise floor estimate)
  with the same magic-number pattern. The file is in `src/core/`,
  separate from both Mi-5's `reference_mode.cpp` and this entry's
  `blind_mode.cpp`. Filed as `Mi-5-MUSIC-DETECTION` below per
  `feedback_tier_boundary_preservation.md` in-tier-but-different-
  file filing.

### Mi-5-ANALYSIS — magic threshold soup in `analysis.cpp` (Mi-5 sibling) — **RESOLVED in #70 (`a519df0`)** via Mi-5-CORE-CLEANUP

- **Origin.** Filed 2026-05-31 during PR #67 (Mi-5) audit. Same
  audit-agent adjacent-entry sweep that surfaced Mi-5-BLIND. User-
  ratified filing via AskUserQuestion 2026-05-31.
- **Defect (project hygiene).** `src/core/analysis.cpp` contains
  decision thresholds matching the Mi-5 pattern. Specific sites not
  yet empirically enumerated; will be surveyed at dispatch time.
- **Invariant established.** Inherited from Mi-5 (see Mi-5-BLIND
  above for the invariant text).
- **Files touched.** `src/core/analysis.cpp`.
- **Exit criteria.** No magic numbers remain in `analysis.cpp` decision
  sites.
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Direct sibling of Mi-5.
- **Effort.** Unknown until empirical site survey; likely 5-15 sites.
- **Filed timing.** Per same discipline as Mi-5-BLIND above.
- **Audit-cardinality (forward).** Single-audit by both axes.
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED in #70 (commit `a519df0`) via
  Mi-5-CORE-CLEANUP close-out
  (bundle PR with Mi-5-MUSIC-DETECTION below). Empirical site count:
  1 decision threshold + 1 non-promoted defensive value. Catalog:
  - `kZcrMinFrameSamples = 2` (`static constexpr`) — pins the M-10
    invariant "ZCR is defined as 0 for frames of length less than 2"
    at file scope, replacing the in-body literal `< 2` at the
    `compute_zero_crossing_rate` degenerate-length guard.
  - **NOT promoted (deliberate):** `0.5f` placeholder at line 118
    (`compute_spectral_flatness` stub). This isn't a decision
    threshold — it's a TODO/placeholder return value while the
    function awaits FFT implementation. The accompanying inline
    comment `"return zeros (placeholder)"` contradicts the actual
    value, but that's C-5's scope (the function is on the backlog
    for either implementation or removal); not Mi-5-ANALYSIS's
    cure to make.
  - **NOT promoted (deliberate):** `num_frames = 1` single-frame
    fallback at lines 21, 62, 117. Structural special-case for
    short signals, not a policy threshold.
  Narrow scope vs the BACKLOG's "5-15 sites likely" estimate:
  `analysis.cpp`'s three public functions take `frame_length` and
  `hop_length` as CALLER-SUPPLIED parameters, not local decisions.
  The per-pipeline default frame/hop choices live in
  `music_detection.cpp` (per Mi-5-MUSIC-DETECTION) and in the
  algorithmic call-sites that invoke these analyzers — there's no
  in-`analysis.cpp` magic-number cluster of the Mi-5 shape.
  Behavior-preserving: `cmake --build` clean, ctest 14/14 (matches
  pre-cure baseline at `747924c`).

### Mi-5-MUSIC-DETECTION — magic threshold soup in `music_detection.cpp` (Mi-5 sibling) — **RESOLVED in #70 (`a519df0`)** via Mi-5-CORE-CLEANUP

- **Origin.** Filed 2026-06-01 during Mi-5-BLIND-CLEANUP pre-cure
  state check (#69, `1ca7249`). Surfaced by reading the call chain
  `analyze_blind_mode` → `estimate_noise_floor` in
  `src/core/music_detection.cpp:31`.
- **Defect (project hygiene).** `src/core/music_detection.cpp` has
  ≥ 2 known sites with the same magic-number pattern Mi-5 /
  Mi-5-BLIND cured (PR #69 audit-corrected from initial single-site
  filing):
  - `:31` — `sorted_rms.size() / 10` (10th-percentile noise floor
    estimate inside `estimate_noise_floor`). The "/ 10" encodes a
    policy choice (which percentile to use as noise floor) and has
    an inline comment "Sort RMS values and take the 10th percentile
    as noise floor estimate" (line 26) that should port to a
    constexpr citation. Called by both `blind_mode.cpp` (via
    `estimate_noise_floor`) and `reference_mode.cpp` (transitively),
    so the policy is shared cross-pipeline.
  - `:54` — `noise_floor * 4.0f` (12 dB-above-noise-floor music-
    onset threshold inside `detect_music_start`). Inline comment at
    `:53` reads "Threshold: 12 dB above noise floor (factor of 4)".
    Surfaced by PR #69 audit; same shape as the
    `kBlindGapThresholdNoiseFloorMultiplier = 2.0f` (+6 dB) constant
    promoted in this PR — same family, different dB choice.
- **Invariant established.** Inherited from Mi-5 (see Mi-5-BLIND
  above for the invariant text).
- **Files touched.** `src/core/music_detection.cpp` (and
  `src/core/music_detection.hpp` if other thresholds are present at
  the API surface — to be confirmed at dispatch time).
- **Exit criteria.** No magic numbers remain in
  `music_detection.cpp` decision sites.
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Direct sibling of Mi-5.
- **Effort.** ≥ 2 sites enumerated above (PR #69 audit empirical
  count); full pre-dispatch survey may surface 1-3 more given file
  size and scope. Final site count to be confirmed at dispatch.
- **Filed timing.** Per `feedback_tier_boundary_preservation.md`
  in-tier-but-different-file filing — surfaced during Mi-5-BLIND
  state-check; filed as own item rather than folded because
  Mi-5-BLIND's mandate was strictly `blind_mode.cpp` (the calling
  site, not the callee's implementation).
- **Audit-cardinality (forward).** Single-audit by both axes.
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED in #70 (commit `a519df0`) via
  Mi-5-CORE-CLEANUP close-out
  (bundle PR with Mi-5-ANALYSIS above). Empirical site count: 6
  in-body sites consolidated into 4 file-scope `static constexpr`
  declarations. Catalog (top of `music_detection.cpp`, right after
  `namespace mwaac {`):
  - `kMusicDetAnalysisFrameSeconds = 0.05f` — 50 ms envelope frame
    duration (parity with `reference_mode.cpp`'s
    `kEnvelopeDefaultFrameMs = 50.0` and `blind_mode.cpp`'s
    `kBlindAnalysisFrameSeconds = 0.05f`).
  - `kMusicDetAnalysisHopFrameDenominator = 4` — 25% hop = 12.5 ms
    at 50 ms frame (75% overlap, project-standard analysis hop).
  - `kNoiseFloorPercentileDenominator = 10` — 10th percentile of
    frame RMS as noise-floor estimate. Cross-pipeline policy
    (called by both blind_mode and reference_mode pathways).
  - `kMusicOnsetNoiseFloorMultiplier = 4.0f` — +12.04 dB above noise
    floor for sustained-music labeling. Contrasts with
    `kBlindGapThresholdNoiseFloorMultiplier = 2.0f` (+6 dB) which
    is gap-vs-music; the higher 12 dB cutoff here is music-vs-
    surface-noise on lead-in regions.
  - **NOT promoted (deliberate):** `1e-10f` defensive floor at the
    `estimate_noise_floor < 1e-10f` guard in `detect_music_start`.
    Defensive value against near-zero noise floors causing
    downstream division issues, not a tunable policy. Matches
    Mi-5's precedent of leaving defensive defaults alone (compare
    `reference_mode.cpp`'s `-120.0` / `1e-9` in `estimate_noise_floor_db`).
  Bonus dedup: `estimate_noise_floor` (lines 65-66 pre-cure) and
  `detect_music_start` (lines 88-89 pre-cure) both held private
  copies of the 50 ms / 25% hop values; promoting to file scope
  consolidates them. Behavior-preserving: `cmake --build` clean,
  ctest 14/14 (matches pre-cure baseline at `747924c`).

### DOC-VOTE-RADIUS-COMMENT — stale ±1.5/±2.5s vote-window comment in `reference_mode.cpp` — **RESOLVED in #68 (`90e8fc3`)** via T8-DEFERRED-PAPERWORK-SWEEP option (a)

- **Origin.** Filed 2026-05-31 during PR #67 (Mi-5) audit. Pre-existing
  stale comment found by audit-agent during Mi-5 verification; NOT
  introduced by Mi-5. User-ratified filing via AskUserQuestion
  2026-05-31.
- **Defect (descriptive, not invariant).** `src/modes/reference_mode.cpp`
  lines 739, 742 (post-Mi-5 line numbers) contain inline comments
  describing Vote 1 / Vote 2 / Vote 3 snippet windows as "tighter ±1.5 s
  window" and "wider ±2.5 s window" — but all three radii are now
  `kSnippetVoteRadiusSeconds = 10.0` (per Mi-5 catalog). The comment
  was stale pre-Mi-5 (same prose existed at `0d459d3`'s lines
  575/578 with `10.0` literals); Mi-5 inherited rather than introduced
  the stale narration.
- **Reachability.** Cosmetic / for future readers. The constexpr name
  `kSnippetVoteRadiusSeconds` is self-documenting at the call site; the
  stale comment misleads only readers parsing the surrounding prose.
- **Files touched.** `src/modes/reference_mode.cpp` (2 inline comments).
- **Possible outcomes.**
  - (a) Update comments to match current values (single `±10 s` window
    description, or remove the per-vote width annotations since they're
    all identical now). ~3 LOC.
  - (b) Update comments AND consider whether the three votes should
    have different radii (audit didn't recommend; could surface during
    future correlation tuning).
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Micro-cleanup shape.
- **Effort.** ≤ 5 LOC.
- **Filed timing.** Per
  `feedback_audit_suggestions_need_ratification.md`, audit suggestion
  surfaced for user ratification before filing.
- **Audit-cardinality (forward).** Single-audit (trivially small).
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED via T8-DEFERRED-PAPERWORK-SWEEP
  close-out, option (a). Comment block at `reference_mode.cpp:738-743`
  (post-Mi-5 line numbers) rewritten to remove the stale "±1.5 s" /
  "±2.5 s" width annotations and instead reference the unified
  `kSnippetVoteRadiusSeconds` (±10 s) used by all three snippet specs
  (verified empirically: all 3 entries in the `specs` vector at lines
  759, 763, 767 reference `kSnippetVoteRadiusSeconds`). Semantic
  structure preserved — Snippet A (Vote 1) is still the primary
  estimator; Snippets B, C (Votes 2, 3) are still validators / recovery.
  Brief Mi-5 cross-reference added to the comment so future readers
  see the radii-unification provenance.

### Mi-5-CORRELATION — magic threshold soup in `correlation.cpp` (Mi-5 sibling) — **RESOLVED in #71 (`f4215ce`)** via Mi-5-CORRELATION-CLEANUP

- **Origin.** Filed 2026-06-01 during PR #70 (Mi-5-CORE-CLEANUP)
  audit. Audit's adjacent-entry sweep flagged `correlation.cpp:130`
  `apply_highpass(processed, sample_rate, 80.0f)` as the closest
  remaining Mi-5-shape candidate in `src/core/`. User-ratified
  filing via AskUserQuestion 2026-06-01.
- **Defect (project hygiene).** `src/core/correlation.cpp:130`
  passes a bare `80.0f` literal as the cutoff frequency to
  `apply_highpass`. The `apply_highpass` function definition (line
  92) takes `cutoff_hz` as a parameter, so the `80.0f` is a
  call-site policy decision (which cutoff to use for the pre-
  correlation highpass) — not the function's internal magic. Per
  the Mi-5 invariant, decision thresholds belong at file scope as
  `constexpr` with citation comments.
- **Defensive values (informational, not in scope).** Audit noted
  `correlation.cpp` also contains `1e-10f` and `1e-15` defensive
  guards. Per Mi-5's precedent (compare reference_mode's `-120.0` /
  `1e-9` in `estimate_noise_floor_db`, music_detection's `1e-10f`
  in `detect_music_start`), defensive values stay as in-body
  literals.
- **Invariant established.** Inherited from Mi-5 (see Mi-5-BLIND
  above for the invariant text).
- **Files touched.** `src/core/correlation.cpp` (call-site
  replacement); possibly `src/core/correlation.hpp` if API surface
  needs the constant exported.
- **Exit criteria.** No magic numbers remain in `correlation.cpp`
  decision sites.
- **Tier rationale.** Tier 8 (Documentation / attribution / hygiene).
  Direct sibling of Mi-5.
- **Effort.** ≤ 10 LOC (1 catalog block + 1 in-body replacement).
  Possibly more if a full pre-dispatch survey surfaces other
  correlation-domain decision thresholds (e.g. correlation-window
  bounds, lag-search radii at call sites). Final scope to be
  confirmed at dispatch.
- **Filed timing.** Per `feedback_audit_suggestions_need_ratification.md`
  audit suggestion surfaced for user ratification before filing;
  ratified.
- **Audit-cardinality (forward).** Single-audit by both axes.
- **Cure-attribution.** When cured, this entry receives a Status /
  Resolution block.
- **Status / Resolution.** RESOLVED in #71 (commit `f4215ce`) via
  Mi-5-CORRELATION-CLEANUP close-out. Empirical site survey: 2 sites promoted to file-scope
  `static constexpr`, beyond the audit's single-site suggestion
  (`:130` 80.0f highpass). The second site was identified during the
  pre-cure read of the `cross_correlate` body. Catalog (top of
  `correlation.cpp`, right after `namespace mwaac {`):
  - `kCorrelationHighpassCutoffHz = 80.0f` — pre-correlation 80 Hz
    highpass cutoff applied via `apply_highpass` in
    `preprocess_for_correlation`. Citation: 80 Hz attenuates vinyl
    rumble + turntable mechanical noise below the lowest pitched
    musical content (cellos ~65 Hz; lowest piano note ~27 Hz; the
    80 Hz cutoff removes infrasonic / sub-bass content dominated
    by playback-mechanism noise).
  - `kCorrelationRefineRadiusDownsampleMultiplier = 2` — Stage-2
    refinement window width as a multiplier of `downsample_factor`.
    The `2` means the refiner searches ±2 × downsample_factor
    samples around the coarse peak. Encodes a policy choice on
    how much over-search to do beyond the coarse stage's
    quantization error.
  - **NOT promoted (deliberate):** `1e-10f` at `:120` (normalize_rms
    zero-RMS guard); `1e-10` at `:206` and `:250`
    (cross_correlate per-lag normalization zero-norm guards);
    `1e-15` at `:284` and `:345` (cross_correlate_fft FFT-path
    zero-energy short-circuits). All defensive guards against
    division by tiny values, not tunable policy. Matches the
    Mi-5 / Mi-5-BLIND / Mi-5-MUSIC-DETECTION precedent of leaving
    defensive defaults alone.
  Audit's single-site suggestion (80.0f only) was followed-plus-one:
  per `feedback_user_concrete_detail_paraphrase.md`, orchestrator
  did its own empirical site survey rather than propagating only
  the audit-paraphrased site count. Behavior-preserving:
  `cmake --build` clean, ctest 14/14 (matches pre-cure baseline at
  `cd185fa`).

### Mi-5 family close-out summary (PR #71 close-out)

The Mi-5 family is fully closed across 5 entries / 6 files
(2026-06-01). Empirical raw `grep -cE "^(static |inline )?constexpr"`
totals against `main` post-#71:

| Entry | File(s) | Raw constexpr (new) | PR |
|---|---|---|---|
| Mi-5 | `src/modes/reference_mode.cpp` | 44 (45 total — 1 pre-existing C-4 `kAnalysisToNativeRoundingTolerance`) | #67 |
| Mi-5-BLIND | `src/modes/blind_mode.cpp` + `blind_mode.hpp` | 4 + 4 = **8** | #69 |
| Mi-5-ANALYSIS | `src/core/analysis.cpp` | 1 | #70 |
| Mi-5-MUSIC-DETECTION | `src/core/music_detection.cpp` | 4 | #70 |
| Mi-5-CORRELATION | `src/core/correlation.cpp` | 2 | #71 |

**Mi-5 family raw total: 44 + 8 + 1 + 4 + 2 = 59 new file-scope
`constexpr` promotions.**

Narrative semantic-decision count (where the
`kBlindSignalReferencePercentileNumerator/Denominator` pair counts
as one decision, not two): 44 + 7 + 1 + 4 + 2 = 58.

(PR #71's commit message + PR description claimed "53 promoted
decision constants" — an arithmetic slip from memory rather than
the empirical count. Per `feedback_user_concrete_detail_paraphrase.md`,
the audit's count verification surfaced this; corrected here as
the authoritative family total.)

**Across all Mi-5-family files, defensive guards (`1e-9`, `1e-10`,
`1e-15`, `-120.0`) and formula constants (dB-to-linear math,
`std::pow(10, db/20)`, frame-size derivations like `sample_rate / N`)
were uniformly NOT promoted** per Mi-5's "decision threshold vs
formula/defensive" distinction. The Mi-5 catalog cross-references
between files (kEnvelopeDefaultFrameMs ↔ kBlindAnalysisFrameSeconds ↔
kMusicDetAnalysisFrameSeconds for the 50ms-frame parity;
kBlindGapThresholdNoiseFloorMultiplier ↔ kMusicOnsetNoiseFloorMultiplier
for the 6 dB / 12 dB contrast) make the threshold landscape
discoverable across the codebase.

---

## Tier 9 — Cleanup (Minor, Nit)

### Mi-2 — compute_rms_energy guard order fragility — `src/core/analysis.cpp`. — **RESOLVED in #76 (`c2609b8`)** via Mi-2 guard reorder. `compute_rms_energy` and `compute_zero_crossing_rate` computed `num_frames = 1 + ((size() - frame_length) / hop_length)` unconditionally, then corrected `num_frames = 1` afterward for sub-frame-length signals — so the `size() - frame_length` subtraction unsigned-wrapped (defined modular arithmetic, invisible to UBSan) before being discarded. Reordered to initialize `num_frames = 1` (the short-signal answer) and conditionally `+=` the extra frames only when `size() >= frame_length`, so the subtraction is never evaluated while negative. `num_frames` is identical in every case (`size()<F`→1, `size()==F`→1, `size()>F`→`1+(size-F)/hop`), so the change is byte-for-byte behavior-preserving; 14/14 ctest green. Used the fully-initialized `= 1` form rather than declared-then-assigned `size_t num_frames;` to avoid a new `cppcoreguidelines-init-variables` finding (clang-tidy is in the merge gate since #73). **Scope:** rms + zcr only. The standing family-grep surfaced a third site of the same pattern at `compute_spectral_flatness` (`analysis.cpp:141`), but that site has **no** corrective guard (latent `bad_alloc` / ~2⁶³ allocation on short input) and is already tracked as **C-5** (Tier 2, cure = return empty on short input) — excluded per the cross-reference rule and left untouched. Audit CLEAN (independent case-analysis confirmed `num_frames` identical across all three size regimes, entry guard `frame_length>0` intact at the casts, spectral_flatness verified unmodified); CI 6/6 green (clang-tidy included).
### Mi-3 — resample_linear divides by zero when sample_rate == 0 — `src/core/audio_buffer.cpp`. — **RESOLVED in #33 (`8af5793`)** via structural cure (deviation from original "early return {}" spec; see `docs/deviations.md` Mi-3 entry for reasoning).
### Mi-6 — `min` identifier shadows std::min — `src/modes/reference_mode.cpp`. — **RESOLVED in #75 (`ef4dc9b`)** via Mi-6-MIN-SHADOW (behavior-preserving rename `min`→`minutes` at the decl `reference_mode.cpp:1409` and its sole use `:1417`, inside the `g_verbose`-gated "Per-Track Alignment Results" print loop; `std::to_string(minutes)` emits bytes identical to `std::to_string(min)`, so verbose stderr output is unchanged). **Scope correction:** the original entry also listed `src/main.cpp`, but main.cpp contains zero `min` tokens (`grep -nE '\bmin\b' src/main.cpp` → 0 hits) — that reference was stale and is dropped; reference_mode.cpp was the single real site. Audit CLEAN (independently confirmed 16× qualified `std::min` with no `using`-directive, shadow-family sweep = this one site / no local `max` siblings, empty Active known-failing list); CI 6/6 green (clang-tidy included).
### Mi-11 — test_deps.cpp is dead — delete or compile. — **RESOLVED in #73 (`c5939d1`)** via T8-CLANG-TIDY-BASELINE-CLEANUP (delete option). The file was deleted as part of the clang-tidy baseline clean-up because (a) it was not referenced in CMakeLists.txt and was never compiled into any target, (b) the file's absence from `compile_commands.json` produced a `clang-diagnostic-error` under clang-tidy, and (c) the file's only contents were `#include` statements verifying header availability — there was no behavior to preserve. Verified no other references in the source tree before deletion. See T8-CLANG-TIDY-BASELINE entry's Status / Resolution block for the closure details.
### Mi-12 — src/core/core.hpp is dead — delete. — **RESOLVED in #74 (`8c1e219`)** via T9-DEAD-CODE-SWEEP (delete option). Empirically verified dead pre-cure: `grep -rn "core/core.hpp\|#include.*\"core.hpp\"\|#include <core/core" src/ tests/` returned zero hits (the file is an umbrella header that `#include`s `audio_info.hpp` / `split_point.hpp` / `analysis_result.hpp` / `alignment_result.hpp`, but nothing includes it back). Deleted via `git rm`. Removed from PROJECT_SPEC.md Architecture diagram in same PR (the entry was previously annotated `(dead; Mi-12 pending deletion)` per T8-SPEC-ARCH-DRIFT-CLEANUP; preemptive removal matches the PR #73 audit's cross-doc reconciliation pattern catch on test_deps.cpp).
### Mi-13 — verbose.hpp g_timer_start is unused — delete. — **RESOLVED in #74 (`8c1e219`)** via T9-DEAD-CODE-SWEEP (delete option). Empirically verified unused pre-cure: `grep -rn "g_timer_start" src/ tests/` returned only the declaration at `src/core/verbose.hpp:13`. `VerboseTimer` uses its own member `start_` (`src/core/verbose.hpp:34, 47`) rather than the global; no other code path reads or writes `g_timer_start`. Deleted the declaration line + surrounding comment ("Start timing for a named operation") via `Edit`. Adjacent: `#include <chrono>` retained — still required for `VerboseTimer`'s `std::chrono::steady_clock::time_point start_` member.
### Mi-14 — verbose globals not thread-safe — std::atomic<bool> or Logger&. — **RESOLVED in #77 (`23477d0`)** via the atomic<bool> option. *Scope refined post-T9-DEAD-CODE-SWEEP (PR #74): `g_timer_start` was the second verbose-global; deleted as unused via Mi-13 sibling, so the remaining surface is `g_verbose` only (`src/core/verbose.hpp:10`). Mi-14's "globals" framing was correct at filing time; the singular form would now read as "`g_verbose` is not thread-safe".*

**Ratified 2026-06-07 — `std::atomic<bool>`.** Cured in this PR: `g_verbose`
is now `inline std::atomic<bool> g_verbose{false}` (`verbose.hpp`, with
`#include <atomic>`); every read/write site compiles unchanged via atomic's
`operator bool`/`operator=`. The `:10` ref above is pre-cure (the declaration
is now at `:11` after the added include).
### Mi-15 — explicit ctors audit on result wrappers — resolved by M-14.
### Mi-16 — encode_float80 NaN/over-/under-flow handling — **doc/code mismatch RESOLVED in #77 (`23477d0`); clamp-assert + extreme-value-test hardening DEFERRED (below the Tier-9 cut)**

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
- **Ratified 2026-06-07 — doc-only fix (cure option b).** The doc/code
  mismatch is cured in this PR: the `encode_float80` header comment no longer
  claims "NaN encodes as +0" (the pre-cure claim the bullets above describe);
  it now states NaN is out of contract — asserted in Debug, unspecified in
  Release (falls through `frexp`, NOT +0) — and notes the sole caller passes a
  real sample rate so NaN never reaches it. **Residual, deferred (below the
  Tier-9 cut):** the clamp → `assert(isfinite && >= 0)` hardening (bullet 1)
  and the `encode_float80(1e±5000)` extreme-value tests (bullet 3) are
  intentionally NOT done — hardening, not the doc-lie correctness defect.
### Mi-17 — std::stoll in natural_less can throw — bound digit count + un-SKIP natural-sort unit test — **RESOLVED in #46 (`4d542d3`)**

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
  - [x] `natural_less` does not throw on any input. Cure landed at
        `src/modes/reference_mode.cpp:716-762` (definition moved out
        of the file-static anonymous namespace and exposed at
        `mwaac::natural_less` in `src/modes/reference_mode.hpp:84`).
        Cure shape: option (c) — length-then-lex compare on
        zero-stripped digit strings inside the digit-run branch.
        Equivalent to numeric compare for any in-range value AND
        well-defined for digit runs of any length. Audit verified the
        strict-weak-ordering properties (irreflexivity, antisymmetry,
        transitivity) and that the leading-zero-strip handles the
        edge cases `"000"`, `"0"`, `"123"`, `"0123"` correctly without
        narrowing the ordering. Pre-cure `std::stoll` calls at
        `:60-61` (pre-resolution line numbers) deleted.
  - [x] `tests/test_reference_mode.cpp:20`'s SKIP replaced with a
        real assertion. Post-Mi-4 close-out the SKIP sat at `:24` (line
        drift from C-4's added passing TEST_CASE); replaced with two
        TEST_CASEs at `tests/test_reference_mode.cpp:120-141`
        ("Reference mode: natural filename sort ordering" — primary
        invariant including the BACKLOG-mandated
        `natural_less("Track 2.wav", "Track 10.wav")` plus
        decade-boundary cases and strict-weak irreflexivity check) and
        `:151-181` ("natural_less: digit run > 18 characters does not
        throw" — overflow regime: equal-length 25-digit, different-
        length 20-vs-21-digit, mixed short-vs-pathological, plus
        strict-weak symmetry on pathological inputs). Splitting the
        primary-invariant and overflow axes into separate TEST_CASEs
        means a future regression that brings back `std::stoll` aborts
        only the overflow case rather than masking the primary one.
  - [x] Combined with M-REF-ALIGN-UNIT, `test_reference_mode`'s exit
        status flips to `Passed`. Empirical post-merge: 5 cases / 47
        assertions, all pass on every CI variant (Linux Debug/Release,
        macOS Debug/Release, sanitizers); 0 SKIPs remain. Binary
        exit-code surface had already been cured by C-4 at PR #41
        (1 pass + 2 skip → exit 0); this PR cures the SKIP-cluster
        surface (5 pass + 0 skip → exit 0) per the cure-attribution
        split documented in `docs/known-failing-tests.md` test_reference_mode
        entry. Sibling defect at `src/modes/reaper_export.cpp:43-44`
        (`natural_less_filename`, identical std::stoll throw shape)
        filed pre-dispatch as separate Tier 9 item M-REAPER-EXPORT-SORT-THROW
        in `e2893d6` to preserve Mi-17's explicit single-function scope.

### M-REAPER-EXPORT-SORT-THROW — std::stoll in `natural_less_filename` can throw — sibling defect of Mi-17 — **RESOLVED in #47 (`88e5267`)**

- **Origin.** Surfaced during the M-REF-ALIGN-UNIT + Mi-17 paired-dispatch
  pre-dispatch checklist (adjacent-entry sweep on `std::stoll` usage in
  `src/`). Filed as a separate item rather than folded into Mi-17 because
  Mi-17's BACKLOG framing is explicitly "Tier 9 (Cleanup / Nit) —
  single-function hardening with a single new unit test"; folding would
  expand Mi-17 from one-function to two-function and break that scope
  contract. Per `feedback_tier_boundary_preservation.md`, in-tier sweep
  findings on a different function file as their own item.
- **Defect.** `src/modes/reaper_export.cpp:43-44` (inside the
  file-static `natural_less_filename` comparator) calls
  `std::stoll(ap[i].second)` and `std::stoll(bp[i].second)` on each
  digit-run pair without bounding the digit count. Identical defect
  shape as Mi-17 (`reference_mode.cpp:60-61`): a pathological filename
  like `Track 12345678901234567890.wav` triggers `std::out_of_range`,
  which propagates out of the `std::sort` callsite at
  `reaper_export.cpp:72` (`std::sort(out.begin(), out.end(), natural_less_filename)`)
  and aborts the program. The two functions are independent copies of
  the same natural-sort logic — `natural_less_filename` was added to
  `reaper_export.cpp` so the export module wouldn't depend on
  reference-mode internals (see comment at `reaper_export.cpp:18-20`),
  but the stoll-throw bug came along for the ride.
- **Invariant established.** Same as Mi-17's: "the natural-sort
  comparator produces a strict-weak-ordering total order on filenames
  containing arbitrary-length digit runs, and never throws."
- **Files touched.** `src/modes/reaper_export.cpp` (the
  `natural_less_filename` body around `:43-44`), `tests/test_reaper_export.cpp`
  if it exists, otherwise a new minimal test file or fold into an
  existing reaper-export test.
- **Cure options.** Same as Mi-17:
  - (a) Bound digit-run length at 18 chars; lex-fall-back on the rest.
  - (b) Use `std::from_chars` and check for `errc::result_out_of_range`.
  - (c) Manual digit-by-digit compare (length-then-lex on zero-stripped
    digit strings is functionally equivalent to numeric compare and never
    throws).
  Whichever cure Mi-17 lands on, mirror it here for consistency.
- **Tier rationale.** Tier 9 (Cleanup / Nit) — single-function
  hardening, single test. Same shape as Mi-17.
- **Effort.** ≤ 20 lines of code + 1-2 unit-test cases. One PR, one
  audit. No fixture or pipeline interaction. Prefer dispatch *after*
  Mi-17 lands so cure-shape consistency can be enforced by direct
  reference.
- **Exit criteria.**
  - [x] `natural_less_filename` does not throw on any input. Cure shape:
        **delegation to `mwaac::natural_less`** rather than parallel
        re-derivation. Body at `src/modes/reaper_export.cpp:32` is a
        single line: `return natural_less(a.filename().string(), b.filename().string());`.
        The duplicate algorithm body (30+ lines including the parts
        lambda and the std::stoll for-loop) at pre-cure `:21-51` is
        deleted; the function is now an explicit thin wrapper over
        the Mi-17-hardened `mwaac::natural_less` (length-then-lex on
        zero-stripped digit strings). `#include "modes/reference_mode.hpp"`
        added at `:2` to bring the public Mi-17 symbol into scope.
        natural_less_filename moved from anon-namespace to `mwaac::`
        scope (declared at `src/modes/reaper_export.hpp:40-41`); std::sort
        callsite at `:56` resolves the unqualified name via enclosing-
        namespace lookup, production behavior unchanged on normal-
        length filenames. Audit verified the delegation argument
        shape (`.filename().string()`) matches the parallel natural_sort
        callsite at `src/modes/reference_mode.cpp:37` exactly — zero
        semantic drift.
  - [x] Test asserts the cure shape. `tests/test_reaper_export.cpp` (new
        file) with two TEST_CASEs at `:19-40` and `:42-73`. Primary
        invariant case includes a path-prefix tie-break check
        (`/refs/Track 2.wav` vs `/elsewhere/Track 2.wav` → tied,
        not less) that discriminates the wrapper-layer `.filename().string()`
        extraction from a broken `.string()` shape; overflow case
        mirrors Mi-17's structure exactly (25-digit equal-length,
        20-vs-21-digit length differential, mixed short-vs-pathological,
        strict-weak symmetry on pathological inputs). 2 cases / 8
        assertions, all pass on every CI variant. Cure-shape mirroring
        achieved by direct delegation rather than parallel re-derivation,
        which the audit verdict accepted as a stricter form of the
        BACKLOG's "mirror Mi-17 here for consistency" mandate (no
        algorithm-divergence risk over time; any future cure to
        `mwaac::natural_less` automatically applies here).

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

### Mi-18 — -Wconversion / -Wdouble-promotion / -Wsign-conversion cleanup — **RESOLVED (reconciled 2026-06-07)**

**Status — RESOLVED.** The `-Werror` body was completed incrementally across
the cycle's per-TU passes (e.g., `audio_buffer.cpp` in PR #30 `b9f9508`;
`blind_mode.cpp` in `46e58ce` "style(blind_mode): silence -Werror findings
(Mi-18)") — the entry was simply never stamped. Verified 2026-06-07: a local
Release + `MWAAC_WERROR=ON` rebuild of every first-party TU is warning-free
(only a benign `ld: ignoring duplicate libraries` linker notice, not a
compiler `-W` finding), consistent with 6/6 green CI on every PR since the gate
was set green in #73. The 15 follow-up *questions* the sweep surfaced
(suppressions via `[[maybe_unused]]` / cast / `#pragma` that silenced the
warning without resolving the underlying design or correctness point) were
captured in `docs/m-i-18-followups.md` and are promoted to the tracked
`Mi-18-FU` catalog entry below (per-item IDs `Mi-18-FU-1` … `Mi-18-FU-8`,
15 items total). The historical plan text is retained for
narrative continuity.

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

### MI18-FOLLOWUP-BLIND-ITER — defensive cast on `blind_mode.cpp:73-74` iterator+size_t arithmetic — **RESOLVED (reconciled 2026-06-07)**

**Status — RESOLVED (reconciled).** The two defensive casts this item asks
for are already present in the tree — they landed in commit `46e58ce`
("style(blind_mode): silence -Werror findings (Mi-18)") as part of the
Mi-18 per-TU `-Wconversion`/`-Wsign-conversion` sweep, not as a dedicated
PR. The `:73-74` line refs throughout this entry are **stale**: the
`std::vector<float> gap_samples(...)` construction now sits at
`blind_mode.cpp:104-105`, each iterator offset wrapped exactly as the exit
criteria require:
`samples.begin() + static_cast<std::ptrdiff_t>(start_sample)` and
`... static_cast<std::ptrdiff_t>(end_sample)`. Verified against the working
tree 2026-06-07. No code change needed; this is decision-independent
reconciliation paperwork closing out a tracking entry whose work was
already done under its owner-epic.

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
  - [x] `samples.begin() + start_sample` → `samples.begin() + static_cast<std::ptrdiff_t>(start_sample)` — present at `blind_mode.cpp:104` (stale ref said `:73`).
  - [x] Same cure at line 74 for `end_sample` — present at `blind_mode.cpp:105`.
  - [x] Build remains green on Linux GCC and macOS Apple Clang — landed under `46e58ce`; CI green since.
  - [x] Single commit, no scope creep beyond the two casts — folded into the `blind_mode.cpp` Mi-18 TU sweep (`46e58ce`).
- **Effort.** ≤ 5 lines of diff, one commit, no audit needed (mechanical
  defensive cast — the audit framework already covered the broader pattern
  in Mi-18 pass 2).

### Mi-18-FU — Mi-18 follow-up catalog (15 items) — promotion of `docs/m-i-18-followups.md`

**Origin.** The Mi-18 `-Werror` sweep silenced its findings with mechanical
casts / `[[maybe_unused]]` / `#pragma` suppressions, intentionally NOT folding
the deeper design/correctness questions into the cure PRs (Mi-18 scope =
mechanical only). Those 15 questions were parked in
`docs/m-i-18-followups.md`. This entry promotes them into the source-of-truth
BACKLOG so they survive the Tier-9 close-out and the testing handoff rather
than living only in a side doc. **The mechanical cure is DONE and verified**
(see Mi-18 above — `-Werror` body clean); every item below is a *post-cure*
question and none block the build.

**Identification.** Cited by **symbol name**, not line — the FU-doc's line refs
are stale (line-drift rule, `docs/orchestrator-handoff.md`). Locations below
are from a working-tree grep 2026-06-07.

*Candidate correctness defect — RATIFIED 2026-06-07 (remove the param); RESOLVED in #77 (`23477d0`):*

- `Mi-18-FU-4c` — `estimate_noise_floor(window_seconds)` ignored its window.
  Pre-cure, the declaration carried `window_seconds = 2.0f` ("Window for
  searching quietest region") while the body marked it `[[maybe_unused]]` and
  took a 10th-percentile RMS over the *entire* signal; the sole caller passed
  the 2.0 s default, which was silently dropped. **Distinct from the resolved
  `score_gap` `sample_rate` issue (Mi-7 / M-7)** despite the FU-doc's "REAL
  Mi-7 SMOKE" label — its own unfiled ignored-param issue. **Ratified
  2026-06-07: remove the param** (the window was never load-bearing; the
  estimator is whole-signal by design). Cured in this PR — `window_seconds`
  dropped from both the `music_detection.hpp` declaration and the
  `music_detection.cpp` definition; all three call sites already omitted the
  argument, so no caller changed. **RESOLVED in #77 (`23477d0`).**

*Real feature work — single-tracked elsewhere:*

- `Mi-18-FU-4b` — `compute_spectral_flatness` is a `// TODO: Implement with
  FFT` stub returning `0.5f` (`analysis.cpp:133`, decl `analysis.hpp:33`).
  **This is the same work as `C-5`** — folded into C-5 to avoid double-tracking;
  the vestigial `sample_rate` param resolves when the FFT lands.

*Dead-code deletion candidates — RATIFIED 2026-06-07 (delete as a batch); cure lands in the follow-up dead-code-sweep PR (verify call-sites first):*

- `Mi-18-FU-6b` — `measure_fade_in_samples` (`reference_mode.cpp:365`) +
  `estimate_noise_floor_db` (`:637`), both `[[maybe_unused]]`, no call site.
  Future-hook vs delete.
- `Mi-18-FU-7a` — dead `bytes_per_frame` in the AIFF header builder.
  **Target is the `[[maybe_unused]]` instance at `audio_file.cpp:1131` ONLY** —
  there is a LIVE `bytes_per_frame` at `:1031` (used `:1032`/`:1033`) and a LIVE
  `bytes_per_frame()` method (`:691`,`:1291`,`:1294`). A by-name delete would
  break the live ones.
- `Mi-18-FU-7b` — `gap_start_sec` (`blind_mode.cpp:272`, `[[maybe_unused]]`)
  computed but never logged. Rewire into verbose output or delete.
- `Mi-18-FU-7c` — `create_test_wav` stub, no caller, doesn't even write a file
  (`test_audio_file.cpp:28`, `[[maybe_unused]]`). Delete; `test_lossless.cpp`
  has the real one.
- `Mi-18-FU-7d` — `double phase = 0.0` declared + never-advanced in two fixture
  builders (`test_integration.cpp:162` and `:204`, both `[[maybe_unused]]`).
  Delete both.
- `Mi-18-FU-7e` — `read_file_bytes` `[[maybe_unused]]` at `test_lossless.cpp:21`.
  **Caveat:** the FU-doc claims a twin in `test_integration.cpp` "is used" at
  `:26`, but the twin is actually at `:89` and is ALSO `[[maybe_unused]]`.
  Confirm call-sites for BOTH before deleting either; the "is used" claim needs
  re-verification.
- `Mi-18-FU-8` — `vinyl_info` bound but never read (`test_integration.cpp:690`,
  `[[maybe_unused]]`). Possibly an abandoned assertion — sanity-check whether the
  test should assert on it before deleting.

*Design-level — recommend DEFER (cast landed; deeper fix is a separate design pass, out of Tier 9):*

- `Mi-18-FU-1` — `resample_linear` mixes `float`/`double` interpolant
  (`audio_buffer.cpp:57`). Keep-float vs promote-to-double kernel.
- `Mi-18-FU-2` — channel-average `float/int` divide (`audio_buffer.cpp:41`,
  cast present). `std::accumulate` cleanup candidate; numerically fine for audio.
- `Mi-18-FU-3` — `output_size` `size_t→double` ratio cast past 2^53
  (`audio_buffer.cpp:81`; Mi-3 already guards the cast at `:72`). Integer-
  arithmetic rewrite is the deeper fix.
- `Mi-18-FU-4a` — vestigial `sample_rate` on `compute_rms_energy`
  (`analysis.hpp:14` / `analysis.cpp:29`, `[[maybe_unused]]`). Keep-for-ABI vs
  drop — a signature change.

*Already cured via `#pragma`; shim is a nice-to-have — recommend DEFER:*

- `Mi-18-FU-5` — pocketfft `size→long-double` warning, suppressed by `#pragma`
  at the include site in `correlation.cpp` (vendored header off-limits). A
  `mwaac_fft.hpp` shim would centralize the suppression.
- `Mi-18-FU-6` — Catch2 template warnings, suppressed by `#pragma` in test TUs.
  A shared `tests/test_main.hpp` would centralize it.

**Cross-references.** `Mi-18-FU-4b` ⇄ `C-5` (same spectral_flatness work —
single-track under C-5). `Mi-18-FU-4c` is independent of `Mi-7` / `M-7`. The
`### Nits — N-1 through N-~12` section below overlaps this catalog (both are
ride-along cleanup); the dead-`static` / `M_PI` / waveform nits are tracked
there.

**Status.** Catalog promoted 2026-06-07. Ratification outcomes (2026-06-07):
**FU-4c → remove the param (RESOLVED in #77)**; **the FU-6b/7a/7b/7c/7d/7e/8
dead-code batch → delete (cure in the follow-up dead-code-sweep PR)**;
design-level (FU-1/2/3/4a) and pragma-shim (FU-5/6) **deferred** as below the
Tier-9 cut; FU-4b folded into C-5. RESOLVED stamps land in each item's
close-out.

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
