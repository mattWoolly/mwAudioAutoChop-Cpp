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
// TEST_CASE classification (Mi-MARKER-NUDGE-SEMANTIC re-cure, per
// Mi-8 audit-1 finding 2's guidance):
//
//   Mi-MARKER-NUDGE-SEMANTIC regression-guards (would FAIL with
//   either Mi-8 block-shift OR no-cure-at-all):
//     - nudge_marker_right: moves boundary right; prev grows, sel shrinks
//     - nudge_marker_right: works on blind-mode gap-of-1 output
//       (the motivating case — pre-re-cure block-shift universally
//       no-op'd interior markers on blind-mode output)
//     - nudge_marker_right: refuses on selected zero-duration collapse
//     - nudge_marker_left: moves boundary left; prev shrinks, sel grows
//     - nudge_marker_left: refuses on previous zero-duration collapse
//     - nudge_marker_*: inter-track gap preserved across sequences
//     - nudge_marker_*: durations change inversely (sum invariant)
//
//   Invariant locks / defensive documentation:
//     - nudge_marker_right: first marker (idx == 0) no-ops
//     - nudge_marker_left: first marker (idx == 0) no-ops
//     - nudge_marker_*: empty split_points / out-of-range no-op
//
// The pre-re-cure (Mi-8 block-shift) test set has been rewritten;
// the assertions there codified the wrong semantic per the user's
// Mi-MARKER-NUDGE-SEMANTIC choice (boundary-shift).
//
// Future Mi-* authors: when extending this harness, preserve the
// regression-guard / invariant-lock distinction. A test that passes
// pre-cure is documentation, not a guard.

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

// ─── nudge_marker_right (boundary-shift per Mi-MARKER-NUDGE-SEMANTIC) ──

TEST_CASE("nudge_marker_right: moves boundary right; prev grows, sel shrinks",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // The canonical boundary-shift case (matches the AskUserQuestion
    // preview). Two adjacent markers with gap-of-1 (blind-mode output
    // shape). Nudge right on marker[1]: boundary between [0] and [1]
    // moves right by 1; marker[0] grows, marker[1] shrinks; gap-of-1
    // preserved.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 299)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 200);  // grew
    CHECK(s.split_points[1].start_sample == 201);  // shrunk
    CHECK(s.split_points[1].end_sample == 299);
    // Inter-track gap preserved (end[0] + 1 == start[1]):
    CHECK(s.split_points[0].end_sample + 1 == s.split_points[1].start_sample);
}

TEST_CASE("nudge_marker_right: first marker (idx == 0) no-ops (no boundary before file start)",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // Mi-MARKER-NUDGE-SEMANTIC: selected_marker == 0 has no boundary
    // BEFORE it — the file start is fixed. Nudge no-ops.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 200);
}

TEST_CASE("nudge_marker_right: refuses when selected marker would collapse to zero duration",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // marker[1] is currently duration 1 (start==end==200). Nudge right
    // would push start to 201 > end=200 → zero/negative duration. Refuse.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 200)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[1].start_sample == 200);
    CHECK(s.split_points[1].end_sample == 200);
    // Neighbor also unchanged.
    CHECK(s.split_points[0].end_sample == 199);
}

TEST_CASE("nudge_marker_right: last allowed step (duration 2 → 1) succeeds",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // Audit-1 coverage-gap follow-up: assert the step BEFORE refusal
    // succeeds. marker[1] is duration 2 (start=200, end=201). Nudge
    // right shrinks to duration 1 (start=201, end=201) — allowed.
    // Off-by-one drift in the refusal predicate ("> end" tightened
    // to ">= end") would NOT be caught by the duration==1 refusal
    // test alone; this case catches the too-strict direction.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 201)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_right(s);
    CHECK(s.split_points[0].end_sample == 200);     // grew
    CHECK(s.split_points[1].start_sample == 201);   // shrunk
    CHECK(s.split_points[1].end_sample == 201);     // unchanged
    CHECK(s.split_points[1].duration_samples() == 1);
}

TEST_CASE("nudge_marker_right: works on blind-mode gap-of-1 output (Mi-MARKER-NUDGE-SEMANTIC regression-guard)",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // The motivating case for the Mi-MARKER-NUDGE-SEMANTIC re-cure:
    // pre-re-cure (block-shift), interior markers on blind-mode
    // gap-of-1 output universally no-op'd. Post-re-cure (boundary-shift),
    // they nudge naturally. This case asserts marker[1] in a 3-track
    // blind-mode-shape layout (end[0]=199, start[1]=200, end[1]=399,
    // start[2]=400) successfully nudges.
    auto s = build_state(/*total=*/10000,
                         {marker(0, 199), marker(200, 399), marker(400, 9999)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_right(s);
    // Boundary between marker[0] and marker[1] moved right.
    CHECK(s.split_points[0].end_sample == 200);
    CHECK(s.split_points[1].start_sample == 201);
    // Other edges of marker[1] and other markers untouched.
    CHECK(s.split_points[1].end_sample == 399);
    CHECK(s.split_points[2].start_sample == 400);
}

// ─── nudge_marker_left (symmetric) ──────────────────────────────────

TEST_CASE("nudge_marker_left: moves boundary left; prev shrinks, sel grows",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    auto s = build_state(/*total=*/1000,
                         {marker(100, 199), marker(200, 299)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 198);  // shrunk
    CHECK(s.split_points[1].start_sample == 199);  // grew
    CHECK(s.split_points[1].end_sample == 299);
    // Inter-track gap preserved (end[0] + 1 == start[1]):
    CHECK(s.split_points[0].end_sample + 1 == s.split_points[1].start_sample);
}

TEST_CASE("nudge_marker_left: first marker (idx == 0) no-ops (no boundary before file start)",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    auto s = build_state(/*total=*/1000,
                         {marker(100, 200)},
                         /*selected=*/0);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 200);
}

TEST_CASE("nudge_marker_left: refuses when previous marker would collapse to zero duration",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // marker[0] is currently duration 1 (start==end==100). Nudge left
    // would push prev.end to 99 < prev.start=100 → zero/negative duration.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 100), marker(200, 500)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].end_sample == 100);
    CHECK(s.split_points[1].start_sample == 200);
}

TEST_CASE("nudge_marker_left: last allowed step (prev duration 2 → 1) succeeds",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // Audit-1 coverage-gap follow-up: symmetric to the right-nudge
    // last-allowed-step test. marker[0] is duration 2 (start=100,
    // end=101). Nudge left on marker[1] shrinks marker[0] to
    // duration 1 (start=100, end=100) — allowed.
    auto s = build_state(/*total=*/1000,
                         {marker(100, 101), marker(200, 500)},
                         /*selected=*/1);
    mwaac::tui::nudge_marker_left(s);
    CHECK(s.split_points[0].start_sample == 100);   // unchanged
    CHECK(s.split_points[0].end_sample == 100);     // shrunk
    CHECK(s.split_points[1].start_sample == 199);   // grew
    CHECK(s.split_points[0].duration_samples() == 1);
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
    CHECK(s.split_points[0].start_sample == 100);
    CHECK(s.split_points[0].end_sample == 200);
}

// ─── Mi-NUDGE-EVIDENCE-STALENESS regression-guards ──────────────────
//
// Boundary-shift nudges clear SplitPoint::evidence on both affected
// markers. evidence is descriptive provenance from the algorithmic
// pipelines (blind_mode / reference_mode set it at construction);
// post-user-edit it no longer describes the current marker range.
// Refused nudges (idx == 0 no-op, would-collapse refusal) must leave
// evidence untouched — only successful commits clear it.

namespace {

// Helper: build a marker with non-empty evidence (so the cure has
// something to clear).
mwaac::SplitPoint marker_with_evidence(std::int64_t start, std::int64_t end) {
    mwaac::SplitPoint sp;
    sp.start_sample = start;
    sp.end_sample = end;
    sp.evidence["test_key"] = static_cast<double>(123.0);
    sp.evidence["another_key"] = std::string("provenance");
    return sp;
}

} // namespace

TEST_CASE("nudge_marker_right: clears evidence on successful nudge (Mi-NUDGE-EVIDENCE-STALENESS regression-guard)",
          "[tui][app_handlers][mi-nudge-evidence-staleness]")
{
    // Two markers each with non-empty evidence. A successful nudge
    // moves the boundary right and must clear both markers' evidence
    // (both prev and sel had a boundary moved).
    auto s = build_state(/*total=*/1000,
                         {marker_with_evidence(100, 199),
                          marker_with_evidence(200, 299)},
                         /*selected=*/1);
    REQUIRE_FALSE(s.split_points[0].evidence.empty());
    REQUIRE_FALSE(s.split_points[1].evidence.empty());
    mwaac::tui::nudge_marker_right(s);
    // Sanity: boundary actually moved (this should be a successful nudge).
    CHECK(s.split_points[0].end_sample == 200);
    CHECK(s.split_points[1].start_sample == 201);
    // Cure: both evidence maps cleared.
    CHECK(s.split_points[0].evidence.empty());
    CHECK(s.split_points[1].evidence.empty());
}

TEST_CASE("nudge_marker_left: clears evidence on successful nudge (Mi-NUDGE-EVIDENCE-STALENESS regression-guard)",
          "[tui][app_handlers][mi-nudge-evidence-staleness]")
{
    // Symmetric to the right-nudge case.
    auto s = build_state(/*total=*/1000,
                         {marker_with_evidence(100, 199),
                          marker_with_evidence(200, 299)},
                         /*selected=*/1);
    REQUIRE_FALSE(s.split_points[0].evidence.empty());
    REQUIRE_FALSE(s.split_points[1].evidence.empty());
    mwaac::tui::nudge_marker_left(s);
    // Sanity: boundary actually moved.
    CHECK(s.split_points[0].end_sample == 198);
    CHECK(s.split_points[1].start_sample == 199);
    // Cure: both evidence maps cleared.
    CHECK(s.split_points[0].evidence.empty());
    CHECK(s.split_points[1].evidence.empty());
}

TEST_CASE("nudge_marker_*: refused nudges preserve evidence (Mi-NUDGE-EVIDENCE-STALENESS)",
          "[tui][app_handlers][mi-nudge-evidence-staleness]")
{
    // idx == 0 no-op (right): no boundary before the first marker;
    // evidence must be preserved.
    {
        auto s = build_state(/*total=*/1000,
                             {marker_with_evidence(100, 200)},
                             /*selected=*/0);
        const auto original_size = s.split_points[0].evidence.size();
        REQUIRE(original_size > 0);
        mwaac::tui::nudge_marker_right(s);
        CHECK(s.split_points[0].evidence.size() == original_size);
    }
    // idx == 0 no-op (left): symmetric.
    {
        auto s = build_state(/*total=*/1000,
                             {marker_with_evidence(100, 200)},
                             /*selected=*/0);
        const auto original_size = s.split_points[0].evidence.size();
        REQUIRE(original_size > 0);
        mwaac::tui::nudge_marker_left(s);
        CHECK(s.split_points[0].evidence.size() == original_size);
    }
    // Would-collapse refusal (right): sel duration 1 cannot shrink to 0.
    {
        auto s = build_state(/*total=*/1000,
                             {marker_with_evidence(100, 199),
                              marker_with_evidence(200, 200)},
                             /*selected=*/1);
        const auto prev_size = s.split_points[0].evidence.size();
        const auto sel_size = s.split_points[1].evidence.size();
        REQUIRE(prev_size > 0);
        REQUIRE(sel_size > 0);
        mwaac::tui::nudge_marker_right(s);
        CHECK(s.split_points[0].evidence.size() == prev_size);
        CHECK(s.split_points[1].evidence.size() == sel_size);
    }
    // Would-collapse refusal (left): prev duration 1 cannot shrink to 0.
    {
        auto s = build_state(/*total=*/1000,
                             {marker_with_evidence(100, 100),
                              marker_with_evidence(200, 500)},
                             /*selected=*/1);
        const auto prev_size = s.split_points[0].evidence.size();
        const auto sel_size = s.split_points[1].evidence.size();
        REQUIRE(prev_size > 0);
        REQUIRE(sel_size > 0);
        mwaac::tui::nudge_marker_left(s);
        CHECK(s.split_points[0].evidence.size() == prev_size);
        CHECK(s.split_points[1].evidence.size() == sel_size);
    }
}

// ─── boundary-shift invariants ──────────────────────────────────────

TEST_CASE("nudge_marker_*: inter-track gap preserved across boundary-shift sequences",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // Boundary-shift cure shifts BOTH prev.end and sel.start by the
    // same delta in lockstep. The inter-track gap (sel.start - prev.end)
    // is invariant. Verify across a long sequence of nudges.
    auto s = build_state(/*total=*/10000,
                         {marker(100, 199), marker(200, 5000)},
                         /*selected=*/1);
    const std::int64_t initial_gap =
        s.split_points[1].start_sample - s.split_points[0].end_sample;
    for (int i = 0; i < 100; ++i) {
        mwaac::tui::nudge_marker_right(s);
        CHECK(s.split_points[1].start_sample - s.split_points[0].end_sample == initial_gap);
    }
    for (int i = 0; i < 200; ++i) {
        mwaac::tui::nudge_marker_left(s);
        CHECK(s.split_points[1].start_sample - s.split_points[0].end_sample == initial_gap);
    }
}

TEST_CASE("nudge_marker_*: durations change inversely across boundary-shift",
          "[tui][app_handlers][mi-8][mi-marker-nudge-semantic]")
{
    // Per the boundary-shift semantic, when the boundary moves right
    // by N: prev gains N samples, sel loses N samples. Sum of adjacent
    // durations is invariant. Verify across a sequence.
    auto s = build_state(/*total=*/10000,
                         {marker(100, 199), marker(200, 500)},
                         /*selected=*/1);
    const std::int64_t initial_sum =
        s.split_points[0].duration_samples() + s.split_points[1].duration_samples();
    for (int i = 0; i < 100; ++i) {
        mwaac::tui::nudge_marker_right(s);
        const std::int64_t cur_sum =
            s.split_points[0].duration_samples() + s.split_points[1].duration_samples();
        CHECK(cur_sum == initial_sum);
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

// ─── Mi-VIEW-ZOOM-BOUNDARY-SHIFT ───────────────────────────────────
//
// Pre-cure: commit_normalized_view used a clamp-only policy that
// pinned the offending edge without shifting the opposite edge.
// `zoom_out` near `[0, total]` boundaries produced a view narrower
// than the requested range. Post-cure: shift-shift policy preserves
// the requested range whenever it fits in [0, total]; falls back to
// the maximal view [0, total] only when requested range exceeds total.

TEST_CASE("zoom_out near left boundary: range preserved by shift-shift (Mi-VIEW-ZOOM-BOUNDARY-SHIFT regression-guard)",
          "[tui][app_handlers][mi-view-zoom-boundary-shift]")
{
    // Pre-cure: cur=[0, 100), total=100000. zoom_out targets range=200
    // about center=50. proposed=[-50, 150). Clamp-only policy: start
    // clamped to 0, end UNCHANGED at 150 → effective range 150 (NOT 200).
    // Post-cure: shift-shift policy → start clamped to 0, end shifted
    // to 200 → effective range 200 (preserved).
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/0, /*view_end=*/100);
    mwaac::tui::zoom_out(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 200);
    CHECK(s.view_end - s.view_start == 200);  // range preserved
}

TEST_CASE("zoom_out near right boundary: range preserved by shift-shift (Mi-VIEW-ZOOM-BOUNDARY-SHIFT regression-guard)",
          "[tui][app_handlers][mi-view-zoom-boundary-shift]")
{
    // Symmetric to the left-boundary case: cur=[99900, 100000),
    // total=100000. zoom_out targets range=200 about center=99950.
    // proposed=[99850, 100050). Clamp-only policy: end clamped to
    // 100000, start UNCHANGED at 99850 → effective range 150.
    // Post-cure: end clamped to 100000, start shifted to 99800 →
    // effective range 200.
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/99900, /*view_end=*/100000);
    mwaac::tui::zoom_out(s);
    CHECK(s.view_end == 100000);
    CHECK(s.view_start == 99800);
    CHECK(s.view_end - s.view_start == 200);  // range preserved
}

TEST_CASE("zoom_out: requested range > total collapses to [0, total] fallback",
          "[tui][app_handlers][mi-view-zoom-boundary-shift]")
{
    // When the requested range exceeds total_samples, no shift-shift
    // policy can preserve it. Fallback: collapse to the maximal view
    // [0, total]. Cure: cur=[40, 60), total=100. zoom_out targets
    // range=40 (capped at total since 20*2=40 < 100), about center=50.
    // proposed=[30, 70). Both edges in bounds — straightforward zoom.
    // Stronger test: cur=[0, 100), total=100. zoom_out targets
    // range=200 capped at 100 → straight cap to total. Verify the
    // fallback produces [0, 100).
    auto s = build_view_state(/*total=*/100,
                              /*view_start=*/0, /*view_end=*/100);
    mwaac::tui::zoom_out(s);
    CHECK(s.view_start == 0);
    CHECK(s.view_end == 100);
}

TEST_CASE("commit_normalized_view (via zoom_out): shift-shift preserves range when fitting",
          "[tui][app_handlers][mi-view-zoom-boundary-shift]")
{
    // No-regression: zoom_out from a comfortable middle view should
    // produce the same result as Mi-9's existing behavior (no shift
    // triggered because both edges land in bounds).
    auto s = build_view_state(/*total=*/100000,
                              /*view_start=*/40000, /*view_end=*/50000);
    mwaac::tui::zoom_out(s);
    // Range 10000 doubled to 20000 about center 45000 → [35000, 55000).
    CHECK(s.view_start == 35000);
    CHECK(s.view_end == 55000);
}
