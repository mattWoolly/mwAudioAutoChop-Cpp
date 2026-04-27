---
name: M-14 scope inventory
description: Authoritative file/line inventory of every LoadResult and Expected declaration, return, and consuming call site. Hand to M-14 fix-agent on dispatch so scope is fixed up-front, not discovered mid-flight.
type: project
---

# M-14 — `LoadResult` / `Expected` collapse: scope inventory

**Generated:** 2026-04-25 from main @ `b365b3c` (orchestrator paperwork commit).
**Baseline note.** When M-14 dispatches, the working tree will be **post-C-2 merge** (PR #29 merged to main), which removes a handful of `audio_file.value()` consumers from `src/main.cpp` (replaced by the `auto& audio_file = opened.value();` rebind pattern) and adds precondition asserts to `Expected::value/error`. The producer-side declarations and the modes/tests inventory below are unaffected by C-2 and are the load-bearing surface for M-14. The fix-agent should re-grep `src/main.cpp` after rebasing on the post-C-2 main to pick up the canonical-shape consumers introduced by C-2.

## Hard exit criterion (from BACKLOG.md M-14)

The latent `[basic.life]/8` UB in `Expected`'s `reinterpret_cast`-into-aligned-storage pattern **must be cured in this PR — no further deferral**. Two acceptable cures (audit-2 of C-2 evaluated both):

1. **Replace storage with `std::variant<T, E>`.** Mass-rewrite of `Expected`'s
   internals; call-site API stays compatible if the accessors keep their
   existing names. `LoadResult` removed entirely.
2. **Keep placement-new layout, insert `std::launder` at every accessor.**
   Smaller diff, cures UB only; explicit justification required in the PR
   description per M-14's exit criterion language.

Pick **one** path; do not ship both.

Independently, M-14 must also:
- Remove `LoadResult` from the tree. Its three consumers (`load_audio_mono`
  internally + four call sites listed below) migrate to `Expected<AudioBuffer, LoadError>` (or whatever unified taxonomy is chosen — see "Error-taxonomy unification" below).
- Document `Expected`'s thread-safety and moved-from semantics in its contract docstring (audit-2 of C-2 findings F-AUDIT2-2 and the move-construction note).

## Error-taxonomy unification (decision needed in M-14, not deferred)

Currently three distinct `enum class` error taxonomies travel through
`Expected`/`LoadResult`:

| Taxonomy | Defined in | Used by |
|---|---|---|
| `AudioError` | `src/core/audio_file.hpp` | `AudioFile::open`, `read_raw_samples`, `parse_wav_header`, `parse_aiff_header`, `write_track` |
| `LoadError` | `src/core/audio_buffer.hpp` | `load_audio_mono` |
| `BlindError` | `src/modes/blind_mode.hpp` | `analyze_blind_mode` |
| `ReferenceError` | `src/modes/reference_mode.hpp` | `analyze_reference_mode`, `load_reference_tracks` |

Actual taxonomy values on current main (verified during the audit-pass discipline that caught two errors in the original draft of this section):

```cpp
enum class LoadError    { FileNotFound, InvalidFormat, ReadError, ResampleError };
enum class AudioError   { FileNotFound, InvalidFormat, UnsupportedFormat, ReadError, WriteError, InvalidRange };
```

M-14 does **not** need to unify these taxonomies — they have legitimately
different domains. But the M-14 fix-agent must explicitly decide how to
handle `LoadError` when `LoadResult` is removed:

- **Option (a) — merge `LoadError` into `AudioError` and add a new value.** Three of the four `LoadError` values (`FileNotFound`, `InvalidFormat`, `ReadError`) already exist in `AudioError`. The fourth, `ResampleError`, has **no counterpart** in `AudioError`. Pick this option only if you also add `AudioError::ResampleError`. Pro: single error taxonomy. Con: every existing `AudioError` consumer now has a value it doesn't care about.
- **Option (b) — route `ResampleError` to an existing `AudioError` value.** `InvalidFormat` is the closest semantic neighbor (a sample rate that resampling can't reach is arguably a "format mismatch"), but this is **lossy** — callers that distinguished resample failure from format failure can no longer do so. Document the loss explicitly.
- **Option (c) — keep `LoadError` as its own type.** `LoadResult` is removed, but `Expected<AudioBuffer, LoadError>` remains (with the unified `Expected` storage now backing it). Pro: no taxonomy churn for callers. Con: leaves the question of what to do if `load_audio_mono` ever needs to surface an `AudioError` value not in `LoadError`.

The original draft of this section recommended (a) on the false premise
that `LoadError` was already a strict subset of `AudioError`. **It is not.**
The audit-pass on this doc caught the mistake before M-14 dispatched (see
"Audit-pass discipline" note in `docs/deviations.md` →
`KNOWN-FAILING-COMPLETENESS-V1` for the meta-pattern).

Pick one. Record the decision in `docs/decisions/expected-api.md` (the
file C-2 created for the API-shape decision; this is its natural
extension). If the fix-agent picks (b), the loss-of-distinction note
must be in the decision file.

`BlindError` and `ReferenceError` stay as-is.

---

## Producer side (declarations + return-statement constructors)

### `src/core/audio_buffer.hpp`

- **L11.** `enum class LoadError { FileNotFound, InvalidFormat, ReadError, ResampleError };` — taxonomy to unify or rehome (see "Error-taxonomy unification" above; note that the original draft of this doc misstated this enum as `{FileNotFound, InvalidFormat, UnsupportedFormat, ReadError}` and that error was caught by the pre-dispatch audit pass).
- **L20–L36.** `class LoadResult<T>` template — the entire class is removed by M-14. Members:
  - L22: default ctor `LoadResult()` (errors with `ReadError`).
  - L23: `LoadResult(LoadError)`.
  - L24: `LoadResult(const T&)`.
  - L25: `LoadResult(T&&)`.
  - L32: `LoadError error() const` — note: returns by value, dereferences `error_` (which is `std::optional<LoadError>`).
  - L36: `std::optional<LoadError> error_;` — single-storage representation, no UB hazard.
- **L57.** `LoadResult<AudioBuffer> load_audio_mono(...)` declaration.

### `src/core/audio_file.hpp`

- **L25–~L100 (Expected class body).** `template <class T, class E> class Expected` — the type whose storage representation M-14 must cure or replace.
  - L27: `Expected() = default;`
  - L30, L36: value ctors (copy, move).
  - L42, L48: error ctors (copy, move).
  - L53–L62: copy/move ctors invoking placement-new on aligned storage (the UB hazard).
  - L71: dtor invoking destructor through `reinterpret_cast`.
  - L79, L91: copy/move assignment.
  - Accessor methods (`value() &`, `value() const&`, `value() &&`, `error() const&`, `error() &&`, `has_value()`, `operator bool`) — these are where C-2's precondition asserts live and where any `std::launder` cure inserts.
- **L155.** `static Expected<AudioFile, AudioError> AudioFile::open(...)` declaration.
- **L162 (drifts).** `Expected<std::vector<uint8_t>, AudioError> AudioFile::read_raw_samples(int64_t offset, int64_t size) const` member declaration. (Original draft of this doc named the function `read_pcm_to_buffer`; that name does not exist — the audit pass caught it.)
- **L172, L176.** `Expected<AudioInfo, AudioError> parse_wav_header(...)` / `parse_aiff_header(...)` free-function declarations.
- **L181.** `Expected<std::filesystem::path, AudioError> write_track(...)` free-function declaration.

### `src/core/audio_buffer.cpp`

- **L7.** `LoadResult<AudioBuffer> load_audio_mono(...)` body.
- **L14.** `return LoadError::FileNotFound;` (implicit-conversion ctor).
- **L23.** `return LoadError::ReadError;` (implicit-conversion ctor).
- (Plus the implicit `return AudioBuffer{...};` success path — read the full body to enumerate.)

### `src/core/audio_file.cpp`

- **L116.** `Expected<AudioFile, AudioError> AudioFile::open(...)` body.
- **L119, L125, L135, L141, L147, L165.** `return Expected<AudioFile, AudioError>(<error>);` returns inside `open`.
- **L156.** `return Expected<AudioFile, AudioError>(info_result.error());` — error-propagation via `.error()` accessor; sensitive to whichever cure path is chosen.
- **L225 (drifts).** `Expected<std::vector<uint8_t>, AudioError> AudioFile::read_raw_samples(...)` body.
- **L227, L230, L233, L238, L243, L249.** `return Expected<std::vector<uint8_t>, AudioError>(<error>);` returns inside `read_raw_samples`. Plus a success return at L252.
- **L912 (consumer).** `auto raw_result = source.read_raw_samples(byte_offset, bytes_to_read);` inside `write_track`.
- **L238.** `Expected<AudioInfo, AudioError> parse_wav_header(...)` body.
- **L241, L285, L326.** `return Expected<AudioInfo, AudioError>(<error>);` returns inside `parse_wav_header`.
  - **L285** is the `fmt_size=0xFFFFFFFF → UnsupportedFormat` deviation case — see `docs/deviations.md` `FIXTURE-MALFORMED`.
- **L340.** `Expected<AudioInfo, AudioError> parse_aiff_header(...)` body.
- **L343, L392.** `return Expected<AudioInfo, AudioError>(<error>);` returns inside `parse_aiff_header`.
- **L706–L762.** `Expected<std::filesystem::path, AudioError> write_track(...)` body.
- **L718, L729, L759.** `return Expected<std::filesystem::path, AudioError>(<error>);` returns.
- **L762.** `return Expected<std::filesystem::path, AudioError>(output_path);` — value-return on success.

### `src/modes/blind_mode.hpp`

- **L27.** `Expected<AnalysisResult, BlindError> analyze_blind_mode(...)` declaration.

### `src/modes/blind_mode.cpp`

- **L95.** `Expected<AnalysisResult, BlindError> analyze_blind_mode(...)` body.
- **L103.** `auto load_result = load_audio_mono(...);` — internal consumer of `LoadResult` (becomes consumer of unified type after M-14).
- (Read the full body to find every `return` point; only the function entry is captured here.)

### `src/modes/reference_mode.hpp`

- **L30.** `Expected<AnalysisResult, ReferenceError> analyze_reference_mode(...)` declaration.
- **L37.** `Expected<std::vector<ReferenceTrack>, ReferenceError> load_reference_tracks(...)` declaration.

### `src/modes/reference_mode.cpp`

- **L734.** `Expected<std::vector<ReferenceTrack>, ReferenceError> load_reference_tracks(...)` body.
- **L757.** `auto result = load_audio_mono(path, sample_rate);` — internal consumer.
- **L1003.** `Expected<AnalysisResult, ReferenceError> analyze_reference_mode(...)` body.
- **L1012.** `auto vinyl_result = load_audio_mono(vinyl_path, analysis_sr);` — internal consumer.
- **L1038.** `auto tracks_result = load_reference_tracks(reference_path, analysis_sr);` — internal consumer.
- **L1099.** `auto audio_file = AudioFile::open(vinyl_path);` — internal consumer.

---

## Consumer side (call sites that read `Expected`/`LoadResult` results)

### `src/main.cpp` — **post-C-2 baseline assumed**

Pre-C-2 line numbers below; rebase before annotating. C-2 introduces the
canonical `if (!opened) return 1; auto& audio_file = opened.value();` shape
that consolidates several of these sites into single-rebind references.

- **L104.** `auto result = mwaac::analyze_reference_mode(vinyl_path, reference_path);`
- **L117.** `auto audio_file = mwaac::AudioFile::open(vinyl_path);` (reference branch)
- **L160.** `auto write_result = mwaac::write_track(audio_file.value(), ...);` (reference branch — replaced by reference-rebind in C-2)
- **L225.** `auto result = mwaac::analyze_blind_mode(vinyl_path);`
- **L249.** `auto audio_file = mwaac::AudioFile::open(vinyl_path);` (blind branch)
- **L300.** `auto write_result = mwaac::write_track(audio_file.value(), ...);` (blind branch — replaced by reference-rebind in C-2)
- **L337.** `auto load_result = mwaac::load_audio_mono(vinyl_path);` (tui branch)

### `src/tui/app.cpp`

- **L289.** `auto source_result = mwaac::AudioFile::open(state.vinyl_path);`
- **L315.** `auto result = mwaac::write_track(source, output_path, sp.start_sample, sp.end_sample);`

### `src/modes/blind_mode.cpp` (intra-modes consumer)

- **L103.** `auto load_result = load_audio_mono(vinyl_path, config.analysis_sr);`

### `src/modes/reference_mode.cpp` (intra-modes consumer)

- **L757.** `auto result = load_audio_mono(path, sample_rate);`
- **L1012.** `auto vinyl_result = load_audio_mono(vinyl_path, analysis_sr);`
- **L1038.** `auto tracks_result = load_reference_tracks(reference_path, analysis_sr);`
- **L1099.** `auto audio_file = AudioFile::open(vinyl_path);`

### `tests/test_audio_file.cpp`

- **L33–L41.** `TEST_CASE("Expected basic operations", "[audio_file]")` — directly exercises the `Expected` ctor / `has_value` / `value` / `error` API. **This test must be kept passing under M-14; if the variant migration changes accessor return categories, this test is the canary.**
- **L45.** `auto result = mwaac::AudioFile::open("/nonexistent/path/to/file.wav");`
- (C-2 added `Expected: value()-on-error abort` death tests in this file. Re-grep after rebase.)

### `tests/test_lossless.cpp`

- **L218–L221.** `open_result` from `AudioFile::open`, `has_value`, `.value()` rebind.
- **L235–L236.** `write_result` from `write_track`.
- **L273–L277.** Same pattern.
- **L280–L281.** Same pattern.
- **L310–L313.** Same pattern.
- **L316, L321, L326.** Three negative-range `write_track` results.
- **L349–L353, L359–L360.** Six-rate AIFF round-trip (C-1).
- **L394–L406.** Round-trip read-back at L394–406 (`open_result`, `write_result`, `output_open`).

### `tests/test_integration.cpp`

- **L205–L208.** `open_result` (silence_path).
- **L237–L240.** `load_result` from `load_audio_mono`.
  - **Note.** Uses `load_result.ok()` (not `has_value()`). If M-14 collapses `LoadResult` into `Expected`, decide: keep `ok()` as an alias for `has_value()` for back-compat, or migrate test sites. Recommendation: drop `ok()`, migrate.
- **L262–L267.** Same `load_audio_mono` pattern (`ok()`, `value()`).
- **L338, L390, L434.** `analyze_reference_mode` results.
- **L417–L418, L436.** `vinyl_file` from `AudioFile::open` (`(void)vinyl_file` because the test pre-allocates against FIXTURE-REF).
- **L468.** `analyze_blind_mode` result.
- **L508–L526.** `analysis_result` from `analyze_blind_mode` (uses `has_value`/`value`).
- **L557–L560.** Same pattern.
- **L602–L644.** `vinyl_file_result` from `AudioFile::open` + `export_result` from `write_track`.
- **L678–L696.** `source_file` and `output_file` from `AudioFile::open`, `export_result` from `write_track`.
- **L760–L766.** `blind_result` and `ref_result`.

### Other test files

- `tests/test_audio_buffer.cpp` — does **not** consume `LoadResult` or `Expected` (verified by grep). Touches `AudioBuffer` directly.
- `tests/test_basic.cpp`, `test_correlation.cpp`, `test_analysis.cpp`, `test_music_detection.cpp`, `test_blind_mode.cpp`, `test_reference_mode.cpp` — do **not** mention either type (verified by grep).

---

## Files included by the M-14 working set

Every TU that includes either `core/audio_buffer.hpp` or `core/audio_file.hpp`
will recompile when the headers change. Listed for the fix-agent's
build-time-budget awareness:

**Includes `core/audio_buffer.hpp`:**
- `src/main.cpp`
- `src/core/audio_buffer.cpp`
- `src/modes/blind_mode.cpp`, `blind_mode.hpp`
- `src/modes/reference_mode.cpp`, `reference_mode.hpp`
- `src/tui/app.hpp`
- `tests/test_integration.cpp`
- `tests/test_audio_buffer.cpp`

**Includes `core/audio_file.hpp`** (by direct grep — see also transitive includes via `audio_buffer.hpp` etc.):
- `src/core/audio_file.cpp`
- `src/main.cpp` (transitively via `audio_buffer.hpp` and direct)
- `src/modes/*` (transitively)
- `src/tui/app.cpp` (transitively via `app.hpp`)
- `tests/test_audio_file.cpp`
- `tests/test_lossless.cpp`
- `tests/test_integration.cpp`

---

## Out of scope for M-14 (do not touch)

- The `AudioError`/`LoadError`/`BlindError`/`ReferenceError` taxonomy
  semantics (only `LoadError` merge is in scope, per recommendation above).
- The `parse_aiff_header` `numSampleFrames`/`sampleRate` decode gap — that
  is **Mi-1** and remains separate.
- The C-1 inline-scope `build_aiff_header` deviation — promoted to its own
  backlog item `AIFF-INLINE-SCOPE` (see BACKLOG.md).
- The `fmt_size=0xFFFFFFFF → UnsupportedFormat` deviation — re-evaluation
  is logged for the M-3/M-4/M-5 parser-hardening epic owner.
- `write_track`'s atomic-write semantics — already in main via M-16
  (PR #28). M-14's edits to `write_track`'s return-type plumbing must not
  regress the temp-sibling + rename idiom.

## Suggested fix-agent dispatch order within M-14

1. **Header rewrite first.** Whichever cure path is chosen (variant or launder), rewrite `audio_file.hpp::Expected` and remove `LoadResult` from `audio_buffer.hpp` in a single header edit. Update `audio_buffer.hpp::load_audio_mono` declaration to return the unified type.
2. **Producer-body edits.** `audio_file.cpp` (5 producer functions) and `audio_buffer.cpp` (1 producer function) and modes/*.cpp (3 producer functions). Pure mechanical translation.
3. **Consumer-body edits.** `main.cpp`, `tui/app.cpp`, intra-modes call sites. Most consumers use `auto`, so only sites that name the type explicitly need a rename.
4. **Tests last.** `test_audio_file.cpp::TEST_CASE("Expected basic operations")` is the API-shape canary — keep it green throughout. `test_lossless.cpp` and `test_integration.cpp` are mechanical.
5. **Build sanitizer-clean** (MWAAC_SANITIZE=ON) before declaring done. The `[basic.life]/8` UB cure must be verifiable by ASan + UBSan running the full test suite.

## Audit mandate (paraphrased from BACKLOG.md M-14)

M-14 is Tier 3 and gets the standard one-pass audit. Audit must verify:
- The cure path chosen actually cures `[basic.life]/8` UB (UBSan-clean sanitizer run with `-fsanitize=address,undefined`).
- `LoadResult` is gone from the source tree (`grep LoadResult` returns 0 hits in `src/` and `tests/`).
- The `Expected` contract docstring covers thread-safety and moved-from semantics (audit-2 of C-2 mandates).
- No regression in the C-2 `Expected` death tests (`tests/test_audio_file.cpp` L522–L658, four TEST_CASEs by name):
  - `"Expected: value() on errored Expected aborts the process"` (L522)
  - `"Expected: error() on value-bearing Expected aborts the process"` (L552)
  - `"Expected: documented contract — has_value() gates value()"` (L573)
  - `"main: failed AudioFile::open exits cleanly (no crash)"` (L608)
- No regression in M-16's atomic-write tests (`tests/test_lossless.cpp` L1053–L1314, five TEST_CASEs by name):
  - `"write_track: success leaves only target file at output path"` (L1053)
  - `"write_track: failure leaves no file at output path (parent dir missing)"` (L1082)
  - `"write_track: failure leaves no file at output path (target is a directory)"` (L1122)
  - `"write_track: long-filename concurrent writes do not collide"` (L1183)
  - `"write_track: target filename longer than NAME_MAX returns WriteError"` (L1273)
  `write_track`'s return-type plumbing must remain wired through to the same call-site behavior — gate by TEST_CASE-name match (per `docs/known-failing-tests.md` SCHEMA-V2), line numbers will drift as M-14's edits add/remove code in those files.

> **Audit-pass record.** This doc was itself audited per `feedback_pre_staged_docs_need_audit.md` before being treated as a gate input. Findings: the original draft used a fictional `read_pcm_to_buffer` (actual API: `AudioFile::read_raw_samples`) and an incorrect `LoadError` enumeration (actual: `{FileNotFound, InvalidFormat, ReadError, ResampleError}` — `ResampleError` has no `AudioError` counterpart, so the strict-subset merge claim was false). The taxonomy table, merge recommendation, and these test citations were patched as a single atomic correction commit on main before M-14 dispatch.
