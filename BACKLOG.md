# mwAudioAutoChop C++ - Development Backlog

## Status Legend
- `[ ]` Pending
- `[~]` In Progress
- `[x]` Complete
- `[!]` Blocked

---

## Phase 1: Foundation & Build System

### AAC-CPP-001: Project Setup & CMake Configuration
**Priority:** P0 (Critical)  
**Status:** `[x]`  
**Estimated Effort:** Small  
**Dependencies:** None

**Description:**
- Initialize CMake project structure
- Set up directory layout as per PROJECT_SPEC.md
- Configure C++20 standard
- Add .gitignore for build artifacts
- Create placeholder files for initial structure

**Acceptance Criteria:**
- `cmake -B build && cmake --build build` succeeds
- Project compiles with empty main.cpp
- Directory structure matches spec

---

### AAC-CPP-002: Dependency Integration
**Priority:** P0 (Critical)  
**Status:** `[x]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-001

**Description:**
- Integrate libsndfile for audio I/O
- Integrate FFTW3 or KissFFT for FFT
- Integrate FTXUI for terminal UI
- Integrate Catch2 for testing
- CMake find_package or FetchContent setup

**Acceptance Criteria:**
- All dependencies resolve and link
- Simple test using each library compiles

---

### AAC-CPP-003: Core Data Structures
**Priority:** P0 (Critical)  
**Status:** `[x]`  
**Estimated Effort:** Small  
**Dependencies:** AAC-CPP-001

**Description:**
Create core data structures:
- `AudioInfo`: File metadata (sample rate, channels, bit depth, etc.)
- `SplitPoint`: Start/end samples, confidence, evidence
- `AnalysisResult`: Collection of split points with metadata
- `AlignmentResult`: Alignment data from reference mode

**Acceptance Criteria:**
- All structs compile with proper C++20 idioms
- Unit tests for basic operations

---

## Phase 2: Audio I/O (Lossless Foundation)

### AAC-CPP-004: WAV/AIFF Header Parsing
**Priority:** P0 (Critical)  
**Status:** `[x]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-003

**Description:**
- Parse WAV (RIFF) and RF64 headers to find data chunk offset
- Parse AIFF headers to find SSND chunk offset
- Extract bit depth, channels, sample rate
- Cross-validate with libsndfile

**Acceptance Criteria:**
- Correctly parse test WAV and AIFF files
- Data offset matches manual inspection
- Unit tests with various bit depths (16, 24, 32)

---

### AAC-CPP-005: Lossless Track Export
**Priority:** P0 (Critical)  
**Status:** `[~]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-004

**Description:**
- Raw byte-copy from source file at calculated offset
- Build valid WAV header for output
- Build valid AIFF header for output
- No DSP processing, no format conversion

**Acceptance Criteria:**
- Output bytes are identical to source range (SHA-256 verification)
- Output files play correctly in audio software
- Unit tests verify lossless invariant

---

### AAC-CPP-006: Audio Loading for Analysis
**Priority:** P1 (High)  
**Status:** `[x]`  
**Estimated Effort:** Small  
**Dependencies:** AAC-CPP-002

**Description:**
- Load audio as float samples for analysis using libsndfile
- Mono conversion for correlation
- Resampling to analysis rate (22050 Hz)

**Acceptance Criteria:**
- Load test files successfully
- Mono conversion produces expected output
- Unit tests for various formats

---

## Phase 3: Signal Processing & Analysis

### AAC-CPP-007: FFT-Based Cross-Correlation
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-002, AAC-CPP-006

**Description:**
- Implement O(n log n) FFT-based cross-correlation
- Return lag (offset) and peak correlation value
- Preprocessing: highpass filter + RMS normalization

**Acceptance Criteria:**
- Correctly aligns known offset signals
- Performance acceptable for multi-minute files
- Unit tests with synthetic signals

---

### AAC-CPP-008: RMS Energy Computation
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Small  
**Dependencies:** AAC-CPP-006

**Description:**
- Frame-based RMS energy calculation
- Configurable frame length and hop size
- Return vector of per-frame RMS values

**Acceptance Criteria:**
- Matches expected RMS for known signals
- Performance suitable for real-time display
- Unit tests

---

### AAC-CPP-009: Spectral Analysis Features
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-007

**Description:**
- Spectral flatness (noise vs. tonal detection)
- Spectral centroid
- Zero-crossing rate
- Optional: Spectral contrast

**Acceptance Criteria:**
- Features match Python librosa output within tolerance
- Unit tests with known audio characteristics

---

### AAC-CPP-010: Chromagram Computation
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-007

**Description:**
- CQT-based or STFT-based chromagram (12 pitch classes)
- Used for reference correlation (invariant to EQ/mastering)

**Acceptance Criteria:**
- Produces 12-bin chroma features per frame
- Unit tests with known musical content

---

### AAC-CPP-011: Onset Detection
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-009

**Description:**
- Onset strength envelope
- Peak picking for onset frames
- Onset detection for boundary refinement

**Acceptance Criteria:**
- Detects onsets in test audio
- Unit tests with click tracks

---

## Phase 4: Alignment & Mode Implementation

### AAC-CPP-012: Music Start Detection
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-008, AAC-CPP-009

**Description:**
- Detect where music begins (skip lead-in groove noise)
- Uses RMS + spectral flatness thresholding
- Sustained region detection

**Acceptance Criteria:**
- Correctly identifies music start in vinyl rip
- Unit tests with synthetic lead-in + music

---

### AAC-CPP-013: Per-Track Alignment
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** AAC-CPP-007, AAC-CPP-012

**Description:**
- Align each reference track individually to vinyl
- Handles cumulative drift by widening search window
- Returns per-track offset and confidence

**Acceptance Criteria:**
- Tracks align with < 0.1s accuracy
- Confidence scores reflect alignment quality
- Integration test with multi-track reference

---

### AAC-CPP-014: Reference Mode Pipeline
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** AAC-CPP-013, AAC-CPP-005

**Description:**
- Full reference mode implementation
- Load vinyl + reference
- Per-track alignment
- Boundary refinement
- Generate split points

**Acceptance Criteria:**
- Produces correct splits for test data
- Integration test matching Python output

---

### AAC-CPP-015: Noise Floor Estimation
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-008, AAC-CPP-009

**Description:**
- Estimate vinyl surface noise characteristics
- RMS level, spectral flatness of noise regions
- Sliding window for adaptive threshold

**Acceptance Criteria:**
- Correctly identifies noise floor in vinyl rip
- Unit tests

---

### AAC-CPP-016: Blind Mode Pipeline
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** AAC-CPP-015, AAC-CPP-011, AAC-CPP-005

**Description:**
- Full blind mode implementation
- Gap candidate detection via RMS threshold
- Multi-feature scoring for confidence
- Onset-based boundary refinement

**Acceptance Criteria:**
- Detects track boundaries in test vinyl rip
- Reasonable confidence scores

---

## Phase 5: CLI Interface

### AAC-CPP-017: Argument Parsing
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Small  
**Dependencies:** AAC-CPP-003

**Description:**
- Parse command-line arguments (reference/blind mode)
- Support all options from Python CLI
- Help text and version info

**Acceptance Criteria:**
- `--help` displays usage
- All options parsed correctly

---

### AAC-CPP-018: CLI Reference Mode Command
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-014, AAC-CPP-017

**Description:**
- `mwaac reference <vinyl> -r <ref> -o <out>`
- Progress reporting
- Report formatting (matches Python output style)

**Acceptance Criteria:**
- CLI produces same output as Python version
- Error messages are clear

---

### AAC-CPP-019: CLI Blind Mode Command
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-016, AAC-CPP-017

**Description:**
- `mwaac blind <vinyl> -o <out>`
- All blind mode options
- Report formatting

**Acceptance Criteria:**
- CLI produces expected output
- Options function correctly

---

## Phase 6: Interactive TUI

### AAC-CPP-020: TUI Application Shell
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-002

**Description:**
- Basic FTXUI application setup
- Full-screen terminal mode
- Event loop and keyboard handling
- Exit handling (Ctrl+C, Q)

**Acceptance Criteria:**
- Application launches full-screen
- Clean exit on quit command

---

### AAC-CPP-021: Waveform Rendering
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** AAC-CPP-006, AAC-CPP-020

**Description:**
- Render audio waveform using Unicode blocks
- Downsampling for display resolution
- Peak/RMS display modes
- Zoom in/out functionality

**Acceptance Criteria:**
- Waveform visually represents audio
- Zoom levels work correctly
- Renders at 60fps (or acceptable refresh rate)

---

### AAC-CPP-022: Chop Point Markers
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-021

**Description:**
- Display chop points as vertical markers on waveform
- Different colors for selected/unselected
- Marker labels (track numbers)

**Acceptance Criteria:**
- Markers visible and distinct
- Selected marker highlighted

---

### AAC-CPP-023: Keyboard Navigation
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-022

**Description:**
- Left/Right: Navigate between chop points
- Up/Down: Zoom in/out
- +/-: Fine-adjust selected chop point
- Space: Toggle preview mode
- Enter: Confirm and export
- H: Toggle help overlay
- Q/Esc: Exit

**Acceptance Criteria:**
- All keys function as specified
- Help overlay shows bindings

---

### AAC-CPP-024: Help Overlay
**Priority:** P3 (Low)  
**Status:** `[ ]`  
**Estimated Effort:** Small  
**Dependencies:** AAC-CPP-023

**Description:**
- Toggleable help panel showing keyboard shortcuts
- Non-obstructive overlay design

**Acceptance Criteria:**
- Help displays all commands
- Toggle works reliably

---

### AAC-CPP-025: Export from TUI
**Priority:** P2 (Medium)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-023, AAC-CPP-005

**Description:**
- Export tracks with current chop points
- Progress indicator during export
- Success/error feedback

**Acceptance Criteria:**
- Exports match CLI output
- Progress shown during write

---

## Phase 7: Quality Assurance

### AAC-CPP-026: Integration Test Suite
**Priority:** P1 (High)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** AAC-CPP-018, AAC-CPP-019

**Description:**
- End-to-end tests with real audio files
- Compare output to Python version
- Lossless verification suite

**Acceptance Criteria:**
- All integration tests pass
- Output matches Python within tolerance

---

### AAC-CPP-027: Performance Benchmarks
**Priority:** P3 (Low)  
**Status:** `[ ]`  
**Estimated Effort:** Medium  
**Dependencies:** AAC-CPP-014, AAC-CPP-016

**Description:**
- Benchmark correlation on various file sizes
- Memory usage profiling
- TUI frame rate measurement

**Acceptance Criteria:**
- Performance acceptable for 1-hour files
- No memory leaks

---

### AAC-CPP-028: Final QA Validation
**Priority:** P0 (Critical)  
**Status:** `[ ]`  
**Estimated Effort:** Large  
**Dependencies:** All

**Description:**
- Full functionality testing
- Edge case testing
- Cross-platform validation (if applicable)
- Create tickets for any issues found

**Acceptance Criteria:**
- All functionality verified
- No P0/P1 issues open

---

## Execution Order

**Parallel Tracks (after Phase 1):**

Track A (Core): 001 → 003 → 004 → 005
Track B (Dependencies): 001 → 002 → 006
Track C (Analysis): 002 → 007 → 008 → 009

**Then Sequential:**
- Phase 4 depends on Phase 2 + 3
- Phase 5 depends on Phase 4
- Phase 6 can start after Phase 2 basics
- Phase 7 is final

---

## Notes

- Each backlog item should be implemented as a separate PR
- PRs require agent review before merge
- Update this backlog as work progresses
- New issues discovered during QA get added as new items
