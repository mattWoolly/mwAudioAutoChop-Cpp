# Decision — `AudioError` taxonomy for parser/IO paths

- **Status.** Active. Drafted as a Tier 4 prerequisite so M-3, M-4, M-5, and Mi-1 write against a defined taxonomy rather than each picking ad-hoc between `UnsupportedFormat` and `InvalidFormat` (the FIXTURE-MALFORMED deviation pattern).
- **Scope.** `mwaac::AudioError` enum in `src/core/audio_file.hpp`. Seven values; one sentence each below; producer call sites cited inline so a fix-agent or audit-agent can verify membership without re-grepping.

## Per-value contract

- **`FileNotFound`** — the OS-visible open step failed at the file-handle layer (path missing, permission denied, libsndfile `sf_open` returns null). Producers: `audio_file.cpp:136`, `audio_buffer.cpp:15`. *Note: this is a coarse approximation — `sf_open` returns null for many reasons (corrupt header, unsupported codec, EACCES). Unification under `FileNotFound` is intentional and pre-dates this taxonomy; refining it is its own backlog item, not a parser-hardening goal.*
- **`InvalidFormat`** — bytes were read but the parser determined they do not constitute a well-formed file of the claimed container type (RIFF magic missing, fmt-chunk arithmetic inconsistent, AIFF COMM size wrong, AIFF SSND chunk truncated, etc.). The malformation is *intrinsic to the bytes* — a different parser would also reject. Producers: `audio_file.cpp:158, 164, 173, 258, 343, 360, 409`.
- **`UnsupportedFormat`** — the file is well-formed but uses a feature this parser does not implement (e.g. non-PCM/non-float WAVE_FORMAT_EXTENSIBLE SubFormat, an audio codec we do not decode). The malformation is *not intrinsic*: a more complete parser would accept. Producers: `audio_file.cpp:302` (the WAVE_FORMAT_EXTENSIBLE catch-all that M-3 narrows).
- **`ReadError`** — the file was opened and the header parsed, but a subsequent byte-level read returned short or failed (`sf_readf_*` short, `ifstream::read` short, missing chunk body). Distinct from `InvalidFormat`: the parser reached a point where it expected a known number of bytes and did not get them. Producers: `audio_file.cpp:142, 152, 182, 227, 238, 243, 249, 914`, `audio_buffer.cpp:24`.
- **`WriteError`** — the output path could not be created, the temp-sibling rename failed, or `ofstream::write` returned short. Atomic-write idiom guarantees no partial file at the target path on this error (M-16 invariant). Producers: `audio_file.cpp:953, 970, 977, 983, 989, 998`.
- **`InvalidRange`** — sample range argument is out of the file's bounds (`offset < 0`, `offset + size > total_frames`, `start_sample > end_sample`). Caller-side error, not a file-content error. Producers: `audio_file.cpp:230, 233, 903`.
- **`ResampleError`** — `resample_linear` precondition violated (`sample_rate <= 0`) or output size overflows `size_t`. Mi-3 introduced this value with real producers; see `docs/deviations.md` Mi-3 entry for the structural-cure rationale. Producers: `audio_buffer.cpp:65, 79`.

## Disambiguation rule for parser-hardening epics (M-3 / M-4 / M-5 / Mi-1)

Each `return AudioError::*` call is the parser declaring what it sees at that local site, not a claim about whether the file is "really" malformed end-to-end. Choose the value that matches the local check:

- **`InvalidFormat`** for structural inconsistency the local check detects (RIFF magic missing, chunk arithmetic doesn't balance, AIFF COMM size wrong, fmt subchunk truncated). The local check itself is what failed.
- **`UnsupportedFormat`** for a documented field value the local check doesn't decode (e.g. `audio_format` not in {PCM, IEEE_FLOAT, EXTENSIBLE}; AIFF `compressionType` not NONE; WAVE_FORMAT_EXTENSIBLE SubFormat GUID outside the implemented set). The field was readable, the value just isn't covered.

**Upstream-malformation cases.** When a malformation in chunk N makes the local check at chunk N+1 resolve to `UnsupportedFormat` (e.g. the FIXTURE-MALFORMED `fmt_size = 0xFFFFFFFF` case: walker survives, lands on zeroed body, reads `audio_format = 0`, returns `UnsupportedFormat` at the local audio_format check), the local-view rule accepts the existing return value as within-contract. Refactoring to chase the upstream malformation across error-code boundaries is anti-Knuth — both values satisfy INV-PARSER-REJECT. Record the case in `docs/deviations.md` if it surfaces during a fix-agent's work, but do not expand scope to "fix" it.

Audit-agent for each Tier 4 PR verifies the chosen value against this rule by reading the local check, not by reasoning about the file's overall shape.

## Re-evaluate

If a Tier 4 fix-agent finds a case where neither value is clearly correct (e.g. truncated file with otherwise-valid header), record it as a deviation in `docs/deviations.md` under the relevant M-* item rather than expanding the enum. Enum changes go through their own backlog item with the same audit-pass discipline as M-14's `ResampleError` addition.
