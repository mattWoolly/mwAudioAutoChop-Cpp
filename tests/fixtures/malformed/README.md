# FIXTURE-MALFORMED — Truncated and malformed header corpus

A small corpus of hand-crafted, deliberately malformed WAV / RF64 / AIFF
files. Every blob is at most 256 bytes, so we generate them at build time
from `generate.cpp` rather than committing binary artefacts. The byte
recipes are documented per blob below; a reader can reconstruct any of
them by hand from this README and `generate.cpp`.

The corpus exists to enforce one invariant family on the header parsers:

> **INV-PARSER-BOUNDED.** `parse_wav_header` and `parse_aiff_header`
> return in bounded time on every input — no crash, no infinite loop,
> no gigabyte-scale allocation — for every member of this corpus.
>
> **INV-PARSER-REJECT.** Each member of this corpus elicits either
> `AudioError::InvalidFormat` or `AudioError::UnsupportedFormat`.
> Cases that the parser is expected to handle correctly only after a
> backlog item lands (M-2, M-5) are tagged `[!shouldfail]` until then.

The whole corpus is exercised under ASan + UBSan in the `audio_file`
test target. If a future change makes any blob crash, loop, or allocate
absurdly, the test will fail under sanitizers in a way that names the
offending blob.

## Backlog items this fixture serves

- **FIXTURE-MALFORMED** — this item.
- **M-3** — WAVE_FORMAT_EXTENSIBLE handling. (See FIXTURE-WAVEEXT for
  the positive case; this fixture covers the negative-tag rejection
  path: format-tag = 0, format-tag = 0x0002 ADPCM.)
- **M-4** — RF64 ds64 placement. Covers "RF64 with no ds64 anywhere"
  and "RF64 with truncated ds64".
- **M-5** — AIFF SSND offset honoured. The non-zero-offset case is
  tagged `[!shouldfail]` until M-5 lands, at which point it becomes a
  positive-acceptance case (and should move to a different fixture).
- **M-2** — parser/libsndfile cross-check. The "data chunk claiming
  size > file size" case is tagged `[!shouldfail]` until M-2 lands.

## The blobs

Every blob lives at `${CMAKE_BINARY_DIR}/tests/fixtures/malformed/<name>`
after the `malformed_fixture` target builds. Names match
`manifest.txt`. Sizes are in bytes (≤ 256 throughout).

| # | Name | Size | Expected | Why malformed |
|---|------|------|----------|---------------|
| 1 | `tiny_8.wav` | 8 | InvalidFormat | File shorter than the 12-byte RIFF/WAVE preamble. |
| 2 | `tiny_43.wav` | 43 | InvalidFormat | File one byte short of the parser's 44-byte minimum. |
| 3 | `riff_no_chunks.wav` | 44 | InvalidFormat | Valid `RIFF`/`WAVE` preamble; no `fmt`/`data` chunks anywhere. |
| 4 | `fmt_size_max.wav` | 64 | UnsupportedFormat | `fmt ` chunk_size = 0xFFFFFFFF; chunk walker must terminate without reading past the buffer. The body bytes are zero-padded so `audio_format == 0` is read, falling through to the unsupported-tag branch. |
| 5 | `fmt_size_overflows_file.wav` | 64 | UnsupportedFormat | `fmt ` chunk_size = 0x00010000 (much larger than the 64-byte file). Same body-interpretation path as `fmt_size_max.wav`. |
| 6 | `data_before_fmt.wav` | 80 | InvalidFormat | `data` chunk appears before `fmt `. The spec allows out-of-order chunks but our parser must not be tripped by it; here the `fmt ` chunk is also malformed (size 0) so the file is rejected as InvalidFormat. |
| 7 | `data_size_overflows_file.wav` | 64 | InvalidFormat (pending M-2) | `data` chunk_size = 0x10000000 — far larger than the 64-byte file. Tagged `[!shouldfail]` until M-2's bytes-per-frame cross-check lands. |
| 8 | `rf64_no_ds64.wav` | 64 | InvalidFormat | `RF64`/`WAVE` magic but no `ds64` chunk; parser falls through with rf64_data_size = 0 and currently still reports InvalidFormat because no `data` chunk follows. |
| 9 | `rf64_ds64_truncated.wav` | 56 | InvalidFormat | `RF64`/`WAVE` + `ds64` chunk_size = 12 (< the 24-byte minimum payload). Parser ignores the truncated ds64; subsequent absence of a valid `fmt`/`data` pair surfaces as InvalidFormat. |
| 10 | `aiff_no_comm_no_ssnd.aiff` | 54 | InvalidFormat | Valid `FORM`/`AIFF` envelope but neither `COMM` nor `SSND` chunk. |
| 11 | `aiff_ssnd_size_lt_8.aiff` | 80 | InvalidFormat | `SSND` chunk_size = 4 (< 8). The parser's `chunk_size >= 8` guard prevents the `chunk_size - 8` underflow; absence of populated SSND data surfaces as `data_size = 0` then InvalidFormat (no COMM either, here). |
| 12 | `aiff_ssnd_offset_nonzero.aiff` | 96 | InvalidFormat (pending M-5) | Valid AIFF with a non-zero `SSND` offset field. Tagged `[!shouldfail]` until M-5 honours the offset. |
| 13 | `aiff_comm_size_lt_18.aiff` | 80 | InvalidFormat | `COMM` chunk_size = 12 (< 18). `found_comm` stays false; no SSND either. |
| 14 | `wav_fmt_audio_format_zero.wav` | 64 | UnsupportedFormat | `fmt ` chunk has audio_format = 0 (not in the PCM/float/A-law/μ-law allow-list). |
| 15 | `wav_fmt_adpcm.wav` | 64 | UnsupportedFormat | `fmt ` chunk has audio_format = 0x0002 (Microsoft ADPCM). |

> Note: the spec calls out 14 malformations; we use 15 names because the
> "fmt size larger than file" malformation is split into the corner case
> `chunk_size = 0xFFFFFFFF` (which is also the RF64 placeholder sentinel)
> and the more pedestrian `chunk_size = 0x00010000`. Both code paths are
> worth exercising.

## How to regenerate by hand

`generate.cpp` is the canonical recipe. Each blob is produced by a small
function that pushes bytes into a `std::vector<uint8_t>` then writes to a
named output path. To reproduce blob `<name>` from a shell:

```sh
# Configure once.
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DMWAAC_SANITIZE=ON
# Build the generator and run it.
cmake --build build --target malformed_fixture
# Inspect with hexdump.
xxd build/tests/fixtures/malformed/<name>
```

If you need to add a new malformation, edit `generate.cpp` (add a new
emit function and a new `manifest.txt` entry — both are checked into
this directory) and re-run the build.

## Reading the manifest

`manifest.txt` is the single source of truth for "which files exist and
what should happen when they're parsed." Each non-comment line:

```
<filename>   <expected>   <description-rest-of-line>
```

`expected` is one of:

- `InvalidFormat` — parser returns `AudioError::InvalidFormat`.
- `UnsupportedFormat` — parser returns `AudioError::UnsupportedFormat`.
- `InvalidFormat-pending-M-N` — currently does NOT return InvalidFormat
  (e.g. it succeeds with bogus data); the test tags this case
  `[!shouldfail]` so Catch2 reports it as expected-failure until M-N
  lands. When M-N lands, change the tag to plain `InvalidFormat`.

The test in `tests/test_audio_file.cpp` reads the manifest at runtime
(its path is injected via a compile-time define) and walks every entry.
