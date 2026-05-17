#include <catch2/catch_test_macros.hpp>
#include "modes/blind_mode.hpp"
#include <sndfile.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

TEST_CASE("Gap detection finds gaps in RMS data", "[blind]") {
    // Simulate RMS: loud-quiet-loud pattern
    std::vector<float> rms(1000, 0.1f);  // Baseline
    
    // Add a gap (quiet region) from frame 300-500
    for (int i = 300; i < 500; ++i) {
        rms[static_cast<std::size_t>(i)] = 0.01f;
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
        samples[static_cast<std::size_t>(i)] = 0.01f;  // Quiet gap
    }
    
    // Score the quiet region
    float score = mwaac::score_gap(samples, 2000, 4000, 0.5f);
    
    REQUIRE(score > 0.9f);  // Should be high confidence (very quiet)
}

TEST_CASE("Blind mode API compiles", "[blind]") {
    // Verify API is usable
    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0f;
    config.max_gap_seconds = 30.0f;

    REQUIRE(config.min_gap_seconds == 2.0f);
}

namespace {

// Minimal mono PCM_FLOAT WAV writer for the M-8 test. Reuses
// libsndfile so the produced file is loadable by load_audio_mono
// (which is what analyze_blind_mode calls internally).
bool write_temp_wav(const std::filesystem::path& path,
                    const std::vector<float>& samples,
                    int sample_rate) {
    SF_INFO info = {};
    info.samplerate = sample_rate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* sf = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (!sf) return false;
    sf_count_t written = sf_write_float(sf, samples.data(),
                                        static_cast<sf_count_t>(samples.size()));
    sf_close(sf);
    return written == static_cast<sf_count_t>(samples.size());
}

} // namespace

// M-8: a gap-free input is a legitimate outcome (no inter-track silences),
// not an error. Pre-cure analyze_blind_mode returned BlindError::NoGapsFound
// on an input with no detected gaps; the CLI surfaced this as an error
// exit. Post-cure: the function returns a single-split result spanning the
// entire input with confidence 1.0, and the NoGapsFound enum value is
// removed (callers no longer need to special-case it). See
// INV-BLIND-SINGLE-TRACK.
TEST_CASE("analyze_blind_mode: single-track (gap-free) input returns 1 split",
          "[blind][m-8]")
{
    namespace fs = std::filesystem;

    // Build a steady 440 Hz tone for 5 seconds at 22050 Hz — no inter-track
    // silence, no detectable gap by the noise-floor threshold.
    const int sr = 22050;
    const int duration_samples = sr * 5;
    std::vector<float> samples(static_cast<std::size_t>(duration_samples));
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < duration_samples; ++i) {
        samples[static_cast<std::size_t>(i)] =
            static_cast<float>(0.7 * std::sin(
                2.0 * pi * 440.0 * static_cast<double>(i)
                / static_cast<double>(sr)));
    }

    fs::path tmp_dir = fs::temp_directory_path() / "mwaac_blind_m8";
    fs::create_directories(tmp_dir);
    fs::path tmp_path = tmp_dir / "no_gaps.wav";
    REQUIRE(write_temp_wav(tmp_path, samples, sr));

    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0f;
    config.max_gap_seconds = 5.0f;
    config.analysis_sr = sr;

    auto result = mwaac::analyze_blind_mode(tmp_path, config);

    // Pre-cure: result.has_value() == false; result.error() ==
    // BlindError::NoGapsFound. Post-cure: result.has_value() == true with a
    // single split spanning the input.
    REQUIRE(result.has_value());
    const auto& analysis = result.value();
    CHECK(analysis.mode == "blind");
    REQUIRE(analysis.split_points.size() == 1);
    CHECK(analysis.split_points[0].start_sample == 0);
    CHECK(analysis.split_points[0].end_sample ==
          static_cast<int64_t>(samples.size()) - 1);
    CHECK(analysis.split_points[0].source == "blind");
    CHECK(analysis.split_points[0].confidence == 1.0);

    std::error_code ec;
    fs::remove(tmp_path, ec);
    fs::remove(tmp_dir, ec);
}