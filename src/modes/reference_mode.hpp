#pragma once

#include "core/split_point.hpp"
#include "core/analysis_result.hpp"
#include "core/audio_buffer.hpp"
#include "core/audio_file.hpp"
#include <filesystem>
#include <vector>
#include <cstdint>

namespace mwaac {

struct ReferenceTrack {
    std::filesystem::path path;
    AudioBuffer audio;
    int64_t duration_samples{0};
};

enum class ReferenceError {
    VinylLoadFailed,
    ReferenceLoadFailed,
    AlignmentFailed,
    NoTracksFound
};

// Analyze vinyl using reference tracks
// vinyl_path: path to vinyl rip
// reference_path: directory with per-track reference files OR single concatenated file
// analysis_sr: sample rate for analysis (default 22050 Hz)
Expected<AnalysisResult, ReferenceError> analyze_reference_mode(
    const std::filesystem::path& vinyl_path,
    const std::filesystem::path& reference_path,
    int analysis_sr = 22050
);

// Load reference tracks from directory (natural sorted)
Expected<std::vector<ReferenceTrack>, ReferenceError> load_reference_tracks(
    const std::filesystem::path& reference_dir,
    int sample_rate
);

// Align each reference track to vinyl, returning per-track offsets
// Returns vector of (vinyl_start_sample, correlation_confidence)
std::vector<std::pair<int64_t, double>> align_per_track(
    const AudioBuffer& vinyl,
    const std::vector<ReferenceTrack>& tracks,
    int64_t music_start_sample = 0
);

} // namespace mwaac