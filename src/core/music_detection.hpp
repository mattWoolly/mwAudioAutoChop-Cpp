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

// Compute optimal threshold using Otsu's method
// This finds the threshold that best separates "silence" from "music"
// Returns the optimal RMS threshold value
float compute_otsu_threshold(
    std::span<const float> rms_values
);

// Compute threshold as a sensitivity value (0.0 to 1.0)
// Returns sensitivity that would produce the Otsu threshold
float compute_auto_sensitivity(
    std::span<const float> rms_values,
    float noise_floor
);

} // namespace mwaac