#include "app_handlers.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace mwaac::tui {

namespace {

// Helper: is selected_marker a valid index into split_points?
bool selected_marker_in_range(const AppState& state) {
    return !state.split_points.empty() &&
           state.selected_marker >= 0 &&
           state.selected_marker < static_cast<int>(state.split_points.size());
}

} // namespace

void nudge_marker_right(AppState& state) {
    if (!selected_marker_in_range(state)) {
        return;
    }

    const std::size_t idx = static_cast<std::size_t>(state.selected_marker);
    auto& mark = state.split_points[idx];

    // Global upper bound: end_sample stays within the audio buffer.
    // If the AppState carries no audio (empty samples), total_samples
    // is 0 and upper_bound is -1 — the cure below correctly no-ops.
    const std::int64_t total_samples =
        static_cast<std::int64_t>(state.audio.samples.size());
    std::int64_t upper_bound = total_samples - 1;

    // Sibling upper bound: marker[i].end_sample must stay strictly less
    // than marker[i+1].start_sample (the inter-track gap that the
    // blind_mode / reference_mode pipelines maintain — see
    // src/modes/blind_mode.cpp's post-loop end_sample fill-in:
    // `split_points[i].end_sample = split_points[i + 1].start_sample - 1`).
    if (idx + 1 < state.split_points.size()) {
        const std::int64_t next_start = state.split_points[idx + 1].start_sample;
        upper_bound = std::min(upper_bound, next_start - 1);
    }

    // No-op if the nudge would violate either bound. Refusing the
    // nudge (rather than partially shifting or saturating at the
    // bound) keeps the marker's duration invariant — a half-shift
    // would change duration_samples(), which the user did not request.
    if (mark.end_sample + 1 > upper_bound) {
        return;
    }

    mark.start_sample += 1;
    mark.end_sample += 1;
}

void nudge_marker_left(AppState& state) {
    if (!selected_marker_in_range(state)) {
        return;
    }

    const std::size_t idx = static_cast<std::size_t>(state.selected_marker);
    auto& mark = state.split_points[idx];

    // Global lower bound.
    std::int64_t lower_bound = 0;

    // Sibling lower bound: marker[i].start_sample must stay strictly
    // greater than marker[i-1].end_sample.
    if (idx > 0) {
        const std::int64_t prev_end = state.split_points[idx - 1].end_sample;
        lower_bound = std::max(lower_bound, prev_end + 1);
    }

    if (mark.start_sample - 1 < lower_bound) {
        return;
    }

    mark.start_sample -= 1;
    mark.end_sample -= 1;
}

// ─── Mi-9 view-bounds mutators ─────────────────────────────────────

namespace {

// Mi-9 — minimum useful zoom-in floor. Below this, the view is so
// narrow it can't fit a meaningful waveform — keep the original
// pre-cure floor so existing user behavior is preserved.
constexpr std::int64_t MIN_VIEW_RANGE = 1000;

// Resolve the `view_end == 0` sentinel ("auto-stretch to file end")
// into an explicit sample range. Returns {view_start, view_end} as
// they would appear in the Renderer.
std::pair<std::int64_t, std::int64_t> resolve_view_range(
    const AppState& state, std::int64_t total)
{
    const std::int64_t cur_start = state.view_start;
    const std::int64_t cur_end = state.view_end > 0 ? state.view_end : total;
    return {cur_start, cur_end};
}

// Mi-9 normalization helper (Mi-VIEW-ZOOM-BOUNDARY-SHIFT extended):
// enforce INV-VIEW-NON-INVERTED (`0 <= view_start < view_end <=
// total_samples`) using a **shift-shift** policy — when one edge
// of the proposed view is out of bounds, shift the opposite edge
// by the same delta to preserve the requested range. Falls back
// to a clamp-clamp policy only when the requested range exceeds
// total_samples (in which case the view collapses to `[0, total]`).
//
// Pre-Mi-VIEW-ZOOM-BOUNDARY-SHIFT: this helper used a clamp-only
// policy that pinned the offending edge without shifting the
// opposite edge, producing a narrower-than-requested view near
// the audio boundaries (e.g. zoom_out from view=[0, 100) on
// total=100000 produced view=[0, 150) instead of view=[0, 200)
// preserving the 200-sample zoom-out request).
//
// On empty audio (total <= 0), no valid view exists; the helper
// signals this by returning false and the caller must no-op
// rather than commit a degenerate state. On success, writes the
// normalized values to state and returns true.
bool commit_normalized_view(AppState& state,
                            std::int64_t proposed_start,
                            std::int64_t proposed_end,
                            std::int64_t total)
{
    if (total <= 0) {
        return false;  // no valid view exists
    }

    // Mi-VIEW-ZOOM-BOUNDARY-SHIFT: shift-shift normalization.
    // If `proposed_start < 0`, shift `proposed_end` right by the
    // overflow amount. If `proposed_end > total`, shift
    // `proposed_start` left by the overflow amount. Order matters
    // (left-then-right) so that when both edges are out of bounds
    // — i.e. requested range >= total — the second shift triggers
    // the post-shift clamp to collapse to [0, total] safely.
    if (proposed_start < 0) {
        proposed_end += -proposed_start;
        proposed_start = 0;
    }
    if (proposed_end > total) {
        proposed_start -= (proposed_end - total);
        proposed_end = total;
    }

    // Post-shift clamp. When the requested range exceeds total, the
    // shifted edge will overflow the opposite bound (e.g. proposed
    // range = total + 10: after both shifts, start = -10, end = total;
    // start gets clamped to 0). The clamp order (end then start) is
    // retained from Mi-9 — clamping start first against `end` could
    // pin it at total - 1 even when caller wanted a tighter view.
    proposed_end = std::clamp<std::int64_t>(proposed_end, 1, total);
    proposed_start = std::clamp<std::int64_t>(proposed_start, 0, proposed_end - 1);

    // Strict-less-than `start < end` invariant: post-clamp end >= 1,
    // start <= end - 1, so start < end. Commit.
    state.view_start = proposed_start;
    state.view_end = proposed_end;
    return true;
}

} // namespace

void zoom_in(AppState& state) {
    const std::int64_t total = static_cast<std::int64_t>(state.audio.samples.size());
    if (total <= 0) {
        return;
    }

    const auto [cur_start, cur_end] = resolve_view_range(state, total);
    const std::int64_t cur_range = cur_end - cur_start;
    if (cur_range <= MIN_VIEW_RANGE) {
        // Already at or below the minimum useful zoom — refuse rather
        // than collapse the view to a single sample.
        return;
    }

    const std::int64_t center = cur_start + cur_range / 2;
    const std::int64_t new_range = std::max<std::int64_t>(cur_range / 2, MIN_VIEW_RANGE);
    const std::int64_t new_start = center - new_range / 2;
    const std::int64_t new_end = new_start + new_range;

    commit_normalized_view(state, new_start, new_end, total);
}

void zoom_out(AppState& state) {
    const std::int64_t total = static_cast<std::int64_t>(state.audio.samples.size());
    if (total <= 0) {
        return;
    }

    const auto [cur_start, cur_end] = resolve_view_range(state, total);
    const std::int64_t cur_range = cur_end - cur_start;

    const std::int64_t center = cur_start + cur_range / 2;
    const std::int64_t new_range = std::min<std::int64_t>(cur_range * 2, total);
    const std::int64_t new_start = center - new_range / 2;
    const std::int64_t new_end = new_start + new_range;

    commit_normalized_view(state, new_start, new_end, total);
}

void pan_to_start(AppState& state) {
    const std::int64_t total = static_cast<std::int64_t>(state.audio.samples.size());
    if (total <= 0) {
        return;
    }

    const auto [cur_start, cur_end] = resolve_view_range(state, total);
    const std::int64_t cur_range = std::min<std::int64_t>(cur_end - cur_start, total);

    commit_normalized_view(state, 0, cur_range, total);
}

void pan_to_end(AppState& state) {
    const std::int64_t total = static_cast<std::int64_t>(state.audio.samples.size());
    if (total <= 0) {
        return;
    }

    const auto [cur_start, cur_end] = resolve_view_range(state, total);
    const std::int64_t cur_range = std::min<std::int64_t>(cur_end - cur_start, total);

    commit_normalized_view(state, total - cur_range, total, total);
}

// ─── Mi-CURSOR-COL-CLAMP cursor mutators ───────────────────────────

void move_cursor_left(int& cursor_col) {
    // Pre-cure already clamped at 0; preserved verbatim.
    cursor_col = std::max(0, cursor_col - 1);
}

void move_cursor_right(int& cursor_col, int display_width) {
    // Mi-CURSOR-COL-CLAMP: upper bound at display_width - 1.
    // The std::max(0, ...) guards against display_width <= 0 (degenerate
    // terminal sizes occasionally observed in CI / tiny terminals);
    // without it, display_width - 1 could be negative and the
    // std::min would clamp cursor_col to a negative value.
    const int upper = std::max(0, display_width - 1);
    cursor_col = std::min(cursor_col + 1, upper);
}

} // namespace mwaac::tui
