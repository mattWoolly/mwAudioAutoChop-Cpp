#pragma once
#include "core/split_point.hpp"
#include "core/analysis_result.hpp"
#include "core/audio_buffer.hpp"
#include "core/audio_file.hpp"
#include <filesystem>
#include <vector>
#include <span>
#include <cstdint>

namespace mwaac {

// ─── BlindModeConfig defaults (Mi-5-BLIND catalog) ─────────────────
//
// Decision thresholds for the blind-mode pipeline's user-configurable
// inputs. Each value encodes a policy choice that the CLI default
// inherits unless overridden at construction time. Per the Mi-5
// invariant "Every decision threshold is a `constexpr` at top of
// translation unit with a comment citing the observation or corpus
// that produced it" — extended to the .hpp / config-defaults case
// per Mi-5-BLIND.

// Minimum gap duration in seconds — gaps shorter than this are
// treated as intra-track pauses, not track boundaries. Empirical:
// 2 s discriminates inter-track silence from typical musical
// pauses (sustained chords, breath gaps) on the vinyl corpus.
inline constexpr float kBlindDefaultMinGapSeconds = 2.0f;

// Maximum gap duration in seconds — gaps longer than this are
// treated as lead-in / lead-out / side-flip artifacts, not track
// boundaries.
inline constexpr float kBlindDefaultMaxGapSeconds = 30.0f;

// Score_gap confidence gate. Per NEW-BLIND-GAP's docstring on
// score_gap, the formula `1 - gap_rms / signal_reference_rms`
// gates candidates above this threshold. 0.6 was the original
// hand-tuned value; preserved by NEW-BLIND-GAP after re-deriving
// the formula's denominator semantics.
inline constexpr float kBlindDefaultConfidenceThreshold = 0.6f;

// Analysis-rate sample rate (Hz). Most vinyl rips arrive at
// 44.1 kHz native; the analysis pipeline downsamples to this rate
// for envelope and correlation work. 44100 is the de-facto CD-rate
// audio analysis standard.
inline constexpr int kBlindDefaultAnalysisSampleRate = 44100;

struct BlindModeConfig {
    float min_gap_seconds{kBlindDefaultMinGapSeconds};
    float max_gap_seconds{kBlindDefaultMaxGapSeconds};
    float confidence_threshold{kBlindDefaultConfidenceThreshold};
    int analysis_sr{kBlindDefaultAnalysisSampleRate};
};

enum class BlindError {
    LoadFailed,
    AnalysisFailed
    // M-8: NoGapsFound was removed. A gap-free input is a legitimate
    // outcome (returns a single-split result spanning the full input),
    // not an error. See `analyze_blind_mode` body for the gap-empty
    // handling and INV-BLIND-SINGLE-TRACK in docs/invariants.md.
};

// Analyze vinyl without reference tracks
Expected<AnalysisResult, BlindError> analyze_blind_mode(
    const std::filesystem::path& vinyl_path,
    const BlindModeConfig& config = {}
);

// Detect gap candidates in audio
// Returns vector of (start_frame, end_frame) for each gap
std::vector<std::pair<size_t, size_t>> detect_gaps(
    std::span<const float> rms_values,
    float threshold,
    int hop_length,
    int sample_rate,
    float min_gap_seconds,
    float max_gap_seconds
);

// Score a gap candidate.
//
// Returns confidence in [0, 1] based on how quiet the gap region is
// relative to `signal_reference_rms` — a reference level representing
// the typical loudness of the surrounding music. The formula is
// `1 - gap_rms / signal_reference_rms` (clamped), so a gap that is
// much quieter than the reference scores near 1; a gap whose RMS
// equals the reference scores 0.
//
// NEW-BLIND-GAP: the parameter was previously named `noise_floor_rms`
// and `analyze_blind_mode` passed the noise-floor estimate (10th
// percentile of frame RMS). On a fixture where silence dominates the
// signal — e.g. a 2-track rip with a long gap, where the gap RMS
// itself becomes the 10th percentile — the noise floor estimate
// equals the gap RMS, the formula degenerates to `1 - 1 = 0`, and
// every detected gap is rejected by the `confidence >= 0.6` gate.
// The cure renames the parameter to reflect its true semantics
// (signal reference, not noise floor) and pushes the choice of
// reference-level estimator out to the caller, where context is
// available to compute a meaningful loud-reference value.
//
// Caller-side guidance: pick a reference level that approximates
// the music's typical loudness — e.g. a high percentile (p90) of
// frame RMS, or the mean RMS of frames above the detection threshold.
//
// M-7: previously took a `[[maybe_unused]] int sample_rate` parameter
// reserved for a future spectral-flatness scoring path. Spectral
// flatness is now C-5's scope; the parameter was carrying no signal
// and was removed per the cycle's "Public APIs do not carry dead
// parameters" invariant (INV-NO-DEAD-PARAMS).
float score_gap(
    std::span<const float> samples,
    size_t start_sample,
    size_t end_sample,
    float signal_reference_rms
);

} // namespace mwaac