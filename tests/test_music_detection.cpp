#include <catch2/catch_test_macros.hpp>
#include "core/music_detection.hpp"
#include <vector>
#include <cmath>

TEST_CASE("Music start detection finds loud region", "[music]") {
    // Create signal: 1 second of quiet, then 2 seconds of loud
    int sr = 44100;
    std::vector<float> samples(static_cast<std::size_t>(sr * 3));

    // First second: quiet (noise floor level)
    for (int i = 0; i < sr; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.001f * std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f * static_cast<float>(i) / static_cast<float>(sr));
    }

    // Next 2 seconds: loud music
    for (int i = sr; i < sr * 3; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / static_cast<float>(sr));
    }

    auto start = mwaac::detect_music_start(samples, sr, 1.0f);

    // Should detect music starting around 1 second (44100 samples)
    REQUIRE(start > static_cast<int64_t>(static_cast<double>(sr) * 0.9));   // After most of quiet section
    REQUIRE(start < static_cast<int64_t>(static_cast<double>(sr) * 1.2));   // Before too far into music
}

TEST_CASE("Noise floor estimation finds quiet region", "[music]") {
    int sr = 44100;
    std::vector<float> samples(static_cast<std::size_t>(sr * 2));

    // Half loud, half quiet
    for (int i = 0; i < sr; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.5f;
    }
    for (int i = sr; i < sr * 2; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.01f;
    }

    float noise = mwaac::estimate_noise_floor(samples, sr);

    // Should find the quiet region
    REQUIRE(noise < 0.1f);
}