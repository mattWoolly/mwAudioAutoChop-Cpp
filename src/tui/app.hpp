#pragma once
#include "core/audio_buffer.hpp"
#include "core/split_point.hpp"
#include <vector>
#include <filesystem>
#include <string>

namespace mwaac::tui {

// Export status for progress display
struct ExportStatus {
    bool in_progress{false};
    int current_track{0};
    int total_tracks{0};
    bool success{false};
    std::string message;
};

struct AppState {
    AudioBuffer audio;
    std::vector<SplitPoint> split_points;
    std::filesystem::path vinyl_path;
    std::filesystem::path output_dir;
    
    int selected_marker{0};
    int64_t view_start{0};
    int64_t view_end{0};  // 0 = auto (full file)
    bool show_help{false};
    
    // Export state
    ExportStatus export_status;
};

// Run the interactive TUI application.
//
// Returns 0 on any normal exit from the event loop — user-initiated
// quit ('q'/'Q'), Ctrl-C signal handled by the FTXUI screen loop, or
// terminal disconnect. There is no non-zero return path from the
// loop. FTXUI's `ScreenInteractive::Fullscreen()` and `Loop()` are
// best-effort with no throwing failure modes (verified against the
// vendored FTXUI source), so initialization failure manifests as a
// degraded loop rather than a non-zero return; the BACKLOG Mi-10
// invariant "non-zero only on initialization failure" is therefore
// vacuously satisfied. If callers need to distinguish "TUI ran" from
// "TUI couldn't initialize," that signal must come from elsewhere
// (e.g. checking terminal capabilities before the call).
int run_tui(AppState& state);

} // namespace mwaac::tui