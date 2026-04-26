#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/correlation.hpp"
#include <cmath>
#include <vector>

TEST_CASE("Cross-correlation finds correct lag for shifted signal", "[correlation]") {
    // Create a simple signal
    std::vector<float> original(1000);
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = std::sin(2.0f * static_cast<float>(M_PI) * 10.0f * static_cast<float>(i) / 1000.0f);
    }
    
    // Shift by 100 samples
    std::vector<float> shifted(1100);
    std::fill(shifted.begin(), shifted.begin() + 100, 0.0f);
    std::copy(original.begin(), original.end(), shifted.begin() + 100);
    
    auto result = mwaac::cross_correlate(original, shifted);
    
    // Lag should be 100 (shifted is ahead by 100)
    REQUIRE(result.lag == 100);
    REQUIRE(result.peak_value > 0.9);  // High correlation
}

TEST_CASE("RMS normalization produces unit energy", "[correlation]") {
    std::vector<float> samples(100, 2.0f);
    mwaac::normalize_rms(samples);

    float sum_sq = 0.0f;
    for (float s : samples) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / static_cast<float>(samples.size()));

    REQUIRE_THAT(static_cast<double>(rms), Catch::Matchers::WithinAbs(1.0, 0.01));
}

TEST_CASE("FFT cross-correlation finds correct lag for shifted signal", "[correlation][fft]") {
    // Reference: 1000-sample sinusoid with some DC added (tests mean-centering)
    std::vector<float> reference(1000);
    for (size_t i = 0; i < reference.size(); ++i) {
        reference[i] = 0.1f + std::sin(2.0f * static_cast<float>(M_PI) * 7.0f * static_cast<float>(i) / 1000.0f);
    }

    // Target: the reference placed at a known offset inside a longer buffer
    const int64_t true_lag = 347;
    std::vector<float> target(2500, 0.0f);
    for (size_t i = 0; i < reference.size(); ++i) {
        target[static_cast<std::size_t>(true_lag) + i] = reference[i];
    }

    auto result = mwaac::cross_correlate_fft(reference, target);
    REQUIRE(result.lag == true_lag);
    REQUIRE(result.peak_value > 0.99);  // should be ~1.0 for an exact match
}

TEST_CASE("FFT cross-correlation with noise still locks on target", "[correlation][fft]") {
    // Reference: deterministic "distinctive" signal
    std::vector<float> reference(2000);
    for (size_t i = 0; i < reference.size(); ++i) {
        reference[i] = std::sin(2.0f * static_cast<float>(M_PI) * 13.0f * static_cast<float>(i) / 200.0f)
                     + 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 3.0f * static_cast<float>(i) / 200.0f);
    }

    // Target: reference placed at a known lag, plus low-level noise
    const int64_t true_lag = 5000;
    std::vector<float> target(10000, 0.0f);
    // Fill with small pseudo-random noise
    uint32_t seed = 42;
    for (size_t i = 0; i < target.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        float r = (static_cast<float>(static_cast<int32_t>(seed)) / 2.0e9f);  // roughly -1..1
        target[i] = 0.05f * r;
    }
    for (size_t i = 0; i < reference.size(); ++i) {
        target[static_cast<std::size_t>(true_lag) + i] += reference[i];
    }

    auto result = mwaac::cross_correlate_fft(reference, target);
    REQUIRE(result.lag == true_lag);
    REQUIRE(result.peak_value > 0.8);
}

TEST_CASE("FFT correlation agrees with naive implementation", "[correlation][fft]") {
    // Build a small case where we can run both implementations
    std::vector<float> reference(500);
    for (size_t i = 0; i < reference.size(); ++i) {
        reference[i] = std::sin(2.0f * static_cast<float>(M_PI) * 5.0f * static_cast<float>(i) / 500.0f)
                     + 0.25f * std::cos(2.0f * static_cast<float>(M_PI) * 17.0f * static_cast<float>(i) / 500.0f);
    }

    const int64_t true_lag = 220;
    std::vector<float> target(1500, 0.0f);
    for (size_t i = 0; i < reference.size(); ++i) {
        target[static_cast<std::size_t>(true_lag) + i] = reference[i];
    }

    auto fft_result = mwaac::cross_correlate_fft(reference, target);
    // Naive implementation for cross-check
    auto naive_result = mwaac::cross_correlate(reference, target);

    REQUIRE(fft_result.lag == true_lag);
    REQUIRE(naive_result.lag == true_lag);
    // Both implementations should land on the same peak; peak values can
    // differ slightly due to different normalization conventions (the naive
    // version uses a global norm factor), but both should be near 1.0.
    REQUIRE(fft_result.peak_value > 0.99);
    REQUIRE(naive_result.peak_value > 0.5);
}