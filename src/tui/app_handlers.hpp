#pragma once
//
// Mi-8 / Tier 7: TUI state-mutator harness. Extracts the pure-state
// transformation logic out of the FTXUI event-handler closures in
// `src/tui/app.cpp` so it can be tested in isolation. The event
// handlers in `app.cpp` become one-line dispatches:
//
//   if (event == Event::Character('+')) {
//       nudge_marker_right(state);
//       return true;
//   }
//
// Tests in `tests/test_app_handlers.cpp` construct a synthetic
// `AppState` and invoke the mutators directly, asserting on the
// post-state. No FTXUI dependency in the test path.
//
// Scope. This header exposes ONLY the state-mutator functions that
// Mi-8 (marker nudge) needs. As Mi-9 (view bounds) and any future TUI
// items dispatch, they extend this header with their own mutators
// (zoom_in/out, pan_to_start/end, etc.). Helper functions that have
// no test target (e.g. pure cursor positioning) stay inside app.cpp's
// anonymous namespace.
//
// Invariant maintained by `nudge_marker_*` (per BACKLOG Mi-8):
//   For every SplitPoint: 0 <= start_sample <= end_sample <= total_samples - 1.
// And the cross-marker no-gap invariant from blind_mode / reference_mode
// pipelines:
//   For adjacent SplitPoints i, i+1: markers[i].end_sample < markers[i+1].start_sample.
// The nudge mutators clamp against both — the marker no-ops rather
// than violating either invariant.

#include "app.hpp"  // for AppState

namespace mwaac::tui {

// Shift the selected marker (a SplitPoint representing a track range)
// one sample to the right. No-ops when:
//   - split_points is empty or selected_marker is out of range
//   - moving right would push end_sample past total_samples - 1
//   - moving right would push end_sample to or past the next marker's
//     start_sample (maintaining the inter-track gap)
// The marker shifts as a block: both start_sample and end_sample
// move by the same delta, so within-marker duration is preserved.
void nudge_marker_right(AppState& state);

// Mirror of nudge_marker_right. No-ops when:
//   - split_points is empty or selected_marker is out of range
//   - moving left would push start_sample below 0
//   - moving left would push start_sample to or past the previous
//     marker's end_sample (maintaining the inter-track gap)
void nudge_marker_left(AppState& state);

// View-bounds mutators (Mi-9). Each call ends with a normalization
// pass that enforces INV-VIEW-NON-INVERTED:
//   0 <= view_start < view_end <= total_samples
// where total_samples == state.audio.samples.size(). The `view_end == 0`
// sentinel (meaning "auto-stretch to file end" in the Renderer) is
// resolved BEFORE any normalization runs, then re-applied if no valid
// view can be constructed (e.g. empty audio). The mutators no-op on
// empty audio (no valid view satisfies the strict-less-than).

// Zoom in: halve the current view range about its center, floor at
// 1000 samples (minimum useful zoom). No-op on empty audio.
void zoom_in(AppState& state);

// Zoom out: double the current view range about its center, capped
// at total_samples. No-op on empty audio.
void zoom_out(AppState& state);

// Pan to start: jump view to [0, current_range) without changing
// zoom level. Caps current_range at total_samples.
void pan_to_start(AppState& state);

// Pan to end: jump view to [total - current_range, total) without
// changing zoom level. Caps current_range at total_samples.
void pan_to_end(AppState& state);

} // namespace mwaac::tui
