#include "app_handlers.hpp"

#include <algorithm>
#include <cstdint>

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

} // namespace mwaac::tui
