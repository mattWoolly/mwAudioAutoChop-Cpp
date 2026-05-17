#include <catch2/catch_test_macros.hpp>
#include "core/music_detection.hpp"
#include "core/frame_sample_bridge.hpp"  // M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE
#include <vector>
#include <cmath>
#include <cstdint>
#include <type_traits>

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

// M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE: detect_music_start adopts the typed
// bridge from core/frame_sample_bridge.hpp at the frame-iter × hop_length
// conversion (music_detection.cpp:75 pre-cure). This TEST_CASE pins the
// bridge contract from outside the music_detection TU — mirrors the
// pattern test_blind_mode.cpp:152 uses for the same bridge applied at the
// blind_mode site, so a regression that strips `explicit` from the index
// ctors fails recognisably in both test files.
TEST_CASE("M-MUSIC-DETECT: SampleIdx/FrameIdx contract holds in core/",
          "[music][m-music-detect]")
{
    using mwaac::detail::SampleIdx;
    using mwaac::detail::FrameIdx;

    // Same contract assertions as test_blind_mode.cpp's M-6 TEST_CASE;
    // mirror in this TU so a future regression that breaks the bridge
    // fires in both test files (and at any future TU that includes the
    // header).
    STATIC_REQUIRE(!std::is_constructible_v<SampleIdx, FrameIdx>);
    STATIC_REQUIRE(!std::is_constructible_v<FrameIdx, SampleIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<std::int64_t, SampleIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<std::size_t, FrameIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<SampleIdx, std::int64_t>);
    STATIC_REQUIRE(!std::is_convertible_v<FrameIdx, std::size_t>);
}

TEST_CASE("M-MUSIC-DETECT: detect_music_start returns a sample-index, not a frame-index",
          "[music][m-music-detect]")
{
    // Pre-cure detect_music_start computed the return value as
    // `static_cast<int64_t>(i) * hop_length` with `i` and `hop_length`
    // both untagged. Post-cure the conversion goes through
    // detail::frame_to_sample. Behaviorally the return value is the
    // same (the bridge is just typed multiplication), so the existing
    // "Music start detection finds loud region" TEST_CASE above already
    // exercises the return-value correctness end-to-end.
    //
    // This case adds a tighter assertion: the returned int64_t value
    // must be divisible by hop_length (= 50 ms × sample_rate / 4 × 1
    // sample). If a future regression accidentally returned the frame
    // index `i` directly (without multiplying by hop_length), the
    // returned value would not in general be a multiple of hop_length.
    int sr = 44100;
    std::vector<float> samples(static_cast<std::size_t>(sr * 3));
    for (int i = 0; i < sr; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.001f;  // quiet pre-roll
    }
    for (int i = sr; i < sr * 3; ++i) {
        samples[static_cast<std::size_t>(i)] = 0.5f * std::sin(
            2.0f * static_cast<float>(M_PI) * 440.0f *
            static_cast<float>(i) / static_cast<float>(sr));
    }

    auto start = mwaac::detect_music_start(samples, sr, 1.0f);

    // hop_length = (0.05 * sr) / 4 = 0.0125 * sr at sr=44100 → 551.
    // Any multiple of 551 is a valid sample-index from the bridge.
    constexpr int64_t expected_hop_at_44100 = 551;
    REQUIRE(start % expected_hop_at_44100 == 0);
    // Smoke-check the magnitude is in the music region (1 s ± tolerance,
    // mirroring the existing "Music start detection finds loud region"
    // TEST_CASE assertions).
    REQUIRE(start > 0);
    REQUIRE(start < static_cast<int64_t>(sr * 2));
}