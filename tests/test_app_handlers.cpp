#include <catch2/catch_test_macros.hpp>
#include "tui/app_handlers.hpp"
#include "tui/app.hpp"
#include "core/split_point.hpp"
#include "core/audio_buffer.hpp"

#include <cstdint>
#include <vector>

// Mi-8 / Tier 7: state-mutator harness tests. This file is the second
// test target to link mwaac_tui (after test_waveform). Tests construct
// a synthetic AppState directly and invoke the mutators in
// app_handlers.cpp without any FTXUI dependency — no terminal, no
// event loop, no screen.
//
// TEST_CASE classification (per Mi-8 audit-1 finding 2):
//
//   Mi-8 regression-guards (would FAIL with cure reverted):
//     - nudge_marker_right: clamps against total_samples - 1
//     - nudge_marker_right: clamps against next marker's start_sample
//     - nudge_marker_right: last marker clamps against total_samples - 1, not next
//     - nudge_marker_left: clamps against previous marker's end_sample
//     - nudge_marker_right: empty audio (total_samples == 0) no-ops
//     - (Implicit) the normal-shift tests confirm the cure didn't
//       break working paths — they pass both pre- and post-cure but
//       fail under any cure that breaks the non-degenerate path.
//
//   Invariant locks / defensive documentation (pass pre-cure too):
//     - nudge_marker_left: clamps against 0  (pre-cure guard covered this)
//     - nudge_marker_*: empty split_points no-ops both directions
//     - nudge_marker_*: selected_marker out of range no-ops both directions
//     - nudge_marker_*: preserves duration_samples (block-shift property)
//
// Future Mi-9 (and beyond) authors: when extending this harness,
// preserve the regression-guard / invariant-lock distinction. A test
// that passes pre-cure is documentation, not a guard.

namespace {

// Helper to build an AppState with `total_samples` of silent audio
// and the given split points.
mwaac::tui::AppState build_state(std::int64_t total_samples,
                                  std::vector<mwaac::SplitPoint> markers,
                                  int selected) {
    mwaac::tui::AppState s;
    s.audio.samples.resize(static_cast<std::size_t>(total_samples), 0.0f);
    s.audio.sample_rate = 44100;
    s.split_points = std::move(markers);
    s.selected_marker = selected;
    return s;
}

mwaac::SplitPoint marker(std::int64_t start, std::int64_t end) {
    mwaac::SplitPoint sp;
    sp.start_sample = start;
    sp.end_sample = end;
    return sp;
}

} // namespace

// ─── nudge_marker_right ────────────────────────────────────────────

TEST_CASE("nudge_marker_right: normal shift (no neighbors, well within bounds)",
          "[tui][app_handlers][mi-8]")
{
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 101);
    CHECK(s.split_points[0].end_sample == 201);
}

TEST_CASE("nudge_marker_right: clamps against total_samples - 1",
          "[tui][app_handlers][mi-8]")
{
    // Marker's end_sample is already at the last valid sample
    // (total_samples - 1 = 999). Nudge right must no-op.
    auto s = build_state(/*total=*/1000,
                         {marker(800, 999)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 800);
    CHECK(s.split_points[0].end_sample == 999);
}

TEST_CASE("nudge_marker_right: clamps against next marker's start_sample",
          "[tui][app_handlers][mi-8]")
{
    // marker[0] ends at 199; marker[1] starts at 200. The inter-track
    // gap invariant requires marker[0].end_sample < marker[1].start_sample.
    // Currently end=199 < start=200 (gap of 1). Nudging marker[0] right
    // would make end=200, which equals next.start — must no-op.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 500)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 199);
    // marker[1] unchanged.
    CHECK(s.split_points[1].start_sample == 200);
}

TEST_CASE("nudge_marker_right: last marker clamps against total_samples - 1, not next",
          "[tui][app_handlers][mi-8]")
{
    // marker[1] is last; only the global bound applies. Currently end=998;
    // nudge right should produce end=999 (still < 1000 = total_samples).
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 998)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[1].end_sample == 999);
}

// ─── nudge_marker_left ─────────────────────────────────────────────

TEST_CASE("nudge_marker_left: normal shift (no neighbors, well within bounds)",
          "[tui][app_handlers][mi-8]")
{
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].start_sample == 99);
    CHECK(s.split_points[0].end_sample == 199);
}

TEST_CASE("nudge_marker_left: clamps against 0",
          "[tui][app_handlers][mi-8]")
{
    // start_sample == 0 — pre-cure this was the only guarded case.
    auto s = build_state(/*total=*/1000,
                         {marker(0, 200)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].start_sample == 0);
    CHECK(s.split_points[0].end_sample == 200);
}

TEST_CASE("nudge_marker_left: clamps against previous marker's end_sample",
          "[tui][app_handlers][mi-8]")
{
    // marker[0] ends at 199; marker[1] starts at 200. Nudging marker[1]
    // left would make start=199 which equals prev.end — must no-op.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 500)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[1].start_sample == 200);
    CHECK(s.split_points[1].end_sample == 500);
}

// ─── degenerate inputs ─────────────────────────────────────────────

TEST_CASE("nudge_marker_*: empty split_points no-ops both directions",
          "[tui][app_handlers][mi-8]")
{
    auto s = build_state(/*total=*/1000, {}, /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points.empty());
}

TEST_CASE("nudge_marker_*: selected_marker out of range no-ops both directions",
          "[tui][app_handlers][mi-8]")
{
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/5);  // out of range
    mwaac::tui::nudge_marker_right(s);
    mwaac::tui::nudge_marker_left(s);
    // The (only) marker is unchanged.
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 200);
}

TEST_CASE("nudge_marker_right: empty audio (total_samples == 0) no-ops",
          "[tui][app_handlers][mi-8]")
{
    // Edge case: AppState with no audio loaded. total_samples - 1 = -1;
    // any nudge right with marker end >= 0 would exceed it (or invoke
    // signed arithmetic at the bound). Mutator must no-op.
    auto s = build_state(/*total=*/0,
                         {marker(0, 0)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 0);
    CHECK(s.split_points[0].end_sample == 0);
}

// ─── within-marker invariant preservation ──────────────────────────

TEST_CASE("nudge_marker_*: preserves duration_samples on every successful nudge",
          "[tui][app_handlers][mi-8]")
{
    // Both start and end shift by the same delta, so duration is
    // invariant. Verify across a sequence of nudges.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/0);
    const std::int64_t initial_duration = s.split_points[0].duration_samples();
    for (int i = 0; i < 50; ++i) {
        mwaac::tui::nudge_marker_right(s);
        CHECK(s.split_points[0].duration_samples() == initial_duration);
    }
    for (int i = 0; i < 100; ++i) {
        mwaac::tui::nudge_marker_left(s);
        CHECK(s.split_points[0].duration_samples() == initial_duration);
    }
}

// ─── Mi-9 view-bounds mutators ─────────────────────────────────────
//
// TEST_CASE classification (Mi-9 cure):
//
//   Mi-9 regression-guards (would FAIL with cure reverted):
//     - zoom_out: empty audio (total == 0) no-ops (pre-cure produced
//       view_end < view_start when total == 0)
//     - pan_to_start: empty audio no-ops (pre-cure produced
//       zero-range view_start == view_end == 0)
//     - pan_to_end: empty audio no-ops (same shape)
//     - zoom_in: empty audio no-ops (pre-cure would compute a negative
//       view_start from center=0 - new_range/2)
//     - INV-VIEW-NON-INVERTED holds after every mutator on
//       non-degenerate inputs (view_start < view_end strictly)
//
//   Invariant locks / behavioral documentation:
//     - zoom_in: normal halving with floor at MIN_VIEW_RANGE
//     - zoom_out: normal doubling capped at total
//     - pan_to_start: maintains current range when starting from
//       middle of file
//     - pan_to_end: symmetric to pan_to_start

namespace {

// Build a view-state with given view_start / view_end and total_samples.
mwaac::tui::AppState build_view_state(std::int64_t total_samples,
                                       std::int64_t view_start,
                                       std::int64_t view_end) {
    mwaac::tui::AppState s;
    s.audio.samples.resize(static_cast<std::size_t>(total_samples), 0.0f);
    s.audio.sample_rate = 44100;
    s.view_start = view_start;
    s.view_end = view_end;
    return s;
}

} // namespace

TEST_CASE("zoom_in: halves range about center, with floor at MIN_VIEW_RANGE",
          "[tui][app_handlers][mi-9]")
{
    // Range = 8000, center = 5000. Zoom in → range = 4000 about
    // center 5000 → [3000, 7000).
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/1000, /*view_end=*/9000);
    mwaac::tui::zoom_in(s);
    CHECK(s.view_start == 3000);
    CHECK(s.view_end == 7000);
    CHECK(s.view_start < s.view_end);  // INV-VIEW-NON-INVERTED holds

    // Range = 2000 (= 2 * MIN_VIEW_RANGE). Zoom in → range = 1000 (floor).
    s = build_view_state(/*total=*/100000, /*view_start=*/0, /*view_end=*/2000);
    mwaac::tui::zoom_in(s);
    CHECK(s.view_end - s.view_start == 1000);

    // Range = 1000 (already at floor). Zoom in → no-op.
    s = build_view_state(/*total=*/100000, /*view_start=*/0, /*view_end=*/1000);
    mwaac::tui::zoom_in(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 1000);
}

TEST_CASE("zoom_out: doubles range about center, capped at total_samples",
          "[tui][app_handlers][mi-9]")
{
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/40000, /*view_end=*/50000);
    mwaac::tui::zoom_out(s);
    // Range was 10000; doubled to 20000 about center 45000 →
    // [35000, 55000).
    CHECK(s.view_start == 35000);
    CHECK(s.view_end == 55000);

    // Range = 60000 doubled to 120000, capped at 100000.
    s = build_view_state(/*total=*/100000,
                         /*view_start=*/20000, /*view_end=*/80000);
    mwaac::tui::zoom_out(s);
    CHECK(s.view_end - s.view_start == 100000);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 100000);
}

TEST_CASE("pan_to_start: jumps view to [0, current_range)",
          "[tui][app_handlers][mi-9]")
{
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/50000, /*view_end=*/60000);
    mwaac::tui::pan_to_start(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 10000);  // current_range was 10000
}

TEST_CASE("pan_to_end: jumps view to [total - current_range, total)",
          "[tui][app_handlers][mi-9]")
{
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/20000, /*view_end=*/30000);
    mwaac::tui::pan_to_end(s);
    CHECK(s.view_end == 100000);
    CHECK(s.view_start == 90000);  // current_range was 10000
}

// ─── Mi-9 regression-guards: empty audio ───────────────────────────

TEST_CASE("zoom_in / zoom_out / pan_*: empty audio (total_samples == 0) no-op all four",
          "[tui][app_handlers][mi-9]")
{
    // Pre-cure: zoom_out with total==0 computed new_range=0, then
    // view_end = min(view_start + 0, 0) = 0. If view_start was already
    // 0, view_start == view_end (zero-range, violates strict-less-than).
    // pan_to_start / pan_to_end produced the same zero-range. zoom_in
    // would compute new_start from negative center arithmetic.
    // Post-cure: all four no-op on empty audio (no valid view exists).
    auto s = build_view_state(/*total=*/0,
                              /*view_start=*/0, /*view_end=*/0);
    mwaac::tui::zoom_in(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 0);

    mwaac::tui::zoom_out(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 0);

    mwaac::tui::pan_to_start(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 0);

    mwaac::tui::pan_to_end(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 0);
}

// ─── Mi-9 invariant lock ────────────────────────────────────────────

TEST_CASE("view mutators: INV-VIEW-NON-INVERTED holds across long mutation sequences",
          "[tui][app_handlers][mi-9]")
{
    // Alternate zoom_in / zoom_out / pan_to_start / pan_to_end and
    // assert the strict-less-than invariant holds after every call.
    // This locks the post-normalization guarantee.
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/10000, /*view_end=*/20000);
    for (int i = 0; i < 50; ++i) {
        mwaac::tui::zoom_out(s);
        CHECK(s.view_start < s.view_end);
        mwaac::tui::pan_to_end(s);
        CHECK(s.view_start < s.view_end);
        mwaac::tui::zoom_in(s);
        CHECK(s.view_start < s.view_end);
        mwaac::tui::pan_to_start(s);
        CHECK(s.view_start < s.view_end);
    }
}

// ─── Mi-9 view_end == 0 sentinel ────────────────────────────────────

TEST_CASE("view mutators: resolve view_end == 0 sentinel to total_samples",
          "[tui][app_handlers][mi-9]")
{
    // AppState default-constructs view_end == 0 meaning "auto-stretch
    // to file end" per the Renderer at app.cpp. The mutators resolve
    // this sentinel BEFORE normalization so the first zoom/pan
    // operates on the implicit-full-range view rather than treating
    // view_end == 0 literally.
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/0, /*view_end=*/0);
    mwaac::tui::zoom_in(s);
    // Implicit range was 100000; halved to 50000 about center 50000 →
    // [25000, 75000).
    CHECK(s.view_start == 25000);
    CHECK(s.view_end == 75000);
}

// ─── Mi-CURSOR-COL-CLAMP cursor mutators ───────────────────────────
//
// TEST_CASE classification (Mi-CURSOR-COL-CLAMP cure):
//
//   Mi-CURSOR-COL-CLAMP regression-guards (FAIL with cure reverted):
//     - move_cursor_right clamps at display_width - 1
//     - move_cursor_right clamps at display_width - 1 starting from 0
//     - move_cursor_right with display_width == 0 keeps cursor at 0
//     - move_cursor_right with display_width == 1 keeps cursor at 0
//
//   Invariant locks / behavioral documentation:
//     - move_cursor_left clamps at 0 (pre-cure guard preserved verbatim)
//     - move_cursor_*: cursor never exits [0, display_width-1] across
//       long sequences

TEST_CASE("move_cursor_left: clamps at 0",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    int cursor_col = 5;
    mwaac::tui::move_cursor_left(cursor_col);
    CHECK(cursor_col == 4);

    cursor_col = 0;
    mwaac::tui::move_cursor_left(cursor_col);
    CHECK(cursor_col == 0);
}

TEST_CASE("move_cursor_right: normal increment within display",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    int cursor_col = 5;
    mwaac::tui::move_cursor_right(cursor_col, /*display_width=*/100);
    CHECK(cursor_col == 6);
}

TEST_CASE("move_cursor_right: clamps at display_width - 1 (Mi-CURSOR-COL-CLAMP regression-guard)",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    // Pre-cure: `cursor_col++` had no upper bound. Display width 100
    // means valid range is [0, 99]; cursor at 99 must not move right.
    int cursor_col = 99;
    mwaac::tui::move_cursor_right(cursor_col, /*display_width=*/100);
    CHECK(cursor_col == 99);
}

TEST_CASE("move_cursor_right: clamps at display_width - 1 starting from 0",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    // Increment from 0 with display_width == 1: must stay at 0 (the
    // only valid column).
    int cursor_col = 0;
    mwaac::tui::move_cursor_right(cursor_col, /*display_width=*/1);
    CHECK(cursor_col == 0);
}

TEST_CASE("move_cursor_right: degenerate display_width == 0 keeps cursor at 0",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    // `display_width <= 0` is theoretically reachable (terminal width
    // computation could yield it on a 1-column terminal: `dimx - 2 = -1`).
    // The mutator must not invert (move cursor to a negative value).
    int cursor_col = 0;
    mwaac::tui::move_cursor_right(cursor_col, /*display_width=*/0);
    CHECK(cursor_col == 0);

    cursor_col = 0;
    mwaac::tui::move_cursor_right(cursor_col, /*display_width=*/-1);
    CHECK(cursor_col == 0);
}

TEST_CASE("move_cursor_*: cursor stays in [0, display_width-1] across long mutation sequences",
          "[tui][app_handlers][mi-cursor-col-clamp]")
{
    // Mash right then left a lot of times; cursor must never exit
    // the valid range even with stale display widths.
    int cursor_col = 0;
    const int display_width = 80;
    for (int i = 0; i < 200; ++i) {
        mwaac::tui::move_cursor_right(cursor_col, display_width);
        CHECK(cursor_col >= 0);
        CHECK(cursor_col <= display_width - 1);
    }
    for (int i = 0; i < 200; ++i) {
        mwaac::tui::move_cursor_left(cursor_col);
        CHECK(cursor_col >= 0);
        CHECK(cursor_col <= display_width - 1);
    }
}
