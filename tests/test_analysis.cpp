#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/analysis.hpp"
#include <cmath>
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