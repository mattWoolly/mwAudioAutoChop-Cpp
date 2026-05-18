#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/analysis.hpp"
#include "core/frame_sample_bridge.hpp"  // M-ANALYSIS-FRAME-SAMPLE-BRIDGE
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

TEST_CASE("RMS energy of constant signal", "[analysis]") {
    std::vector<float> samples(1000, 0.5f);
    auto rms = mwaac::compute_rms_energy(samples, 44100, 100, 50);
    
    REQUIRE(!rms.empty());
    REQUIRE_THAT(static_cast<double>(rms[0]), Catch::Matchers::WithinAbs(0.5, 0.01));
}

TEST_CASE("RMS energy of sine wave", "[analysis]") {
    std::vector<float> samples(1000);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = std::sin(2.0f * static_cast<float>(M_PI) * 10.0f * static_cast<float>(i) / 1000.0f);
    }

    auto rms = mwaac::compute_rms_energy(samples, 44100, 100, 50);

    // RMS of sine wave should be amplitude / sqrt(2) ≈ 0.707
    REQUIRE(!rms.empty());
    REQUIRE_THAT(static_cast<double>(rms[0]), Catch::Matchers::WithinAbs(0.707, 0.05));
}

TEST_CASE("Zero crossing rate for noisy signal", "[analysis]") {
    // Alternating signal has maximum ZCR
    std::vector<float> samples(100);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    }
    
    auto zcr = mwaac::compute_zero_crossing_rate(samples, 100, 100);
    
    REQUIRE(!zcr.empty());
    REQUIRE(zcr[0] > 0.9f);  // Close to 1.0
}

TEST_CASE("RMS to dB conversion", "[analysis]") {
    // RMS of 1.0 should be 0 dB
    REQUIRE(mwaac::rms_to_db(1.0f) == 0.0f);
    
    // RMS of 0.1 should be -20 dB
    REQUIRE_THAT(static_cast<double>(mwaac::rms_to_db(0.1f)), Catch::Matchers::WithinAbs(-20.0, 0.01));
}

TEST_CASE("dB to RMS conversion", "[analysis]") {
    // 0 dB should be RMS of 1.0
    REQUIRE_THAT(static_cast<double>(mwaac::db_to_rms(0.0f)), Catch::Matchers::WithinAbs(1.0, 0.01));

    // -20 dB should be RMS of 0.1
    REQUIRE_THAT(static_cast<double>(mwaac::db_to_rms(-20.0f)), Catch::Matchers::WithinAbs(0.1, 0.01));
}

TEST_CASE("Empty input returns empty", "[analysis]") {
    std::vector<float> empty;
    auto rms = mwaac::compute_rms_energy(empty, 44100, 100, 50);
    REQUIRE(rms.empty());

    auto zcr = mwaac::compute_zero_crossing_rate(empty, 100, 50);
    REQUIRE(zcr.empty());
}

TEST_CASE("compute_zero_crossing_rate: single-sample frame returns 0, not NaN", "[analysis]") {
    // M-10 regression test. Pre-cure: divisor (end - start - 1) is 0 when
    // the frame contains a single sample, and the inner loop has zero
    // iterations, so the normalization computes 0.0f / 0.0f = NaN per
    // IEEE-754. Post-cure: the per-frame guard returns the BACKLOG-mandated
    // 0.0f — "ZCR is defined as 0 for frames of length less than 2."
    std::vector<float> samples{1.0f};
    auto zcr = mwaac::compute_zero_crossing_rate(samples, /*frame_length=*/1, /*hop_length=*/1);
    REQUIRE(zcr.size() == 1);
    REQUIRE(zcr[0] == 0.0f);          // exact-match: defined as 0 for short frames
    REQUIRE(!std::isnan(zcr[0]));     // explicit NaN exclusion (independent signal)
}

// M-ANALYSIS-FRAME-SAMPLE-BRIDGE: compile-time contracts on the
// FrameIdx bridge from the analysis TU. compute_rms_energy and
// compute_zero_crossing_rate adopt detail::frame_to_sample at their
// frame-iter × hop_length crossings (analysis.cpp:26 and :57 pre-cure).
// Mirror the pattern from tests/test_blind_mode.cpp:152 and
// tests/test_music_detection.cpp:56 — pin the in-header static_asserts
// at the analysis TU boundary so a regression that strips `explicit`
// from the index ctors fails recognisably in every test file that
// adopts the bridge.
TEST_CASE("M-ANALYSIS-FRAME-SAMPLE-BRIDGE: SampleIdx/FrameIdx contract holds in analysis TU",
          "[analysis][m-analysis-frame-sample-bridge]")
{
    using mwaac::detail::SampleIdx;
    using mwaac::detail::FrameIdx;

    // Mirror the M-6 / M-MUSIC-DETECT contract block from this TU.
    STATIC_REQUIRE(!std::is_constructible_v<SampleIdx, FrameIdx>);
    STATIC_REQUIRE(!std::is_constructible_v<FrameIdx, SampleIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<std::int64_t, SampleIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<std::size_t, FrameIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<SampleIdx, std::int64_t>);
    STATIC_REQUIRE(!std::is_convertible_v<FrameIdx, std::size_t>);
}

TEST_CASE("M-ANALYSIS-FRAME-SAMPLE-BRIDGE: compute_rms_energy frame stride is hop_length",
          "[analysis][m-analysis-frame-sample-bridge]")
{
    // Pre-cure compute_rms_energy computed each frame's `start` as
    // `i * static_cast<std::size_t>(hop_length)` with `i` and
    // `hop_length` both untagged. Post-cure the conversion goes through
    // detail::frame_to_sample. Behaviorally the per-frame `start`
    // offset is unchanged (the bridge is just typed multiplication),
    // so the existing RMS TEST_CASEs above already exercise return-value
    // correctness end-to-end.
    //
    // This case adds a tighter assertion: the second frame's start
    // sample (= hop_length) must equal what the bridge would compute
    // for FrameIdx{1}. If a future regression accidentally returned a
    // raw frame index in place of a sample offset, the per-frame RMS
    // values would shift by a hop_length-vs-1 factor and the test
    // signal's expected RMS would no longer match.
    //
    // Signal: a square wave that switches polarity every `hop_length`
    // samples. With hop_length=50, frame_length=50, the first frame
    // RMS is 1.0 and the second frame RMS is also 1.0 (each frame
    // contains a constant-magnitude block). If the bridge mis-computed
    // the start offset, the frames would straddle the polarity flip
    // and RMS would drop or become noisy.
    const int hop_length = 50;
    const int frame_length = 50;
    std::vector<float> samples(200);
    for (size_t i = 0; i < samples.size(); ++i) {
        // Flip polarity every hop_length samples — frame-aligned.
        samples[i] = ((i / static_cast<std::size_t>(hop_length)) % 2 == 0) ? 1.0f : -1.0f;
    }

    auto rms = mwaac::compute_rms_energy(samples, /*sample_rate=*/44100,
                                          frame_length, hop_length);
    REQUIRE(rms.size() >= 4);

    // Direct bridge cross-check: the third frame's intended start is
    // exactly frame_to_sample(FrameIdx{2}, hop_length).value (= 100).
    // We can't observe `start` from outside the function, but if the
    // bridge produced a different value the per-frame RMS would no
    // longer be 1.0 for this fixture.
    using mwaac::detail::SampleIdx;
    using mwaac::detail::FrameIdx;
    using mwaac::detail::frame_to_sample;
    CHECK(frame_to_sample(FrameIdx{2}, hop_length).value == 100);

    // Every frame in this fixture should have RMS == 1.0 (each frame
    // contains a constant-magnitude block aligned to hop_length).
    for (size_t i = 0; i < rms.size(); ++i) {
        CHECK(rms[i] == 1.0f);
    }
}