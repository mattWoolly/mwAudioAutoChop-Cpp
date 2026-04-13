#pragma once
#include "core/audio_buffer.hpp"
#include "core/split_point.hpp"
#include <vector>
#include <filesystem>

namespace mwaac::tui {

struct AppState {
    AudioBuffer audio;
    std::vector<SplitPoint> split_points;
    std::filesystem::path vinyl_path;
    std::filesystem::path output_dir;
    
    int selected_marker{0};
    int64_t view_start{0};
    int64_t view_end{0};  // 0 = auto (full file)
    bool show_help{false};
};

// Run the interactive TUI application
// Returns 0 on success, non-zero on error
int run_tui(AppState& state);

} // namespace mwaac::tui