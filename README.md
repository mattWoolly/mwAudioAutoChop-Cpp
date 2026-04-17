# mwAudioAutoChop

[![CI](https://github.com/mattWoolly/mwAudioAutoChop-C-/actions/workflows/ci.yml/badge.svg)](https://github.com/mattWoolly/mwAudioAutoChop-C-/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

A high-performance C++ tool for automatically splitting continuous vinyl rips into individual tracks. Features both reference-based alignment and blind gap detection modes, with a full-screen terminal UI for visual editing.

> **Lossless by design**: Output samples are byte-identical to the source. No re-encoding, no dithering, no quality loss. Your vinyl rips are too precious for anything less.

## Features

- **Lossless Export**: Bit-perfect sample extraction - audio data is copied, never re-encoded
- **Reference Mode**: Align vinyl rip to individual reference tracks (CD/digital versions)
- **Blind Mode**: Detect track boundaries using gap/silence detection
- **Interactive TUI**: Visual waveform display with keyboard-based chop point editing
- **Format Support**: WAV, AIFF, RF64 for large files >4GB (via libsndfile)
- **High-Resolution Support**: Tested with 192kHz/24-bit vinyl rips
- **Cross-Correlation**: Downsampled correlation with full-resolution refinement

## Installation

### Dependencies

- CMake 3.16+
- C++20 compiler (GCC 10+, Clang 12+)
- libsndfile

#### Ubuntu/Debian
```bash
sudo apt install build-essential cmake libsndfile1-dev
```

#### macOS
```bash
brew install cmake libsndfile pkg-config
```

### Build

```bash
git clone https://github.com/mattWoolly/mwAudioAutoChop-C-.git
cd mwAudioAutoChop-C-
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary will be at `build/bin/mwAudioAutoChop`.

### Run Tests

```bash
ctest --test-dir build
```

## Usage

### Reference Mode

Split a vinyl rip using reference tracks (e.g., CD rips or digital purchases) for alignment:

```bash
./mwAudioAutoChop reference vinyl_rip.wav \
    -r /path/to/reference_tracks/ \
    -o output_dir/
```

Options:
- `-r, --reference` - Directory containing reference tracks (sorted alphabetically/numerically)
- `-o, --output` - Output directory for split tracks
- `-v, --verbose` - Show detailed alignment info
- `--dry-run` - Preview splits without writing files

### Blind Mode

Split using automatic gap detection (no reference needed):

```bash
./mwAudioAutoChop blind vinyl_rip.wav -o output_dir/
```

Options:
- `-o, --output` - Output directory for split tracks
- `--min-gap` - Minimum gap duration in seconds (default: 2.0)
- `--max-gap` - Maximum gap duration in seconds (default: 30.0)
- `-v, --verbose` - Show detailed detection info

### Interactive TUI

Launch the visual editor:

```bash
./mwAudioAutoChop tui vinyl_rip.wav -o output_dir/
```

#### TUI Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Tab` | Next chop point |
| `Shift+Tab` / `P` | Previous chop point |
| `+` / `=` / `]` | Move marker right (fine adjust) |
| `-` / `_` / `[` | Move marker left (fine adjust) |
| `Up` / `Down` | Zoom in/out |
| `Home` / `End` | Pan to start/end |
| `H` | Toggle help overlay |
| `Enter` | Export all tracks |
| `Q` | Quit |

## How It Works

### Reference Mode
1. Loads vinyl rip and reference tracks
2. Detects music start (skips lead-in groove noise)
3. Cross-correlates each reference track against the vinyl (using downsampled audio for speed, then refining at full resolution)
4. Finds optimal alignment position with sample-accurate precision
5. Exports tracks with byte-perfect sample data (no re-encoding)

### Blind Mode
1. Computes RMS energy envelope
2. Estimates noise floor from quietest regions
3. Detects gaps where energy drops below threshold
4. Scores gaps by depth and duration
5. Creates split points at gap boundaries

### Lossless Guarantee

**Your vinyl rips are preserved exactly as recorded.** This tool guarantees byte-identical sample data between input and output:

- **No re-encoding**: Sample data is copied directly, not decoded and re-encoded
- **No dithering**: No processing, normalization, or bit-depth conversion
- **No resampling**: Sample rate is preserved exactly
- **Bit-perfect verification**: The test suite compares SHA-256 checksums of sample data regions to verify lossless operation

The only difference between your source file and the output tracks is the WAV header (which contains the new file length). The actual audio samples are byte-for-byte identical to the corresponding region in your source file.

This is critical for archival workflows where the vinyl rip represents an irreplaceable master recording.

## Project Structure

```
src/
├── main.cpp              # CLI entry point
├── core/
│   ├── audio_file.hpp    # Audio I/O, header parsing, lossless export
│   ├── audio_buffer.hpp  # Audio loading for analysis
│   ├── correlation.hpp   # Cross-correlation (downsampled + refined)
│   ├── analysis.hpp      # RMS, spectral features
│   └── music_detection.hpp
├── modes/
│   ├── reference_mode.hpp  # Reference alignment pipeline
│   └── blind_mode.hpp      # Gap detection pipeline
└── tui/
    ├── app.hpp           # FTXUI application
    └── waveform.hpp      # Waveform rendering
```

## License

MIT License - see LICENSE file for details.

## Acknowledgments

- Original Python implementation: [audio-auto-chop](https://github.com/mattWoolly/audio-auto-chop)
- Terminal UI: [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
- Audio I/O: [libsndfile](https://github.com/libsndfile/libsndfile)
