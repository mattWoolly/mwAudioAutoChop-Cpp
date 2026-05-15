#include <catch2/catch_test_macros.hpp>
#include "modes/reference_mode.hpp"
#include "core/audio_buffer.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// M-REF-ALIGN-UNIT — un-SKIP per-track alignment unit test against landed fixture.
//
// Pre-cure: this case was a SKIP placeholder waiting on FIXTURE-REF, which
// landed via PR #23 but only wired the fixture into test_integration.
// Post-cure (this PR): exercises the FIXTURE-REF v1 manifest at the
// align_per_track unit-level (not the full analyze_reference_mode pipeline
// the integration tests hit), so an alignment-precision regression that
// still passes the gap-detection integration cases shows up here.
//
// Tolerance constant kRefFixtureToleranceSamples mirrors the integration
// test's value at tests/test_integration.cpp:53-55 (50 ms × 44100 Hz / 1000
// = 2205 samples) per BACKLOG M-REF-ALIGN-UNIT exit criterion 2's
// "consistency check" requirement. The two surfaces share the same physical
// alignment-precision budget; a separate name on each side documents that
// they are not coincidentally equal.
TEST_CASE("Reference mode: per-track alignment to synthetic vinyl", "[reference]") {
    namespace fs = std::filesystem;

    const fs::path fixture_dir(MWAAC_REF_FIXTURE_V1_DIR);
    const fs::path vinyl_path = fixture_dir / "vinyl.wav";
    const fs::path refs_dir = fixture_dir / "refs";
    const fs::path manifest_path = fixture_dir / "manifest.txt";

    REQUIRE(fs::exists(vinyl_path));
    REQUIRE(fs::is_directory(refs_dir));
    REQUIRE(fs::exists(manifest_path));

    // Load vinyl at native rate (FIXTURE-REF v1 is mono PCM_16 @ 44100 Hz —
    // see tests/fixtures/ref_v1/README.md).
    auto vinyl_result = mwaac::load_audio_mono(vinyl_path, /*target_sample_rate=*/0);
    REQUIRE(vinyl_result.has_value());
    const auto& vinyl = vinyl_result.value();
    REQUIRE(vinyl.sample_rate == 44100);

    // Load reference tracks via the production loader at the same native
    // rate so align_per_track sees both buffers at the same SR (no
    // implicit resampling) and offsets come back in 44100-Hz samples
    // directly comparable to the manifest's ground truth.
    auto tracks_result =
        mwaac::load_reference_tracks(refs_dir, vinyl.sample_rate);
    REQUIRE(tracks_result.has_value());
    const auto& tracks = tracks_result.value();
    REQUIRE(tracks.size() == 3);

    // Parse the flat KEY=VALUE manifest into a string→string map.
    std::map<std::string, std::string> manifest;
    {
        std::ifstream in(manifest_path);
        REQUIRE(in.is_open());
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            manifest.emplace(line.substr(0, eq), line.substr(eq + 1));
        }
    }
    REQUIRE(manifest.count("num_tracks") == 1);
    REQUIRE(std::stoul(manifest["num_tracks"]) == 3);
    REQUIRE(manifest.count("sample_rate") == 1);
    REQUIRE(std::stoi(manifest["sample_rate"]) == 44100);

    // Tolerance constant. Mirrors test_integration.cpp:54-55. 50 ms at
    // 44100 Hz native = 2205 samples. Named so an alignment regression
    // that drifts the constant produces a single recognisable diff site.
    constexpr int64_t kRefFixtureToleranceSamples = (50LL * 44100) / 1000;
    static_assert(kRefFixtureToleranceSamples == 2205,
                  "Tolerance constant out of sync with integration test");

    auto offsets =
        mwaac::align_per_track(vinyl, tracks, /*music_start_sample=*/0);
    REQUIRE(offsets.size() == 3);

    for (std::size_t i = 0; i < 3; ++i) {
        const std::string key =
            "track" + std::to_string(i + 1) + "_start_sample";
        REQUIRE(manifest.count(key) == 1);
        const int64_t truth_start =
            static_cast<int64_t>(std::stoll(manifest[key]));
        const int64_t actual_start = offsets[i].first;
        const int64_t delta =
            actual_start > truth_start ? actual_start - truth_start
                                       : truth_start - actual_start;
        INFO("Track " << (i + 1)
                      << ": truth_start=" << truth_start
                      << " actual_start=" << actual_start
                      << " delta=" << delta
                      << " (tolerance=" << kRefFixtureToleranceSamples << ")");
        CHECK(delta <= kRefFixtureToleranceSamples);
    }
}

// Mi-17 — un-SKIP natural-sort filename ordering + assert primary invariant.
//
// Pre-cure: this case was a SKIP placeholder. The BACKLOG mandated
// `natural_less("Track 2.wav", "Track 10.wav") == true` as the minimum
// assertion. natural_less itself was hardened against std::stoll throws
// (see Mi-17 entry in BACKLOG.md and the docstring on natural_less in
// reference_mode.hpp); the overflow-doesn't-throw axis is exercised by
// the second TEST_CASE below.
TEST_CASE("Reference mode: natural filename sort ordering", "[reference]") {
    using mwaac::natural_less;

    // Primary invariant — numeric (not lex) ordering on digit runs.
    // Lex compare would put "Track 10.wav" < "Track 2.wav" because '1' < '2';
    // natural compare must give "Track 2.wav" < "Track 10.wav".
    CHECK(natural_less("Track 2.wav", "Track 10.wav"));
    CHECK_FALSE(natural_less("Track 10.wav", "Track 2.wav"));

    // Same prefix, different decade boundaries.
    CHECK(natural_less("Track 1.wav", "Track 2.wav"));
    CHECK(natural_less("Track 9.wav", "Track 10.wav"));
    CHECK(natural_less("Track 99.wav", "Track 100.wav"));

    // Strict-weak: equal inputs are NOT strictly less than each other
    // (irreflexive). std::sort relies on this.
    CHECK_FALSE(natural_less("Track 5.wav", "Track 5.wav"));

    // Trailing extensions tie-break correctly when prefixes match.
    CHECK(natural_less("Track 1.aiff", "Track 1.wav"));
}

// Mi-17 — overflow does not throw / abort (separate TEST_CASE so the
// std::stoll regression's failure mode — terminate the binary — would be
// isolated to this case rather than masking the primary-invariant case
// above).
//
// Pre-cure: std::stoll on the 25-char and 21-char digit runs below threw
// std::out_of_range, propagated out of std::sort, and aborted the process.
// Post-cure (Mi-17 length-then-lex compare on zero-stripped digit strings):
// finite, deterministic strict-weak ordering for any digit length.
TEST_CASE("natural_less: digit run > 18 characters does not throw",
          "[reference]")
{
    using mwaac::natural_less;

    // 25-digit runs, identical lengths — equivalent numeric compare via
    // lex on equal-length zero-stripped digits.
    //   1234567890123456789012345 < 1234567890123456789012346
    CHECK(natural_less(
        "Track 1234567890123456789012345.wav",
        "Track 1234567890123456789012346.wav"));

    // Different digit-run lengths, no leading zero — longer is
    // numerically larger.
    //   99999999999999999999 (20) < 100000000000000000000 (21)
    CHECK(natural_less(
        "Track 99999999999999999999.wav",
        "Track 100000000000000000000.wav"));

    // Mixed — short numeric on one side, pathological on the other.
    //   5 < 12345678901234567890
    CHECK(natural_less(
        "Track 5.wav",
        "Track 12345678901234567890.wav"));

    // Strict-weak symmetry on the pathological inputs (a < b ⇒ !(b < a)).
    CHECK_FALSE(natural_less(
        "Track 1234567890123456789012346.wav",
        "Track 1234567890123456789012345.wav"));
}

// C-4: pin the analysis->native conversion to round-to-nearest semantics.
// The previous implementation truncated toward zero (integer division),
// which at 192 kHz native / 44.1 kHz analysis can place a boundary up to
// ~9 native-rate samples below the mathematically correct position. This
// test asserts the cure: at sample values where rounding and truncation
// produce *different* int64_t outputs, the helper returns the rounded
// value (half away from zero), bounded by 1 native-rate sample of the
// real-valued conversion.
TEST_CASE("Reference mode: native-rate boundary is rounded not truncated",
          "[reference]")
{
    using mwaac::analysis_to_native_sample;

    SECTION("44.1 kHz analysis -> 192 kHz native (round vs truncate differ)") {
        // analysis=2: exact = 2 * 192000 / 44100 = 8.7074829...
        //   truncate -> 8; round-half-away-from-zero -> 9.
        REQUIRE(analysis_to_native_sample(2, 192000, 44100) == 9);
        // analysis=5: exact = 21.7687...; truncate -> 21; round -> 22.
        REQUIRE(analysis_to_native_sample(5, 192000, 44100) == 22);
        // analysis=44099: exact = 191995.6463...;
        //   truncate -> 191995; round -> 191996.
        REQUIRE(analysis_to_native_sample(44099, 192000, 44100) == 191996);
        // Large positive value (worst-case round-trip class): exact has a
        // fractional part of ~0.6463; truncate would still be one short.
        // analysis=88199 = 2*44100 - 1: exact = 384000 - 4.3537... =
        //   383995.6463...; truncate -> 383995; round -> 383996.
        REQUIRE(analysis_to_native_sample(88199, 192000, 44100) == 383996);
    }

    SECTION("Exact-integer ratios round-trip with no rounding") {
        // analysis_sr == native_sr: identity for any input.
        REQUIRE(analysis_to_native_sample(0,        44100, 44100) == 0);
        REQUIRE(analysis_to_native_sample(1,        44100, 44100) == 1);
        REQUIRE(analysis_to_native_sample(123456,   44100, 44100) == 123456);
        // Exact 2:1 upsample.
        REQUIRE(analysis_to_native_sample(0,        88200, 44100) == 0);
        REQUIRE(analysis_to_native_sample(7,        88200, 44100) == 14);
        REQUIRE(analysis_to_native_sample(44100,   192000, 44100) == 192000);
    }

    SECTION("Exact half-boundary rounds away from zero") {
        // analysis_sr=2, native_sr=3, analysis_sample=1:
        //   exact = 1.5; truncate -> 1; round-half-away-from-zero -> 2.
        REQUIRE(analysis_to_native_sample(1, 3, 2) == 2);
        // analysis_sr=2, native_sr=3, analysis_sample=3:
        //   exact = 4.5; truncate -> 4; round -> 5.
        REQUIRE(analysis_to_native_sample(3, 3, 2) == 5);
    }

    SECTION("Negative analysis_sample: round half away from zero") {
        // analysis=-2 at (44100 -> 192000): exact = -8.7074...;
        //   truncate-toward-zero -> -8; round-half-away-from-zero -> -9.
        REQUIRE(analysis_to_native_sample(-2, 192000, 44100) == -9);
        // analysis=-5: exact = -21.7687...;
        //   truncate -> -21; round -> -22.
        REQUIRE(analysis_to_native_sample(-5, 192000, 44100) == -22);
        // Negative half-boundary: analysis_sr=2, native_sr=3, sample=-1:
        //   exact = -1.5; round-half-away-from-zero -> -2.
        REQUIRE(analysis_to_native_sample(-1, 3, 2) == -2);
    }
}

// M-9: empty-vinyl regression. Pre-cure, align_per_track ran the
// per-track loop and ended each iteration with
//   std::clamp(chosen_pos, int64_t{0},
//              static_cast<int64_t>(vinyl.samples.size()) - 1);
// which evaluates to std::clamp(x, 0, -1) when vinyl is empty —
// hi < lo, undefined behavior per cppreference. Post-cure, the
// function early-returns an empty offsets vector before entering the
// loop. Test passes a non-empty `tracks` so the loop *would* run if
// the guard were missing; UBSan-clean execution is the second signal.
TEST_CASE("align_per_track: empty vinyl returns empty offsets, no UB",
          "[reference]")
{
    mwaac::AudioBuffer vinyl;
    vinyl.sample_rate = 44100;
    // vinyl.samples is default-constructed empty.
    REQUIRE(vinyl.samples.empty());

    // One non-empty track: forces the function to be tempted to enter
    // the per-track loop, so the guard is the only thing standing
    // between us and the std::clamp UB at the end of each iteration.
    std::vector<mwaac::ReferenceTrack> tracks(1);
    tracks[0].audio.sample_rate = 44100;
    tracks[0].audio.samples.assign(44100, 0.0f);  // 1 s of silence
    tracks[0].duration_samples =
        static_cast<int64_t>(tracks[0].audio.samples.size());

    auto offsets = mwaac::align_per_track(vinyl, tracks, /*music_start=*/0);
    REQUIRE(offsets.empty());
}
