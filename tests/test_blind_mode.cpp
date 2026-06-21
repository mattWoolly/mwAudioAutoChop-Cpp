#include <catch2/catch_test_macros.hpp>
#include "modes/blind_mode.hpp"
#include "core/frame_sample_bridge.hpp"  // M-6 / M-MUSIC-DETECT — typed-index bridge (hoisted to core/)
#include "core/music_detection.hpp"      // estimate_noise_floor — digital-zero precondition guard
#include "modes/reference_mode.hpp"      // analysis_to_native_sample — INV-BLIND-NATIVE-COORDS
#include <sndfile.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <type_traits>
#include <variant>
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
//
// FIXTURE CHOICE — exercises the gaps.empty() cure path specifically:
// a 1-second tone at 22050 Hz produces 77 RMS frames (50 ms frame, 12.5 ms
// hop). On a constant-amplitude tone the noise-floor estimator (p10 of
// frame RMS) degenerates to the tone's own RMS — the gap-detection
// threshold sits at 2x noise_floor and every frame is registered as
// "below threshold" — so detect_gaps would otherwise produce one giant
// "gap" spanning the whole signal. But the gap length (77 frames ≈ 1.0 s)
// is below min_gap_seconds (2.0 s, = 160 frames at this hop), so
// detect_gaps DROPS the candidate and returns an empty vector. That is
// the only path that hits the M-8 cure (`gaps.empty()` no-error branch);
// any longer tone would still produce a candidate gap that the
// confidence-rejection path swallows, masking whether the cure itself
// works. Without this care, the test would pass equally well with the
// M-8 fix reverted (audit-1 finding 1, 2026-05-16).
TEST_CASE("analyze_blind_mode: single-track (gap-free) input returns 1 split",
          "[blind][m-8]")
{
    namespace fs = std::filesystem;

    // 1-second tone. See FIXTURE CHOICE comment above for why 1 s
    // specifically (must be < min_gap_seconds to force the
    // gaps.empty() cure path).
    const int sr = 22050;
    const int duration_samples = sr * 1;
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
    config.min_gap_seconds = 2.0f;  // > 1 s tone duration — forces detect_gaps to drop the candidate.
    config.max_gap_seconds = 5.0f;
    config.analysis_sr = sr;

    auto result = mwaac::analyze_blind_mode(tmp_path, config);

    // Pre-cure: result.has_value() == false; result.error() ==
    // BlindError::NoGapsFound. Post-cure: result.has_value() == true with
    // a single split spanning the input.
    REQUIRE(result.has_value());
    const auto& analysis = result.value();
    CHECK(analysis.mode == "blind");
    REQUIRE(analysis.split_points.size() == 1);
    CHECK(analysis.split_points[0].start_sample == 0);
    CHECK(analysis.split_points[0].end_sample ==
          static_cast<int64_t>(samples.size()) - 1);
    CHECK(analysis.split_points[0].source == "blind");
    CHECK(analysis.split_points[0].confidence == 1.0);

    // Second-axis regression guard: metadata records 0 gaps for the
    // gap-empty path. If a future change re-introduces the score-
    // rejection branch and the test fixture happens to fall into it,
    // num_gaps_found would be 1 (not 0) and this CHECK would fail —
    // catching the regression that audit-1 caught on the 5 s variant.
    auto num_gaps_it = analysis.metadata.find("num_gaps_found");
    REQUIRE(num_gaps_it != analysis.metadata.end());
    REQUIRE(std::holds_alternative<double>(num_gaps_it->second));
    CHECK(std::get<double>(num_gaps_it->second) == 0.0);

    std::error_code ec;
    fs::remove(tmp_path, ec);
    fs::remove(tmp_dir, ec);
}

namespace {

// Append `dur_seconds` of a `freq` Hz sine at amplitude `amp`.
void append_tone(std::vector<float>& out, double freq, double dur_seconds,
                 int sr, double amp) {
    const auto n = static_cast<std::size_t>(dur_seconds * sr);
    const double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<float>(
            amp * std::sin(2.0 * pi * freq * static_cast<double>(i)
                           / static_cast<double>(sr))));
    }
}

// Append `dur_seconds` of EXACT digital-zero silence (0.0f samples).
void append_silence(std::vector<float>& out, double dur_seconds, int sr) {
    out.insert(out.end(), static_cast<std::size_t>(dur_seconds * sr), 0.0F);
}

} // namespace

// INV-BLIND-ABSOLUTE-SILENCE-FLOOR — the headline regression. A vinyl rip
// from a processed/restored master (e.g. iZotope RX) has TRUE digital-zero
// inter-track silence. When >=10% of frames are exactly 0.0,
// estimate_noise_floor (p10 of frame RMS) returns exactly 0, the relative
// gap threshold (noise_floor*2) collapses to 0, and PRE-CURE detect_gaps'
// strict `<` rejected every 0.0 frame -> 0 gaps -> a single split spanning
// the whole input at confidence 1.0 (a confident wrong answer). The absolute
// silence floor (kBlindGapSilenceFloorLinear, -60 dBFS) combined via max()
// plus the non-strict `<=` make the digital-zero gap detectable.
//
// Fixture: 3 s 440 Hz tone | 3 s EXACT 0.0 | 3 s 550 Hz tone — silence is
// 33% of frames, so the p10 noise-floor estimate is exactly 0 by
// construction (asserted as a precondition). Pre-cure this returns 1 split
// and FAILS the >=2 assert; post-cure it returns >=2. sr = 44100 so
// native == analysis (no coordinate confound; that is covered separately).
TEST_CASE("analyze_blind_mode: digital-zero inter-track gap yields >=2 tracks (p10==0)",
          "[blind][zero-floor]")
{
    namespace fs = std::filesystem;
    const int sr = 44100;
    std::vector<float> samples;
    append_tone(samples, 440.0, 3.0, sr, 0.7);
    append_silence(samples, 3.0, sr);
    append_tone(samples, 550.0, 3.0, sr, 0.7);

    // Precondition that makes this a genuine digital-zero regression: the
    // all-frames noise-floor estimate is exactly 0 on this fixture. (If a
    // future fixture edit lifts this above 0 the test would silently stop
    // exercising the zero-floor path.)
    REQUIRE(mwaac::estimate_noise_floor(samples, sr) == 0.0F);

    fs::path tmp_dir = fs::temp_directory_path() / "mwaac_blind_zero_floor";
    fs::create_directories(tmp_dir);
    fs::path tmp_path = tmp_dir / "digital_zero_gap.wav";
    REQUIRE(write_temp_wav(tmp_path, samples, sr));

    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0F;
    config.max_gap_seconds = 5.0F;
    config.analysis_sr = sr;

    auto result = mwaac::analyze_blind_mode(tmp_path, config);
    REQUIRE(result.has_value());
    const auto& analysis = result.value();

    // Core regression assertion: the digital-zero gap IS detected.
    REQUIRE(analysis.split_points.size() >= 2);
    CHECK(analysis.split_points[0].start_sample == 0);

    // Second track starts after the 3 s gap (~6 s in), within one envelope
    // frame (tolerance 100 ms = sr/10; the music-onset frame straddles the
    // silence->tone boundary so exact-sample equality is not expected).
    const int64_t expected_start = static_cast<int64_t>(sr) * 6;
    const int64_t actual_start = analysis.split_points[1].start_sample;
    INFO("expected ~" << expected_start << " actual " << actual_start);
    CHECK(std::abs(actual_start - expected_start) < sr / 10);
    // A digital-zero gap has gap_rms == 0, so score_gap returns ~1.0 — assert
    // near-perfect, which is STRONGER than the 0.6 gate the split already
    // passed (so it is not a tautology: a scoring regression to e.g. 0.7
    // would still create the split but fail this).
    CHECK(analysis.split_points[1].confidence > 0.99);

    std::error_code ec;
    fs::remove(tmp_path, ec);
    fs::remove(tmp_dir, ec);
}

// INV-BLIND-ABSOLUTE-SILENCE-FLOOR (lead-in/lead-out guard). A rip from a
// processed master usually OPENS with digital-zero lead-in silence and ENDS
// with lead-out silence. The absolute floor makes that silence detectable,
// so without the leading/trailing-silence suppression in analyze_blind_mode
// the lead-in is promoted to a spurious first "track" (and the lead-out to a
// trailing one) at confidence 1.0 — which the 0.6 gate cannot drop. Fixture:
// 3 s zero | 3 s tone | 3 s zero | 3 s tone | 3 s zero. The two real tracks
// start at ~3 s and ~9 s; the count must be EXACTLY 2 (not 4).
TEST_CASE("analyze_blind_mode: lead-in/lead-out silence is not split into tracks",
          "[blind][zero-floor]")
{
    namespace fs = std::filesystem;
    const int sr = 44100;
    std::vector<float> samples;
    append_silence(samples, 3.0, sr);          // lead-in
    append_tone(samples, 440.0, 3.0, sr, 0.7); // track 1
    append_silence(samples, 3.0, sr);          // inter-track gap
    append_tone(samples, 550.0, 3.0, sr, 0.7); // track 2
    append_silence(samples, 3.0, sr);          // lead-out

    fs::path tmp_dir = fs::temp_directory_path() / "mwaac_blind_leadinout";
    fs::create_directories(tmp_dir);
    fs::path tmp_path = tmp_dir / "leadinout.wav";
    REQUIRE(write_temp_wav(tmp_path, samples, sr));

    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0F;
    config.max_gap_seconds = 5.0F;
    config.analysis_sr = sr;

    auto result = mwaac::analyze_blind_mode(tmp_path, config);
    REQUIRE(result.has_value());
    const auto& analysis = result.value();

    // EXACTLY 2 tracks: lead-in and lead-out suppressed, one inter-track gap.
    REQUIRE(analysis.split_points.size() == 2);
    // Track 1 starts where the lead-in ends (~3 s), not at sample 0.
    CHECK(std::abs(analysis.split_points[0].start_sample
                   - static_cast<int64_t>(sr) * 3) < sr / 10);
    // Track 2 starts after track 1 + inter-track gap (~9 s).
    CHECK(std::abs(analysis.split_points[1].start_sample
                   - static_cast<int64_t>(sr) * 9) < sr / 10);

    std::error_code ec;
    fs::remove(tmp_path, ec);
    fs::remove(tmp_dir, ec);
}

// INV-BLIND-NATIVE-COORDS — analyze_blind_mode emits SplitPoint.start_sample
// in ANALYSIS-rate (config.analysis_sr) sample space; the caller (main.cpp's
// blind branch) converts to native rate via analysis_to_native_sample before
// cutting/printing. This pins BOTH halves of that contract on a fixture whose
// native rate (96000) differs from the analysis rate (44100):
//   (a) the returned start is ~6 s in ANALYSIS space (near 6*44100), proving
//       the output is analysis-rate and the caller MUST convert it; and
//   (b) analysis_to_native_sample(start, 96000, 44100) maps back to ~6 s of
//       native time, proving the conversion main.cpp applies is correct.
// Pre-Bug2-cure, main.cpp consumed the analysis-rate start directly at native
// rate, cutting every track ~96000/44100 = 2.18x too early.
TEST_CASE("analyze_blind_mode: split coordinates are analysis-rate, convertible to native",
          "[blind][coord]")
{
    namespace fs = std::filesystem;
    const int native_sr = 96000;
    const int analysis_sr = 44100;
    std::vector<float> samples;
    append_tone(samples, 440.0, 3.0, native_sr, 0.7);
    append_silence(samples, 3.0, native_sr);
    append_tone(samples, 550.0, 3.0, native_sr, 0.7);

    fs::path tmp_dir = fs::temp_directory_path() / "mwaac_blind_coord";
    fs::create_directories(tmp_dir);
    fs::path tmp_path = tmp_dir / "coord_96k.wav";
    REQUIRE(write_temp_wav(tmp_path, samples, native_sr));

    mwaac::BlindModeConfig config;
    config.min_gap_seconds = 2.0F;
    config.max_gap_seconds = 5.0F;
    config.analysis_sr = analysis_sr;  // force native != analysis

    auto result = mwaac::analyze_blind_mode(tmp_path, config);
    REQUIRE(result.has_value());
    const auto& analysis = result.value();
    REQUIRE(analysis.split_points.size() >= 2);

    const int64_t analysis_start = analysis.split_points[1].start_sample;
    // (a) output is analysis-rate: ~6 s * 44100, NOT ~6 s * 96000.
    CHECK(std::abs(analysis_start - static_cast<int64_t>(analysis_sr) * 6)
          < analysis_sr / 10);

    // (b) the documented conversion recovers ~6 s of native time.
    const int64_t native_start =
        mwaac::analysis_to_native_sample(analysis_start, native_sr, analysis_sr);
    const double native_seconds =
        static_cast<double>(native_start) / native_sr;
    INFO("analysis_start=" << analysis_start << " native_start=" << native_start
         << " native_seconds=" << native_seconds);
    CHECK(std::abs(native_seconds - 6.0) < 0.1);

    std::error_code ec;
    fs::remove(tmp_path, ec);
    fs::remove(tmp_dir, ec);
}

// M-6: compile-time contracts on the scoped typed-index bridge.
// The static_asserts inside core/frame_sample_bridge.hpp already enforce
// these contracts at every TU that includes the header, but pinning
// them in a TEST_CASE means a future regression that strips the
// `explicit` qualifier (or otherwise allows mixing) shows up as a
// recognisable test-source failure, not just a build error in an
// internal header.
TEST_CASE("M-6: SampleIdx/FrameIdx reject mixing and implicit conversion",
          "[blind][m-6]")
{
    using mwaac::detail::SampleIdx;
    using mwaac::detail::FrameIdx;

    // No implicit construction in either direction.
    STATIC_REQUIRE(!std::is_constructible_v<SampleIdx, FrameIdx>);
    STATIC_REQUIRE(!std::is_constructible_v<FrameIdx, SampleIdx>);

    // No implicit conversion from raw integers (must use explicit ctor).
    STATIC_REQUIRE(!std::is_convertible_v<std::int64_t, SampleIdx>);
    STATIC_REQUIRE(!std::is_convertible_v<std::size_t, FrameIdx>);

    // No implicit decay to raw integers (must access .value explicitly).
    STATIC_REQUIRE(!std::is_convertible_v<SampleIdx, std::int64_t>);
    STATIC_REQUIRE(!std::is_convertible_v<FrameIdx, std::size_t>);

    // Explicit construction works (sanity check — the explicit ctor
    // should not be removed by accident).
    STATIC_REQUIRE(std::is_constructible_v<SampleIdx, std::int64_t>);
    STATIC_REQUIRE(std::is_constructible_v<FrameIdx, std::size_t>);
}

TEST_CASE("M-6: frame_to_sample bridge multiplies by hop_length",
          "[blind][m-6]")
{
    using mwaac::detail::SampleIdx;
    using mwaac::detail::FrameIdx;
    using mwaac::detail::frame_to_sample;

    // The bridge is the ONLY supported FrameIdx → SampleIdx conversion
    // path. These runtime assertions verify the multiplication is
    // correct on representative inputs; the typed-bridge guarantee
    // (that no other conversion compiles) is in the static_asserts
    // above and in the header itself.
    const int hop_length = 551;  // matches analyze_blind_mode's hop at 44100 Hz

    CHECK(frame_to_sample(FrameIdx{0}, hop_length).value == 0);
    CHECK(frame_to_sample(FrameIdx{1}, hop_length).value == 551);
    CHECK(frame_to_sample(FrameIdx{100}, hop_length).value == 55100);
    CHECK(frame_to_sample(FrameIdx{397}, hop_length).value == 218747);

    // Compile-time evaluable: frame_to_sample is constexpr.
    constexpr SampleIdx s = frame_to_sample(FrameIdx{42}, 551);
    STATIC_REQUIRE(s.value == 23142);
}