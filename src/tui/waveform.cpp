#include "waveform.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

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

// Helper to find marker at a given column position
const MarkerInfo* find_marker_at_column(const std::vector<MarkerInfo>& markers, int col) {
    for (const auto& m : markers) {
        if (m.column == col) return &m;
    }
    return nullptr;
}

std::vector<std::string> render_waveform(
    const std::vector<std::pair<float, float>>& peaks,
    int height,
    int64_t cursor_pos,
    const std::vector<MarkerInfo>& markers)
{
    if (peaks.empty() || height <= 0) {
        return {};
    }
    
    std::vector<std::string> rows(static_cast<size_t>(height + 1), std::string());
    int half_height = height / 2;
    int waveform_height = height - 1;  // Reserve one row for track numbers
    
    for (size_t col = 0; col < peaks.size(); ++col) {
        auto [min_val, max_val] = peaks[col];
        
        // Map [-1, 1] to [0, waveform_height]
        int min_row = static_cast<int>((1.0f - min_val) * half_height);
        int max_row = static_cast<int>((1.0f - max_val) * half_height);
        
        min_row = std::clamp(min_row, 0, waveform_height - 1);
        max_row = std::clamp(max_row, 0, waveform_height - 1);
        
        bool is_cursor = (static_cast<int64_t>(col) == cursor_pos);
        
        // Check for marker at this column
        const MarkerInfo* marker_info = find_marker_at_column(markers, static_cast<int>(col));
        bool is_marker = marker_info != nullptr;
        bool is_selected = marker_info && marker_info->selected;
        
        for (int row = 0; row < waveform_height; ++row) {
            char ch = ' ';
            
            if (row >= max_row && row <= min_row) {
                // This row is within the waveform
                if (is_selected) {
                    ch = '#';  // Highlighted marker
                } else if (is_marker) {
                    ch = '|';
                } else if (is_cursor) {
                    ch = '|';
                } else {
                    ch = '|';  // Simplified waveform
                }
            } else if (is_selected) {
                ch = '#';
            } else if (is_marker) {
                ch = '*';
            } else if (is_cursor) {
                ch = ':';
            }
            
            rows[static_cast<size_t>(row)] += ch;
        }
        
        // Track number row (last row)
        if (marker_info && marker_info->track_number > 0) {
            std::ostringstream oss;
            oss << marker_info->track_number;
            std::string num_str = oss.str();
            // Center the number in the column
            if (col == 0) {
                rows[static_cast<size_t>(waveform_height)] += num_str.substr(0, 1);
            } else if (num_str.length() > 0) {
                rows[static_cast<size_t>(waveform_height)] += num_str.substr(0, 1);
            } else {
                rows[static_cast<size_t>(waveform_height)] += " ";
            }
        } else {
            rows[static_cast<size_t>(waveform_height)] += " ";
        }
    }
    
    return rows;
}

} // namespace mwaac::tui