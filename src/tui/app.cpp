#include "app.hpp"
#include "waveform.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include "core/audio_file.hpp"

namespace mwaac::tui {

using namespace ftxui;

// Forward declaration for export
void export_tracks(AppState& state);

int run_tui(AppState& state) {
    auto screen = ScreenInteractive::Fullscreen();
    
    // Cursor follows selected marker
    int cursor_col = 0;
    if (!state.split_points.empty() && state.selected_marker >= 0 && 
        state.selected_marker < static_cast<int>(state.split_points.size())) {
        int width = Terminal::Size().dimx - 2;
        if (state.audio.samples.size() > 0) {
            cursor_col = static_cast<int>((state.split_points[state.selected_marker].start_sample * width) / state.audio.samples.size());
        }
    }
    
    bool quit = false;
    
    auto component = Renderer([&] {
        int width = Terminal::Size().dimx - 2;
        int height = 20;  // Waveform height
        int64_t total_samples = state.audio.samples.size();
        
        // Downsample audio for display (respect view boundaries)
        int64_t view_start = state.view_start;
        int64_t view_end = state.view_end > 0 ? state.view_end : total_samples;
        
        std::span<const float> view_samples(state.audio.samples.data() + view_start, 
            static_cast<size_t>(std::min(view_end - view_start, total_samples)));
        auto peaks = downsample_for_display(view_samples, width);
        
        // Build marker info with track numbers and selection state
        std::vector<MarkerInfo> markers;
        for (size_t i = 0; i < state.split_points.size(); ++i) {
            if (total_samples > 0 && state.split_points[i].start_sample >= view_start) {
                int64_t rel_pos = state.split_points[i].start_sample - view_start;
                int col = static_cast<int>((rel_pos * width) / (view_end - view_start));
                if (col >= 0 && col < width) {
                    MarkerInfo mi;
                    mi.column = col;
                    mi.track_number = static_cast<int>(i + 1);
                    mi.selected = (static_cast<int>(i) == state.selected_marker);
                    markers.push_back(mi);
                }
            }
        }
        
        // Render waveform
        auto wave_lines = render_waveform(peaks, height, cursor_col, markers);
        
        // Build waveform element with colors
        Elements wave_rows;
        for (size_t i = 0; i < wave_lines.size(); ++i) {
            // Track number row gets different color
            if (i == wave_lines.size() - 1) {
                wave_rows.push_back(text(wave_lines[i]) | color(Color::Yellow) | bold);
            } else {
                wave_rows.push_back(text(wave_lines[i]) | color(Color::Cyan));
            }
        }
        
        // Status line
        std::string status = "Track " + std::to_string(state.selected_marker + 1) + 
                            " of " + std::to_string(state.split_points.size());
        
        // View position info
        std::ostringstream view_info;
        view_info << "Samples: " << view_start << "-" << view_end;
        
        // Export status
        std::string export_msg;
        if (state.export_status.in_progress) {
            export_msg = "Exporting track " + std::to_string(state.export_status.current_track) + 
                        "/" + std::to_string(state.export_status.total_tracks) + "...";
        } else if (state.export_status.success) {
            export_msg = state.export_status.message;
        } else if (!state.export_status.message.empty()) {
            export_msg = "Error: " + state.export_status.message;
        }
        
        // Main content
        Element main_content = vbox({
            text("mwAudioAutoChop") | bold | center,
            separator(),
            vbox(wave_rows) | border,
            separator(),
            hbox({
                text(status),
                filler(),
                text(view_info.str())
            }),
            separator(),
            hbox({
                text(state.vinyl_path.filename().string()),
                filler(),
                text(export_msg) | color(state.export_status.success ? Color::Green : Color::Red)
            })
        });
        
        // Help overlay using Maybe (AAC-CPP-024)
        if (state.show_help) {
            Element help_overlay = window(
                text("Keyboard Shortcuts") | bold | center,
                vbox({
                    text("Navigation:"),
                    text("  Tab          - Next chop point"),
                    text("  Shift+Tab/P  - Previous chop point"),
                    text("  Up/Down      - Zoom in/out"),
                    text("  Home/End     - Pan to start/end of file"),
                    text(""),
                    text("Marker Adjustment:"),
                    text("  +/=/]        - Move marker right 1 sample"),
                    text("  -/_/[        - Move marker left 1 sample"),
                    text(""),
                    text("Other:"),
                    text("  H            - Toggle this help"),
                    text("  Enter        - Export all tracks"),
                    text("  Q            - Quit"),
                    separator(),
                    text("Press H to close") | dim | center
                })
            ) | center;
            
            // Wrap in a container
            return vbox({
                main_content,
                help_overlay
            });
        }
        
        return main_content;
    });
    
    component = CatchEvent(component, [&](Event event) {
        // Handle export progress while exporting
        if (state.export_status.in_progress) {
            return true;  // Block other input during export
        }
        
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            quit = true;
            screen.Exit();
            return true;
        }
        
        // AAC-CPP-023: Keyboard Navigation
        // Tab = Next marker, Shift+Tab = Previous marker
        if (event == Event::Tab) {
            if (!state.split_points.empty()) {
                state.selected_marker = (state.selected_marker + 1) % static_cast<int>(state.split_points.size());
            }
            return true;
        }
        // Shift+Tab = Previous marker (backtab)
        if (event == Event::TabReverse) {
            if (!state.split_points.empty()) {
                state.selected_marker = state.selected_marker > 0 
                    ? state.selected_marker - 1 
                    : static_cast<int>(state.split_points.size()) - 1;
            }
            return true;
        }
        // Also support 'p' for previous marker (easier than Shift+Tab in some terminals)
        if (event == Event::Character('p') || event == Event::Character('P')) {
            if (!state.split_points.empty()) {
                state.selected_marker = state.selected_marker > 0 
                    ? state.selected_marker - 1 
                    : static_cast<int>(state.split_points.size()) - 1;
            }
            return true;
        }
        
        // Marker fine adjustment
        if (event == Event::Character('+') || event == Event::Character('=') || event == Event::Character(']')) {
            if (!state.split_points.empty() && state.selected_marker >= 0 && 
                state.selected_marker < static_cast<int>(state.split_points.size())) {
                state.split_points[state.selected_marker].start_sample += 1;
                state.split_points[state.selected_marker].end_sample += 1;
            }
            return true;
        }
        if (event == Event::Character('-') || event == Event::Character('_') || event == Event::Character('[')) {
            if (!state.split_points.empty() && state.selected_marker >= 0 && 
                state.selected_marker < static_cast<int>(state.split_points.size())) {
                if (state.split_points[state.selected_marker].start_sample > 0) {
                    state.split_points[state.selected_marker].start_sample -= 1;
                    state.split_points[state.selected_marker].end_sample -= 1;
                }
            }
            return true;
        }
        
        // Zoom in/out
        if (event == Event::ArrowUp) {
            int64_t current_range = state.view_end - state.view_start;
            int64_t center = state.view_start + current_range / 2;
            int64_t new_range = std::max<int64_t>(current_range / 2, 1000);
            state.view_start = std::max<int64_t>(0, center - new_range / 2);
            state.view_end = state.view_start + new_range;
            return true;
        }
        if (event == Event::ArrowDown) {
            int64_t current_range = state.view_end - state.view_start;
            int64_t total_samples = state.audio.samples.size();
            int64_t center = state.view_start + current_range / 2;
            int64_t new_range = std::min<int64_t>(current_range * 2, total_samples);
            state.view_start = std::max<int64_t>(0, center - new_range / 2);
            state.view_end = std::min(state.view_start + new_range, total_samples);
            return true;
        }
        
        // Jump to start/end (pan view, maintain zoom level)
        if (event == Event::Home) {
            // Pan to start of file, maintain current zoom level
            int64_t current_range = state.view_end - state.view_start;
            state.view_start = 0;
            state.view_end = std::min(current_range, static_cast<int64_t>(state.audio.samples.size()));
            return true;
        }
        if (event == Event::End) {
            // Pan to end of file, maintain current zoom level
            int64_t total = state.audio.samples.size();
            int64_t current_range = state.view_end - state.view_start;
            state.view_end = total;
            state.view_start = std::max<int64_t>(0, total - current_range);
            return true;
        }
        
        // Help toggle
        if (event == Event::Character('h') || event == Event::Character('H')) {
            state.show_help = !state.show_help;
            return true;
        }
        
        // AAC-CPP-025: Export
        if (event == Event::Return) {
            // Start export in background (for simplicity, do it synchronously here)
            export_tracks(state);
            return true;
        }
        
        // Arrow keys for cursor movement (legacy support)
        if (event == Event::ArrowLeft) {
            cursor_col = std::max(0, cursor_col - 1);
            return true;
        }
        if (event == Event::ArrowRight) {
            cursor_col++;
            return true;
        }
        
        return false;
    });
    
    screen.Loop(component);
    
    return quit ? 0 : 1;
}

// AAC-CPP-025: Export functionality
void export_tracks(AppState& state) {
    if (state.split_points.empty()) {
        state.export_status.success = false;
        state.export_status.message = "No split points to export";
        return;
    }
    
    if (state.output_dir.empty()) {
        state.export_status.success = false;
        state.export_status.message = "No output directory specified";
        return;
    }
    
    // Open source file
    auto source_result = mwaac::AudioFile::open(state.vinyl_path);
    if (!source_result.has_value()) {
        state.export_status.success = false;
        state.export_status.message = "Failed to open source file";
        return;
    }
    mwaac::AudioFile& source = source_result.value();
    
    // Ensure output directory exists
    std::filesystem::create_directories(state.output_dir);
    
    // Export each track
    state.export_status.in_progress = true;
    state.export_status.total_tracks = static_cast<int>(state.split_points.size());
    state.export_status.success = false;
    state.export_status.message = "";
    
    for (size_t i = 0; i < state.split_points.size(); ++i) {
        const auto& sp = state.split_points[i];
        
        // Determine output filename
        std::ostringstream filename;
        filename << "track_" << (i + 1) << ".wav";
        std::filesystem::path output_path = state.output_dir / filename.str();
        
        // Write track
        auto result = mwaac::write_track(source, output_path, sp.start_sample, sp.end_sample);
        
        state.export_status.current_track = static_cast<int>(i + 1);
        
        if (!result.has_value()) {
            state.export_status.in_progress = false;
            state.export_status.message = "Failed to write track " + std::to_string(i + 1);
            return;
        }
    }
    
    state.export_status.in_progress = false;
    state.export_status.success = true;
    state.export_status.message = "Exported " + std::to_string(state.split_points.size()) + " tracks successfully";
}

} // namespace mwaac::tui