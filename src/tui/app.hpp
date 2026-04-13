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

// Run the interactive TUI application
// Returns 0 on success, non-zero on error
int run_tui(AppState& state);

} // namespace mwaac::tui