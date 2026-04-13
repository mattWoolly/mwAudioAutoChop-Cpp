#include <catch2/catch_test_macros.hpp>
#include "modes/blind_mode.hpp"
#include <vector>

TEST_CASE("Gap detection finds gaps in RMS data", "[blind]") {
    // Simulate RMS: loud-quiet-loud pattern
    std::vector<float> rms(1000, 0.1f);  // Baseline
    
    // Add a gap (quiet region) from frame 300-500
    for (int i = 300; i < 500; ++i) {
        rms[i] = 0.01f;
    }
    
    auto gaps = mwaac::detect_gaps(rms, 0.05f, 512, 44100, 0.5f, 10.0f);
    
    REQUIRE(gaps.size() == 1);
    REQUIRE(gaps[0].first == 300);
    REQUIRE(gaps[0].second == 500);
}

TEST_CASE("Gap scoring based on energy", "[blind]") {
    // Create samples with a quiet region
    std::vector<float> samples(10000, 0.5f);  // Loud
    for (int i = 2000; i < 4000; ++i) {
        samples[i] = 0.01f;  // Quiet gap
    }
    
    // Score the quiet region
    float score = mwaac::score_gap(samples, 2000, 4000, 44100, 0.5f);
    
    REQUIRE(score > 0.9f);  // Should be high confidence (very quiet)
}

TEST_CASE("Blind mode API compiles", "[blind]") {
    // Verify API is usable
    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0f;
    config.max_gap_seconds = 30.0f;
    
    REQUIRE(config.min_gap_seconds == 2.0f);
}