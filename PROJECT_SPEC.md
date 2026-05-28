# mwAudioAutoChop C++ - Project Specification

## Overview

Rebuild of the Python `audio-auto-chop` vinyl splitting utility in modern C++. This is a **rebuild**, not a line-by-line port. The implementation should follow idiomatic C++ conventions, modern standards (C++20+), and proper memory/resource management patterns.

## Core Functionality (from Python original)

### 1. Reference Mode
- Align vinyl rip to reference audio (CD rip, FLAC, etc.) using cross-correlation
- Per-track alignment for accurate boundary detection
- Chromagram-based correlation for robustness against mastering differences
- Piecewise drift correction for vinyl speed variations
- Lead-in detection and skipping

### 2. Blind Mode
- Detect track boundaries without reference audio
- Uses spectral flatness, RMS energy, and onset detection
- Adaptive noise floor estimation
- Gap detection between tracks

### 3. Lossless I/O Guarantee
- Output samples are byte-identical to source (no DSP processing)
- Raw byte-copy splitting from source file
- Support for WAV, RF64, and AIFF formats
- Header parsing and generation

## New Features for C++ Version

### 4. Interactive TUI (Terminal User Interface)
Full-screen terminal interface inspired by hardware samplers (Elektron, MPC, vintage samplers):

- **Waveform Visualization**: Display audio waveforms using Unicode block characters
- **Chop Point Editing**: Visual markers for split points that can be adjusted
- **Keyboard Navigation**: 
  - Arrow keys for navigation
  - Zoom in/out on waveform
  - Adjust chop points with precision
  - Preview regions
- **Help Overlay**: Visible keyboard shortcuts
- **Split Preview**: Before/after visualization of proposed splits

### 5. Interactive Workflow
1. Load audio file
2. Auto-detect or manually place chop points
3. Visually review and adjust each chop
4. Preview chops (optional audio playback)
5. Export with confirmation

## Technical Requirements

### Build System
- CMake (3.20+)
- Support for Linux, macOS (Windows optional)
- Proper dependency management

### Dependencies (actual, post-implementation)
- **libsndfile**: Audio I/O (WAV, AIFF). System pkg-config preferred; `FetchContent` fallback pins `1.2.2`. Normalized as the `mwaac_sndfile` interface target regardless of source.
- **pocketfft** (vendored in-tree at `src/core/pocketfft_hdronly.h`, BSD-3-Clause; see [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md)): FFT for correlation and analysis. Header-only; no linkage. Replaces the originally-considered FFTW3/KissFFT (FFTW3 was explicitly removed per backlog item M-12).
- **FTXUI** (FetchContent, pinned `v5.0.0`): Terminal UI. Replaces the originally-considered ncurses alternative.
- **Catch2** (FetchContent, pinned `v3.5.2`): Test framework. Selected per the "Catch2 or GoogleTest" code-standards option.
- *Not used*: Eigen — the originally-considered "optional matrix operations" dependency was never needed; correlation, drift modeling, and envelope analysis are implemented directly against `std::vector<float>` and `std::span<const float>` without a matrix library.

### Code Standards
- C++20 or C++23
- RAII for all resource management
- `std::expected` or exceptions for error handling
- `std::span`, `std::string_view` where appropriate
- Strong typing (avoid raw pointers, use smart pointers)
- Comprehensive unit tests (Catch2 or GoogleTest)

### Architecture
```
src/
├── main.cpp              # Entry point
├── cli/                  # Command-line parsing
├── core/                 # Core audio processing
│   ├── audio_file.hpp    # Audio I/O abstraction
│   ├── correlation.hpp   # Cross-correlation
│   ├── analysis.hpp      # Feature extraction (RMS, spectral)
│   ├── alignment.hpp     # Reference alignment
│   └── split_points.hpp  # Data structures
├── modes/
│   ├── reference.hpp     # Reference mode pipeline
│   └── blind.hpp         # Blind mode pipeline
├── tui/                  # Terminal UI
│   ├── waveform.hpp      # Waveform rendering
│   ├── editor.hpp        # Chop point editor
│   └── app.hpp           # Main TUI application
└── utils/                # Utilities
    └── dsp.hpp           # DSP helpers
```

## Workflow Process

### Development Workflow
1. **Atomic PRs**: Each feature/fix in a separate branch and PR
2. **Agent Review**: PRs reviewed by other agents before merge
3. **Sequential Dependencies**: Core functionality before TUI
4. **Parallel Work**: Independent modules can be developed in parallel

### Git Workflow
- Main branch: `main`
- Feature branches: `feature/<name>`
- Bugfix branches: `fix/<name>`
- All work via PRs with agent review
- Orchestrator merges approved PRs

### Quality Gates
- Code compiles without warnings under the project warning set, enforced as errors via the `MWAAC_WERROR` CMake option (default `ON`; CI keeps it on). Non-MSVC warning set is `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-align -Wnon-virtual-dtor -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough` (plus `-Werror` when `MWAAC_WERROR=ON`); MSVC warning set is `/W4 /permissive-` (plus `/WX` when `MWAAC_WERROR=ON`). See the `mwaac_apply_flags` helper in `CMakeLists.txt` — the warning set is scoped to *our* targets only, not third-party `FetchContent` dependencies (FTXUI / libsndfile / Catch2 legitimately produce warnings under `-Wconversion` / `-Wold-style-cast`).
- AddressSanitizer + UndefinedBehaviorSanitizer can be enabled via the `MWAAC_SANITIZE` CMake option (default `OFF`); CI runs a dedicated sanitizer job (Debug build) with this on.
- Unit + integration tests pass under `ctest`.
- Agent code review approval (audit-agent pattern; verdicts CLEAN / CONCERNS / HALT).
- Documentation for public APIs and project invariants — invariants tracked in [`docs/invariants.md`](docs/invariants.md) as a living document.

## Testing Strategy

### Unit Tests
- Audio I/O: Read/write various formats
- Correlation: Known signal alignment
- Analysis: Feature extraction accuracy
- Split points: Boundary logic

### Integration Tests
- Full pipeline with test audio files
- Lossless verification (byte comparison)
- Reference mode with known reference
- Blind mode with known track count

### QA Step
- Final validation of all functionality
- Performance benchmarks
- Edge case testing
- Issues submitted to backlog if found
