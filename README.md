# mwAudioAutoChop

[![CI](https://github.com/mattWoolly/mwAudioAutoChop-Cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/mattWoolly/mwAudioAutoChop-Cpp/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**Turn one long vinyl rip into clean, perfectly-cut individual tracks — without ever re-encoding a single sample.**

Designed for vinyl archivists who want bit-perfect output, tight alignment to a reference, and a tool that *actually handles the messy reality* of vinyl rips: side flips, fade-ins, runout grooves, and stylus-drop noise.

> **Lossless by design.** Output samples are byte-identical to the source. No re-encoding, no dithering, no quality loss. Your vinyl rips are too precious for anything less.

---

## What it does

Feed it a continuous vinyl rip and either:

- **Reference mode** — a folder of reference tracks (digital master, CD rip, whatever you have), and the tool aligns each one sample-accurately and writes out individually-chopped files.
- **Blind mode** — no reference needed; detects inter-track gaps by energy envelope.
- **TUI mode** — interactive waveform editor for when you want hands-on control.

The chop points are found by correlation + refinement. The audio samples themselves are *copied*, never decoded and re-encoded.

## Quickstart

```bash
# Reference mode (most accurate)
mwAudioAutoChop reference vinyl_rip.wav -r reference_tracks/ -o output/

# Blind mode (no reference needed)
mwAudioAutoChop blind vinyl_rip.wav -o output/

# Preview without writing
mwAudioAutoChop reference vinyl_rip.wav -r reference_tracks/ -o output/ --dry-run -v
```

## Why this and not a simpler tool

Off-the-shelf chopping tools assume the vinyl rip and the reference line up cleanly. Real vinyl rips don't. This tool is built for the failure modes:

- **Flip gaps** — the 10-60 s of silence while you flip the record. Detected, skipped, and excluded from every output track (both from the end of the last side-A track and the start of the first side-B track).
- **Fade-in intros** — a track that opens with a quiet fade from silence. The tool preserves the full fade; it doesn't snap to the first "audible" moment.
- **Reference digital silence vs. vinyl surface noise** — a reference master might start with 100 ms of exact zeros. The vinyl at the same position has groove noise. Rather than pretending the vinyl is silent, the tool recognises the difference and starts the output at the first real sample.
- **Continuous / mixed albums** — works on DJ-mix and electronic records where tracks blend into each other with no gaps at all.
- **Low-correlation tracks** — falls back gracefully instead of cascading a single bad match into every subsequent track.
- **Vinyl drift** — sub-percent pitch wander across a side doesn't throw alignment.

## The pipeline (reference mode)

Each track runs through four stages:

1. **Flip-gap skip.** If the expected start sits inside a long silence (≥ 3 s sustained below -45 dB), jump to where music actually resumes. Short silences — fade-ins, inter-track pauses — are left intact.
2. **Coarse correlation.** Full-length reference vs. a vinyl window, 100× downsampled. Restricted to valid lags so spurious negative-lag matches can't occur.
3. **Multi-snippet fine refinement.** Three 5-second snippets from the reference (post-music-onset, 40 % through, 80 % through) correlate at full resolution against a narrow vinyl window. The post-onset snippet is the primary estimator; the other two validate and recover when the first is weak. Confidence-weighted decision with explicit logging of disagreement.
4. **End trimming.** Track N ends at `start + ref_duration + tail`, unless an inter-track flip silence is detected — in which case we trim precisely to the silence boundary. The last track is capped before the runout groove.

Plus one more small but important thing: if the reference starts with **digital silence** (exact zeros), the output begins on the first real sample instead of copying vinyl noise into that region.

Every stage is logged with `-v`, including per-snippet votes and confidence values, so you can see exactly *why* each track landed where it did.

## Installation

### Dependencies

- CMake 3.16+
- C++20 compiler (GCC 10+, Clang 12+)
- libsndfile
- pkg-config (macOS)

#### Ubuntu / Debian
```bash
sudo apt install build-essential cmake pkg-config libsndfile1-dev
```

#### macOS
```bash
brew install cmake pkg-config libsndfile
```

### Build

```bash
git clone https://github.com/mattWoolly/mwAudioAutoChop-Cpp.git
cd mwAudioAutoChop-Cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary lives at `build/bin/mwAudioAutoChop`.

### Run tests

```bash
ctest --test-dir build
```

## Usage

### Reference mode

```bash
mwAudioAutoChop reference vinyl_rip.wav -r reference_tracks/ -o output/ [options]
```

| Option | Description |
|---|---|
| `-r, --reference` | Directory containing reference tracks. Files are natural-sorted (so `Track 2.wav` < `Track 10.wav`). |
| `-o, --output` | Output directory for chopped tracks. |
| `-v, --verbose` | Show per-track alignment details, snippet votes, flip-gap skips, end-trim decisions. |
| `--dry-run` | Analyse and print what it *would* do, without writing files. |

Reference files are natural-sorted by filename. If your files include track numbers anywhere in the name (`01 - Song.wav`, `Track02.flac`, etc.), they'll sort correctly.

### Blind mode

```bash
mwAudioAutoChop blind vinyl_rip.wav -o output/ [options]
```

| Option | Description |
|---|---|
| `-o, --output` | Output directory. |
| `--min-gap` | Minimum gap duration in seconds to split on (default `2.0`). |
| `--max-gap` | Maximum gap duration (default `30.0`). |
| `-v, --verbose` | Show gap detection details. |

### Interactive TUI

```bash
mwAudioAutoChop tui vinyl_rip.wav -o output/
```

| Key | Action |
|---|---|
| `Tab` / `Shift+Tab` | Next / previous chop point |
| `+` `=` `]` / `-` `_` `[` | Nudge the selected marker right / left |
| `Up` / `Down` | Zoom in / out |
| `Home` / `End` | Pan to start / end |
| `H` | Toggle help overlay |
| `Enter` | Export all tracks |
| `Q` | Quit |

## Lossless guarantee

**Your vinyl rips are preserved exactly as recorded.** This tool guarantees byte-identical sample data between input and output:

- **No re-encoding.** Sample data is copied directly, not decoded and re-encoded.
- **No dithering.** No processing, normalization, or bit-depth conversion.
- **No resampling.** Sample rate is preserved exactly.
- **Verified.** The test suite compares SHA-256 checksums of sample-data regions to confirm byte-identical output.

The only difference between your source file and the output tracks is the WAV header (which contains the new file length). The actual audio samples are byte-for-byte identical to the corresponding region in your source file.

This matters when your vinyl rip is an irreplaceable master recording — because it is.

## Format support

- **WAV** — including **RF64** for files > 4 GB (common for 192 kHz / 24-bit rips).
- **AIFF / AIF**
- **FLAC, MP3, OGG, M4A** for reference tracks (any format libsndfile reads).

## Project structure

```
src/
├── main.cpp              # CLI entry point
├── core/
│   ├── audio_file.hpp    # Audio I/O, header parsing, lossless export
│   ├── audio_buffer.hpp  # Audio loading for analysis
│   ├── correlation.hpp   # Multi-stage cross-correlation
│   ├── music_detection.hpp
│   └── analysis.hpp
├── modes/
│   ├── reference_mode.hpp  # Reference alignment pipeline
│   └── blind_mode.hpp      # Gap detection pipeline
└── tui/
    ├── app.hpp           # FTXUI application
    └── waveform.hpp      # Waveform rendering
tests/                    # Catch2 unit + integration tests
```

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- Original Python implementation: [audio-auto-chop](https://github.com/mattWoolly/audio-auto-chop)
- Terminal UI: [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- Audio I/O: [libsndfile](https://github.com/libsndfile/libsndfile)
