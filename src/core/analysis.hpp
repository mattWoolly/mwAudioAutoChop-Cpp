#pragma once
#include <span>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>

namespace mwaac {

// Frame-based RMS energy computation
// Returns RMS values for each frame
// frame_length: samples per frame (e.g., 2048)
// hop_length: samples between frame starts (e.g., 512)
std::vector<float> compute_rms_energy(
    std::span<const float> samples,
    int sample_rate,  // For reference, not used in basic RMS
    int frame_length = 2048,
    int hop_length = 512
);

// Convert RMS value to decibels
inline float rms_to_db(float rms) {
    return (rms > 0.0f) ? 20.0f * std::log10(rms) : -std::numeric_limits<float>::infinity();
}

// Convert dB to RMS
inline float db_to_rms(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Compute spectral flatness for each frame
// Returns values in [0, 1] where 1 = noise-like, 0 = tonal
std::vector<float> compute_spectral_flatness(
    std::span<const float> samples,
    int sample_rate,
    int frame_length = 2048,
    int hop_length = 512
);

// Zero-crossing rate per frame
std::vector<float> compute_zero_crossing_rate(
    std::span<const float> samples,
    int frame_length = 2048,
    int hop_length = 512
);

} // namespace mwaac