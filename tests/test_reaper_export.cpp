#include <catch2/catch_test_macros.hpp>
#include "modes/reaper_export.hpp"

#include <filesystem>

// M-REAPER-EXPORT-SORT-THROW — sibling of Mi-17. Pre-cure
// `natural_less_filename` carried a duplicate of `natural_less`'s
// std::stoll-throw bug that propagated out of the std::sort callsite at
// `list_reference_paths` to abort the program. Post-cure: the function
// is a thin delegation to `mwaac::natural_less` (which Mi-17 hardened
// to length-then-lex compare on zero-stripped digit strings).
//
// These tests assert the cure shape on the path-taking signature
// directly. The underlying numeric-compare semantics are exhaustively
// covered by the Mi-17 tests in tests/test_reference_mode.cpp; the
// tests below verify the path-string-extraction wrapper is
// well-formed and the delegation is wired correctly.

TEST_CASE("Reaper export: natural filename sort ordering", "[reaper_export]") {
    using mwaac::natural_less_filename;
    namespace fs = std::filesystem;

    // Primary invariant — numeric (not lex) ordering. Lex would put
    // "Track 10.wav" < "Track 2.wav"; natural compare must give
    // "Track 2.wav" < "Track 10.wav".
    CHECK(natural_less_filename(fs::path("Track 2.wav"),
                                fs::path("Track 10.wav")));
    CHECK_FALSE(natural_less_filename(fs::path("Track 10.wav"),
                                      fs::path("Track 2.wav")));

    // Strict-weak: equal inputs are NOT strictly less than each other.
    CHECK_FALSE(natural_less_filename(fs::path("Track 5.wav"),
                                      fs::path("Track 5.wav")));

    // Path argument carries a directory prefix; comparison uses
    // .filename().string(), so the directory part should not affect
    // ordering (both filenames "Track 2.wav" tie regardless of dir).
    CHECK_FALSE(natural_less_filename(fs::path("/refs/Track 2.wav"),
                                      fs::path("/elsewhere/Track 2.wav")));
}

TEST_CASE("Reaper export: natural_less_filename does not throw on >18-char digit run",
          "[reaper_export]")
{
    using mwaac::natural_less_filename;
    namespace fs = std::filesystem;

    // Pre-cure: std::stoll on these digit runs threw std::out_of_range
    // and propagated out of std::sort to abort the program when called
    // from list_reference_paths against a directory containing such a
    // filename. Post-cure: deterministic strict-weak ordering for any
    // digit length, no throw.

    // 25-digit runs, identical lengths.
    CHECK(natural_less_filename(
        fs::path("Track 1234567890123456789012345.wav"),
        fs::path("Track 1234567890123456789012346.wav")));

    // Different digit-run lengths (no leading zero, longer = larger).
    CHECK(natural_less_filename(
        fs::path("Track 99999999999999999999.wav"),
        fs::path("Track 100000000000000000000.wav")));

    // Mixed pathological-vs-short.
    CHECK(natural_less_filename(
        fs::path("Track 5.wav"),
        fs::path("Track 12345678901234567890.wav")));

    // Strict-weak symmetry on the pathological inputs.
    CHECK_FALSE(natural_less_filename(
        fs::path("Track 1234567890123456789012346.wav"),
        fs::path("Track 1234567890123456789012345.wav")));
}
