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

struct BlindModeConfig {
    float min_gap_seconds{2.0f};   // Minimum gap to detect as boundary
    float max_gap_seconds{30.0f};  // Maximum gap (longer = lead-in/out)
    float confidence_threshold{0.6f};
    int analysis_sr{44100};
};

enum class BlindError {
    LoadFailed,
    AnalysisFailed,
    NoGapsFound
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

// Score a gap candidate
// Returns confidence 0-1 based on energy, flatness, etc.
float score_gap(
    std::span<const float> samples,
    size_t start_sample,
    size_t end_sample,
    int sample_rate,
    float noise_floor_rms
);

} // namespace mwaac