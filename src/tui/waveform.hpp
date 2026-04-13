#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <span>

namespace mwaac::tui {

// Marker rendering info
struct MarkerInfo {
    int column;          // Column position on waveform
    int track_number;   // 1-based track number
    bool selected;      // Is this the selected marker
};

// Downsample audio for display
// Returns min/max pairs per column for waveform drawing
std::vector<std::pair<float, float>> downsample_for_display(
    std::span<const float> samples,
    int display_width
);

// Render waveform as vector of strings (one per row)
// Uses Unicode block characters: ▁▂▃▄▅▆▇█
std::vector<std::string> render_waveform(
    const std::vector<std::pair<float, float>>& peaks,
    int height,
    int64_t cursor_pos = -1,              // Column position of cursor (-1 = none)
    const std::vector<MarkerInfo>& markers = {} // Marker positions with track numbers
);

} // namespace mwaac::tui