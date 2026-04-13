#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <span>

namespace mwaac::tui {

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
    int64_t cursor_pos = -1,           // Column position of cursor (-1 = none)
    const std::vector<int>& markers = {} // Column positions of markers
);

} // namespace mwaac::tui