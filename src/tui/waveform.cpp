#include "waveform.hpp"
#include <algorithm>
#include <cmath>

namespace mwaac::tui {

std::vector<std::pair<float, float>> downsample_for_display(
    std::span<const float> samples,
    int display_width)
{
    if (samples.empty() || display_width <= 0) {
        return {};
    }
    
    std::vector<std::pair<float, float>> peaks(display_width);
    
    // Samples per column
    size_t samples_per_col = samples.size() / static_cast<size_t>(display_width);
    if (samples_per_col == 0) samples_per_col = 1;
    
    for (int col = 0; col < display_width; ++col) {
        size_t start = static_cast<size_t>(col) * samples_per_col;
        size_t end = std::min(start + samples_per_col, samples.size());
        
        float min_val = 0.0f;
        float max_val = 0.0f;
        for (size_t i = start; i < end; ++i) {
            min_val = std::min(min_val, samples[i]);
            max_val = std::max(max_val, samples[i]);
        }
        
        peaks[col] = {min_val, max_val};
    }
    
    return peaks;
}

std::vector<std::string> render_waveform(
    const std::vector<std::pair<float, float>>& peaks,
    int height,
    int64_t cursor_pos,
    const std::vector<int>& markers)
{
    if (peaks.empty() || height <= 0) {
        return {};
    }
    
    // Unicode block characters for vertical bars
    static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    
    std::vector<std::string> rows(static_cast<size_t>(height), std::string());
    int half_height = height / 2;
    
    for (size_t col = 0; col < peaks.size(); ++col) {
        auto [min_val, max_val] = peaks[col];
        
        // Map [-1, 1] to [0, height]
        int min_row = static_cast<int>((1.0f - min_val) * half_height);
        int max_row = static_cast<int>((1.0f - max_val) * half_height);
        
        min_row = std::clamp(min_row, 0, height - 1);
        max_row = std::clamp(max_row, 0, height - 1);
        
        bool is_cursor = (static_cast<int64_t>(col) == cursor_pos);
        bool is_marker = std::find(markers.begin(), markers.end(), static_cast<int>(col)) != markers.end();
        
        for (int row = 0; row < height; ++row) {
            char ch = ' ';
            
            if (row >= max_row && row <= min_row) {
                // This row is within the waveform
                if (is_cursor) {
                    ch = '|';
                } else if (is_marker) {
                    ch = '|';
                } else {
                    // Use block character based on how much of this row is filled
                    ch = '|';  // Simplified - use pipe for now
                }
            } else if (is_cursor) {
                ch = ':';
            } else if (is_marker) {
                ch = '.';
            }
            
            rows[static_cast<size_t>(row)] += ch;
        }
    }
    
    return rows;
}

} // namespace mwaac::tui