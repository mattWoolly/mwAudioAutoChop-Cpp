#include <catch2/catch_test_macros.hpp>
#include "modes/reference_mode.hpp"

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
