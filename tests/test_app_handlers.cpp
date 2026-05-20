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
