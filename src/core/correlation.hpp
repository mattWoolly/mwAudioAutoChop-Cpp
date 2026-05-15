#pragma once

#include <span>
#include <vector>
#include <cstdint>
#include <cmath>

namespace mwaac {

struct CorrelationResult {
    int64_t lag{0};         // Sample offset (positive = first ahead of second)
    // peak_value range depends on the producing function:
    //   cross_correlate_fft / cross_correlate_fast — Pearson NCC per-lag, peak in [-1, 1].
    //   cross_correlate (naive) — globally normalized; peak magnitude not bounded
    //     to [-1, 1] in general. See cross_correlate's docstring for the caveat.
    double peak_value{0.0};
};

// Naive O(N*M) cross-correlation — testing-only verification shim for
// cross_correlate_fft. Production sites should use cross_correlate_fft
// or cross_correlate_fast instead; this function exists to cross-check
// the FFT implementation's lag selection (see "FFT correlation agrees
// with naive implementation" in tests/test_correlation.cpp).
//
// Returns the lag where signals align. Positive lag means 'reference'
// appears later in 'target' (target is ahead).
//
// NORMALIZATION CAVEAT: divides the centered cross product by a single
// GLOBAL norm factor (sqrt of total ref energy * total tgt energy) at
// every lag, NOT by per-lag slice energies. Unlike cross_correlate_fft
// (Pearson NCC per-lag, peak in [-1, 1]), this function's peak_value is
// not a per-lag correlation coefficient and is not guaranteed to lie in
// [-1, 1] for arbitrary inputs. Callers using peak_value as a confidence
// score are using the wrong function.
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
// [-1, 1]. Note: the naive cross_correlate above uses a different
// (global, not per-lag) normalization — lag selection is comparable
// across the two implementations; peak magnitudes are not.
CorrelationResult cross_correlate_fft(
    std::span<const float> reference,
    std::span<const float> target
);

} // namespace mwaac