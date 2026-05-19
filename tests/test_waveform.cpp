#include <catch2/catch_test_macros.hpp>
#include "tui/waveform.hpp"

#include <cstdint>
#include <utility>
#include <vector>

// First test target for the `src/tui/` library. Mi-8 / Mi-9 / Mi-10
// BACKLOG entries all note "TUI tests are currently absent; add a
// headless unit test at the state-mutator level" — this file is the
// initial wedge, scoped to `render_waveform` (a pure function that
// requires no TUI event-loop or terminal harness). When Mi-8 / Mi-9
// dispatch they will need a separate headless-state-mutator harness;
// this file is NOT that harness.

// M-WAVEFORM-CLAMP-UB regression test. Pre-cure:
// `render_waveform(..., height=1, ...)` passed the early-return guard
// at waveform.cpp:53 (`peaks.empty() || height <= 0`), computed
// `waveform_height = height - 1 = 0`, and then invoked
// `std::clamp(min_row, 0, waveform_height - 1)` with hi=-1 < lo=0 —
// undefined behavior per cppreference. Post-cure: the input-boundary
// guard is tightened to `height < 2` (per BACKLOG cure shape (a) —
// "fail-fast at the input boundary; render_waveform requires at least
// one waveform row plus one track-number row"), so the degenerate
// height=1 input returns empty without entering the per-column loop.
//
// PRIMARY SIGNAL: sanitizer-clean run on this test (the cycle's CI
// includes a UBsan+ASan job; pre-cure this case would trip UBsan on
// the std::clamp call). FUNCTIONAL SIGNAL: the returned vector is
// well-formed (empty, per cure (a)).
TEST_CASE("render_waveform: height==1 does not invoke std::clamp with hi < lo",
          "[tui][waveform][m-waveform-clamp-ub]")
{
    // Non-empty peaks force the per-column loop to execute pre-cure;
    // each peak then trips the UB at waveform.cpp:68-69. Post-cure the
    // input-boundary guard returns before the loop ever starts.
    std::vector<std::pair<float, float>> peaks = {
        {-0.5f, 0.5f},
        {-0.7f, 0.3f},
        {-0.2f, 0.9f},
    };
    std::vector<mwaac::tui::MarkerInfo> markers;

    auto rows = mwaac::tui::render_waveform(peaks, /*height=*/1,
                                            /*cursor_pos=*/0, markers);

    // Per cure shape (a), height < 2 returns empty (no usable waveform
    // rows to draw — one row is reserved for track numbers, so
    // height=1 leaves zero rows for the waveform).
    CHECK(rows.empty());
}

// Defensive: height==0 must still return empty (was guarded pre-cure
// by the `height <= 0` half of the early-return; cure preserves this
// arm of the guard by widening to `height < 2`).
TEST_CASE("render_waveform: height==0 returns empty",
          "[tui][waveform][m-waveform-clamp-ub]")
{
    std::vector<std::pair<float, float>> peaks = {{-0.5f, 0.5f}};
    std::vector<mwaac::tui::MarkerInfo> markers;

    auto rows = mwaac::tui::render_waveform(peaks, /*height=*/0,
                                            /*cursor_pos=*/0, markers);
    CHECK(rows.empty());
}

// No-regression guard: render_waveform produces well-formed output
// for the smallest valid height (2 — one waveform row + one track-
// number row). Catches a hypothetical regression that widened the
// guard further (e.g., to `height < 3`) and broke previously-valid
// inputs.
TEST_CASE("render_waveform: height==2 returns one waveform row + one track-number row",
          "[tui][waveform][m-waveform-clamp-ub]")
{
    std::vector<std::pair<float, float>> peaks = {
        {-0.5f, 0.5f},
        {-0.7f, 0.3f},
    };
    std::vector<mwaac::tui::MarkerInfo> markers;

    auto rows = mwaac::tui::render_waveform(peaks, /*height=*/2,
                                            /*cursor_pos=*/0, markers);

    // render_waveform constructs `rows` of size `height + 1 == 3`;
    // it then writes into row 0 (waveform) and row `waveform_height == 1`
    // (track numbers). The trailing row at index 2 stays empty.
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].size() == peaks.size());   // waveform row, one char per column
    CHECK(rows[1].size() == peaks.size());   // track-number row, one char per column
}
