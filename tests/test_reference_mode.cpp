#include <catch2/catch_test_macros.hpp>
#include "modes/reference_mode.hpp"

#include <cstdint>

// These test cases existed only as placeholders that asserted REQUIRE(true).
// They are skipped pending a real test-fixture effort (see BACKLOG.md item
// FIXTURE-REF). Once reproducible synthetic vinyl rips exist with known
// reference tracks and known ground-truth boundaries, they will be
// rewritten to verify actual alignment behavior within a sample-count
// tolerance.
//
// Leaving them in the tree as SKIPs rather than deleting them so the
// [reference] tag is reserved for the real tests that will replace them.

TEST_CASE("Reference mode: per-track alignment to synthetic vinyl", "[reference]") {
    SKIP("TODO(test-fixtures): FIXTURE-REF — synthetic vinyl rip with known "
         "track boundaries is not yet in tests/fixtures/. Will assert that "
         "align_per_track lands each track within ±N samples of truth.");
}

TEST_CASE("Reference mode: natural filename sort ordering", "[reference]") {
    SKIP("TODO(test-fixtures): FIXTURE-REF — relies on filesystem fixture "
         "that doesn't exist yet. Will verify 'Track 2.wav' < 'Track 10.wav' "
         "at the public API level.");
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
