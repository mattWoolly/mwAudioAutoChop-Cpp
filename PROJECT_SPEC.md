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

The diagram below matches the actual `src/` tree as of T8-SPEC-ARCH-DRIFT close-out (2026-06-02). It describes current state, not aspiration; when the tree changes substantively, this section is the canonical home for the updated description (per the DOC-3 living-document discipline applied to PROJECT_SPEC.md).

```
src/
├── main.cpp                       # Entry point + CLI parsing
├── cli/                           # (reserved placeholder; .gitkeep-only)
├── core/                          # Core audio processing
│   ├── alignment_result.hpp       # AlignmentResult + TrackOffset + DriftModel structs
│   ├── analysis.{cpp,hpp}         # RMS / ZCR / spectral-flatness analyzers
│   ├── analysis_result.hpp        # AnalysisResult struct
│   ├── audio_buffer.{cpp,hpp}     # AudioBuffer container + ops
│   ├── audio_file.{cpp,hpp}       # libsndfile + parser-validated I/O
│   ├── audio_info.hpp             # AudioInfo metadata struct
│   ├── correlation.{cpp,hpp}      # Cross-correlation (time-domain + FFT)
│   ├── drift_model.cpp            # DriftModel impl (struct in core/alignment_result.hpp)
│   ├── frame_sample_bridge.hpp    # Typed sample/frame index discipline
│   ├── music_detection.{cpp,hpp}  # estimate_noise_floor + detect_music_start
│   ├── pocketfft_hdronly.h        # Vendored BSD-3 FFT (see THIRD_PARTY_LICENSES.md)
│   ├── split_point.hpp            # SplitPoint data structure
│   └── verbose.hpp                # `g_verbose` + `verbose()` log helpers
├── modes/                         # Mode pipelines
│   ├── blind_mode.{cpp,hpp}       # Blind-mode pipeline + BlindModeConfig
│   ├── reaper_export.{cpp,hpp}    # REAPER project (.rpp) writer
│   └── reference_mode.{cpp,hpp}   # Reference-mode pipeline
├── tui/                           # Terminal UI (FTXUI-based)
│   ├── app.{cpp,hpp}              # FTXUI screen loop + run_tui entry
│   ├── app_handlers.{cpp,hpp}     # State-mutator harness (Tier 7)
│   └── waveform.{cpp,hpp}         # render_waveform Unicode rendering
└── utils/                         # (reserved placeholder; .gitkeep-only)
```

Notes on the legacy diagram (pre-T8-SPEC-ARCH-DRIFT):
- `src/cli/` and `src/utils/` were scaffolded at project init but never populated; CLI parsing lives in `main.cpp` and DSP helpers folded into `src/core/*` modules. The placeholder directories are preserved for narrative continuity.
- Earlier diagrams cited file-level stubs (`alignment.hpp`, `editor.hpp`, `src/utils/dsp.hpp`) that never landed; the corresponding functionality was either absorbed into existing modules (alignment → reference_mode; editor → app + app_handlers) or never required (DSP helpers).
- The legacy `*.hpp`-only stub naming (`reference.hpp`, `blind.hpp`) was superseded by the `*_mode.{cpp,hpp}` convention during Tier 5/6 cycle work.

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
