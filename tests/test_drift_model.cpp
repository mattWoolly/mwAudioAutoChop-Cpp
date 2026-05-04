#include <catch2/catch_test_macros.hpp>
#include "core/alignment_result.hpp"

#include <cstdint>

// DRIFT-MODEL-RATE-TRUNCATION (Tier 5, BACKLOG): the previous
// implementation of DriftModel::ref_to_vinyl_sample truncated at three
// internal sites — two pure-integer-division returns (segment_offsets
// empty / max_pos == 0 fast paths) and one float→int cast on the
// polynomial-applied double product. At 192 kHz native / 44.1 kHz analysis
// truncation can land the vinyl sample up to ~9 native samples below the
// mathematically correct position. These tests pin the cure: each site now
// rounds (round-half-away-from-zero) to within 1 native-rate sample, the
// same tolerance C-4 establishes for analysis_to_native_sample. Inputs are
// chosen so the rounded result and the truncated result are different
// int64_ts; assertions are exact-match, which means each test would have
// failed under the pre-cure code.
//
// DriftModel currently has zero production callers
// (`grep -rn ref_to_vinyl_sample src/` shows no hits outside its own
// definition); the BACKLOG entry documents this dormancy. The fix is
// preventive — it stops a future epic from activating DriftModel and
// silently reintroducing the C-4 defect class.

namespace {

// 192 kHz / 44.1 kHz is the canonical "rounding matters" rate pair from
// C-4's analysis: GCD = 300, so the conversion almost never lands on an
// integer and truncation can lag the true value by up to ~9 samples.
constexpr int kNativeSr   = 192000;
constexpr int kAnalysisSr = 44100;

}  // namespace

TEST_CASE(
    "DriftModel::ref_to_vinyl_sample: empty-segment fast path rounds, "
    "not truncates",
    "[drift_model]")
{
    // Empty segment_offsets engages the early-return at the
    // (post-cure) line that was previously
    //   return static_cast<int64_t>(ref_sample * native_sr / analysis_sr);
    // and is now
    //   return analysis_to_native_sample(ref_sample, native_sr, analysis_sr);
    mwaac::DriftModel model;
    REQUIRE(model.segment_offsets.empty());

    SECTION("ref_sample=2 → exact 8.7074..., round 9 (truncate would be 8)") {
        REQUIRE(model.ref_to_vinyl_sample(2, kNativeSr, kAnalysisSr) == 9);
    }
    SECTION("ref_sample=5 → exact 21.7687..., round 22 (truncate would be 21)") {
        REQUIRE(model.ref_to_vinyl_sample(5, kNativeSr, kAnalysisSr) == 22);
    }
    SECTION("ref_sample=44099 → exact 191995.6463..., round 191996 "
            "(truncate would be 191995)") {
        REQUIRE(model.ref_to_vinyl_sample(44099, kNativeSr, kAnalysisSr)
                == 191996);
    }
}

TEST_CASE(
    "DriftModel::ref_to_vinyl_sample: max_pos == 0 path rounds, "
    "not truncates",
    "[drift_model]")
{
    // segment_offsets non-empty but the back-segment's first element is 0,
    // engaging the second early-return. The path under test is
    // distinct from the empty-segment path because it runs after the
    // segment_offsets.empty() check; both used to call the same truncation
    // formula and both now route through analysis_to_native_sample.
    mwaac::DriftModel model;
    model.segment_offsets.emplace_back(int64_t{0}, int64_t{0});
    REQUIRE_FALSE(model.segment_offsets.empty());
    REQUIRE(model.segment_offsets.back().first == 0);

    SECTION("ref_sample=2 → round 9 (truncate would be 8)") {
        REQUIRE(model.ref_to_vinyl_sample(2, kNativeSr, kAnalysisSr) == 9);
    }
    SECTION("ref_sample=5 → round 22 (truncate would be 21)") {
        REQUIRE(model.ref_to_vinyl_sample(5, kNativeSr, kAnalysisSr) == 22);
    }
    SECTION("ref_sample=44099 → round 191996 (truncate would be 191995)") {
        REQUIRE(model.ref_to_vinyl_sample(44099, kNativeSr, kAnalysisSr)
                == 191996);
    }
}

TEST_CASE(
    "DriftModel::ref_to_vinyl_sample: polynomial-applied path rounds, "
    "not truncates",
    "[drift_model]")
{
    // Engage the polynomial branch: segment_offsets non-empty AND
    // max_pos > 0 AND coefficients non-empty.
    //
    // Math (post-cure, std::llround on the double product):
    //   max_pos = 1000, ref_sample = 100  →  t = 0.1
    //   coefficients = {0.5, 0.0}         →  offset = 0.5 + 0.0*t = 0.5
    //   vinyl_sample (double) = 100 + 0.5 = 100.5
    //   vinyl_sample * native / analysis = 100.5 * 192000 / 44100
    //                                    = 19296000 / 44100
    //                                    ≈ 437.55102040816...
    //   std::llround → 438 ; truncate (pre-cure) → 437.
    // Both 100.5 and 19296000.0 are exactly representable in double, so
    // the only floating-point rounding is in the division — its result
    // straddles the rounding boundary cleanly and is not in danger of
    // tipping under platform variance.
    mwaac::DriftModel model;
    model.segment_offsets.emplace_back(int64_t{1000}, int64_t{0});
    model.coefficients = {0.5, 0.0};
    REQUIRE(model.segment_offsets.back().first == 1000);
    REQUIRE_FALSE(model.coefficients.empty());

    SECTION("ref_sample=100, offset=+0.5 → round 438 (truncate would be 437)") {
        REQUIRE(model.ref_to_vinyl_sample(100, kNativeSr, kAnalysisSr) == 438);
    }

    SECTION("ref_sample=200, offset=+0.5 → round 873 (truncate would be 872)") {
        // t = 0.2, offset still 0.5 (c1 == 0).
        // vinyl_sample = 200.5; product = 200.5 * 192000 / 44100
        //              = 38496000 / 44100 ≈ 872.9251700680...
        // std::llround → 873 ; truncate → 872.
        // Different (truncate, round) pair from sub-case 1, with a
        // larger ref_sample, exercises the same rounding behaviour
        // across multiple polynomial inputs.
        REQUIRE(model.ref_to_vinyl_sample(200, kNativeSr, kAnalysisSr) == 873);
    }
}
