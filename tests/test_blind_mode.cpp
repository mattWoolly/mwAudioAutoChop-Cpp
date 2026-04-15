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
    // Create samples with a quiet region that has sharp edges
    // Using a larger sample set to give meaningful duration
    std::vector<float> samples(441000, 0.5f);  // 10 seconds at 44100 Hz
    
    // Add a 5-second quiet gap (ideal duration) from 2-7 seconds
    // This should score well on all metrics: energy, duration, and edge sharpness
    for (int i = 88200; i < 308700; ++i) {  // 2s to 7s
        samples[i] = 0.001f;  // Very quiet gap
    }
    
    // Score the quiet region
    float score = mwaac::score_gap(samples, 88200, 308700, 44100, 0.5f);
    
    // With multi-feature scoring (energy, duration, edge sharpness):
    // - Energy score: very high (0.001 / 0.5 = 0.002, so ~0.998)
    // - Duration score: 5s gap = ideal, so ~1.0
    // - Edge sharpness: 0.5 vs 0.001 = very sharp, so ~0.998
    // Combined: 0.5*0.998 + 0.3*1.0 + 0.2*0.998 = ~0.899
    REQUIRE(score > 0.5f);  // Should be reasonably high confidence
    REQUIRE(score <= 1.0f); // Should be normalized
}

TEST_CASE("Blind mode API compiles", "[blind]") {
    // Verify API is usable
    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0f;
    config.max_gap_seconds = 30.0f;
    
    REQUIRE(config.min_gap_seconds == 2.0f);
}