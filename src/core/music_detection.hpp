#pragma once
#include <span>
#include <cstdint>

namespace mwaac {

// Detect where music starts in audio
// Returns sample index where music begins
// Returns 0 if music starts immediately or detection fails
int64_t detect_music_start(
    std::span<const float> samples,
    int sample_rate,
    float min_music_seconds = 2.0f  // Min duration to confirm music
);

// Estimate noise floor RMS from audio
float estimate_noise_floor(
    std::span<const float> samples,
    int sample_rate,
    float window_seconds = 2.0f  // Window for searching quietest region
);

} // namespace mwaac