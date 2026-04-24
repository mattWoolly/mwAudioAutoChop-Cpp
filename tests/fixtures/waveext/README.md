# FIXTURE-WAVEEXT — 24-bit WAVE_FORMAT_EXTENSIBLE corpus

Reproducible WAV files whose `fmt ` chunk uses the WAVE_FORMAT_EXTENSIBLE
tag (`audio_format = 0xFFFE`). The bytes are assembled by hand because
libsndfile, given a 24-bit stereo write request, will emit the legacy
`audio_format = 0x0001` tag and therefore fails to exercise the
EXTENSIBLE parsing path that Pro Tools, REAPER, and modern Audacity
actually produce.

## Generated artifacts

Built by `gen_waveext_fixture` into `${CMAKE_BINARY_DIR}/fixtures/waveext/`:

| File                                          | Channels | Bits | SubFormat GUID                              | Notes                                        |
|-----------------------------------------------|----------|------|---------------------------------------------|----------------------------------------------|
| `pcm_24bit_stereo.wav`                        | 2        | 24   | `00000001-0000-0010-8000-00aa00389b71` PCM  | The format multichannel-aware encoders emit. |
| `pcm_24bit_5ch.wav`                           | 5        | 24   | `00000001-0000-0010-8000-00aa00389b71` PCM  | EXTENSIBLE is required by spec at >2 ch.     |
| `pcm_extensible_unsupported_subformat.wav`    | 2        | 24   | `00000000-0000-0000-0000-000000000000`      | Unknown SubFormat -> expect UnsupportedFormat. |

Plus `manifest.txt` listing each file's `data_offset`, `data_size`,
`channels`, and `bits_per_sample` (one entry per line).

Sample data is a deterministic 24-bit signed sawtooth (per-fixture stride,
per-channel offset, no RNG dependency); byte-identity round-trip tests
can compare against the source's data section directly.

## Invariants exercised

- **INV-WAVEEXT-1**: `parse_wav_header` (and therefore `AudioFile::open`)
  accepts a WAVE_FORMAT_EXTENSIBLE file whose SubFormat GUID identifies
  PCM (or IEEE float), regardless of channel count, and returns an
  `AudioInfo` whose `channels`, `sample_rate`, `bits_per_sample`,
  `data_offset`, and `data_size` match the on-disk header.
- **INV-WAVEEXT-2**: `parse_wav_header` returns `UnsupportedFormat` (not
  `InvalidFormat`, not a crash) when the SubFormat GUID is neither PCM
  nor IEEE float.
- **INV-WAVEEXT-3**: `write_track` of an EXTENSIBLE source produces an
  output WAV whose data section is byte-identical to the requested
  region of the source's data section. (This invariant ties the fixture
  to the lossless contract; it is the failure mode logged as
  NEW-WAVEEXT-WRITE.)

## Backlog items that depend on this fixture

- **M-3** — WAVE_FORMAT_EXTENSIBLE (0xFFFE) rejected. The parse-path
  fixture (`pcm_24bit_stereo.wav`, `pcm_24bit_5ch.wav`,
  `pcm_extensible_unsupported_subformat.wav`) is the input corpus M-3's
  fix must satisfy.
- **NEW-WAVEEXT-WRITE** — 24-bit 2-channel WAV write failure surfaced in
  Phase 0 of the integration suite. The lossless round-trip test added
  alongside this fixture is the regression gate for that defect.

## Tests gated on this fixture

`tests/test_audio_file.cpp`:
- `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM 24-bit stereo accepted`  `[!shouldfail]`
- `parse_wav_header: WAVE_FORMAT_EXTENSIBLE PCM 24-bit 5-channel accepted`  `[!shouldfail]`
- `parse_wav_header: extensible with unknown SubFormat returns UnsupportedFormat`  `[!shouldfail]`

`tests/test_lossless.cpp`:
- `Lossless: 24-bit 2-ch extensible WAV round-trip preserves bytes`  `[!shouldfail]`

The `[!shouldfail]` tag is removed in the same PR that lands the M-3
(plus EXTENSIBLE-write) fix, at which point the tests must pass.
