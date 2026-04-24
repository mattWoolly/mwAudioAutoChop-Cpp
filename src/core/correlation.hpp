#pragma once

#include <span>
#include <vector>
#include <cstdint>
#include <cmath>

namespace mwaac {

struct CorrelationResult {
    int64_t lag{0};         // Sample offset (positive = first ahead of second)
    double peak_value{0.0}; // Normalized correlation at peak (0-1)
};

// FFT-based cross-correlation
// Returns lag where signals align and normalized peak correlation value
// Positive lag means 'reference' appears later in 'target' (target is ahead)
CorrelationResult cross_correlate(
    std::span<const float> reference,
    std::span<const float> target
);

// Preprocessing for better correlation
// - High-pass filter at 80 Hz (removes rumble)
// - RMS normalization
std::vector<float> preprocess_for_correlation(
    std::span<const float> samples,
    int sample_rate
);

// Simple high-pass filter using IIR
void apply_highpass(std::vector<float>& samples, int sample_rate, float cutoff_hz);

// RMS normalization
void normalize_rms(std::vector<float>& samples);

// Downsample audio by factor (averaging)
std::vector<float> downsample(std::span<const float> samples, int factor);

// Fast correlation using downsampling for coarse search, then refining
// Much faster for large audio files
CorrelationResult cross_correlate_fast(
    std::span<const float> reference,
    std::span<const float> target,
    int downsample_factor = 100  // Default 100x reduction
);

// FFT-based normalized cross-correlation. Returns the best lag (sample
// offset in target where reference aligns) and its normalized peak value.
// Searches only valid lags [0, target.size - reference.size] — the
// reference must fully fit inside the target slice.
//
// Vastly faster than the naive implementation for long signals, so the
// caller can use a MUCH wider target window (e.g. +-10 s instead of
// +-1.5 s) without runtime penalty. This is the key to reliable
// alignment on tracks where coarse correlation misses the true peak.
//
// Correlation is zero-mean on both sides and normalized per-lag by each
// slice's own energy (standard Pearson NCC), so the peak value is in
// [-1, 1] and directly comparable to the naive version.
CorrelationResult cross_correlate_fft(
    std::span<const float> reference,
    std::span<const float> target
);

} // namespace mwaac