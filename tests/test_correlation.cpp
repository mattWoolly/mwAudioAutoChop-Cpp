#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/correlation.hpp"
#include <cmath>
#include <vector>

TEST_CASE("Cross-correlation finds correct lag for shifted signal", "[correlation]") {
    // Create a simple signal
    std::vector<float> original(1000);
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = std::sin(2.0f * M_PI * 10.0f * i / 1000.0f);
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
    float rms = std::sqrt(sum_sq / samples.size());
    
    REQUIRE_THAT(rms, Catch::Matchers::WithinAbs(1.0, 0.01));
}