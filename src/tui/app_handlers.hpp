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

// Move the BOUNDARY before the selected marker (the boundary between
// markers[selected-1] and markers[selected]) one sample to the right.
// Adjusts both edges of that boundary in lockstep:
//   - markers[selected-1].end_sample += 1 (grows marker[selected-1])
//   - markers[selected].start_sample += 1 (shrinks marker[selected])
// Inter-track gap is preserved; adjacent track durations change
// inversely.
//
// Mi-MARKER-NUDGE-SEMANTIC (cure 2026-05-20): boundary-shift semantic
// chosen over the original block-shift (Mi-8) per user judgment. The
// block-shift semantic preserved per-marker duration but combined with
// blind_mode's algorithmic-output gap-of-exactly-1 made interior
// markers in the dominant blind-mode workflow universally unable to
// nudge.
//
// No-ops when:
//   - split_points is empty or selected_marker is out of range
//   - selected_marker == 0 (no boundary before the first marker — its
//     start_sample is at the file start and not editable via nudge)
//   - moving the boundary right would collapse markers[selected] to
//     zero or negative duration
void nudge_marker_right(AppState& state);

// Mirror of nudge_marker_right. Moves the boundary BEFORE the selected
// marker one sample to the left:
//   - markers[selected-1].end_sample -= 1 (shrinks marker[selected-1])
//   - markers[selected].start_sample -= 1 (grows marker[selected])
// No-ops when:
//   - split_points is empty or selected_marker is out of range
//   - selected_marker == 0 (no boundary before first marker)
//   - moving the boundary left would collapse markers[selected-1] to
//     zero or negative duration
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

// Cursor-position mutators (Mi-CURSOR-COL-CLAMP). The cursor is a
// column index into the rendered waveform display, bounded by
// [0, display_width - 1]. `cursor_col` lives in `run_tui`'s local
// scope (not in AppState), so these mutators take it by reference
// rather than via AppState. `display_width` is queried from
// `Terminal::Size().dimx - 2` at handler call time in `app.cpp`.
//
// Pre-cure (Mi-CURSOR-COL-CLAMP defect): ArrowRight handler did
// `cursor_col++` with no upper bound; ArrowLeft handler at app.cpp:255
// correctly clamped via `std::max(0, cursor_col - 1)`. The asymmetry
// is the bug.
//
// Post-cure: symmetric extraction — both handlers reduce to one-line
// dispatches. Mutators no-op (refuse to move past the bound) rather
// than saturating.

// Decrement cursor_col, clamped at 0.
void move_cursor_left(int& cursor_col);

// Increment cursor_col, clamped at display_width - 1.
// Degenerate display_width <= 0: cursor_col stays at 0 (or wherever
// it was if non-zero — the handler caller is responsible for ensuring
// cursor_col is already valid).
void move_cursor_right(int& cursor_col, int display_width);

} // namespace mwaac::tui
