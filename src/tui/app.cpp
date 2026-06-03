#include "app.hpp"
#include "app_handlers.hpp"
#include "waveform.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <utility>
#include "core/audio_file.hpp"

namespace mwaac::tui {

using namespace ftxui;

// Forward declaration for export
void export_tracks(AppState& state);

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — FTXUI event-loop wiring; decomposition tracked separately.
int run_tui(AppState& state) {
    auto screen = ScreenInteractive::Fullscreen();

    // Cursor follows selected marker
    int cursor_col = 0;
    if (!state.split_points.empty() && state.selected_marker >= 0 &&
        std::cmp_less(state.selected_marker, state.split_points.size())) {
        int width = Terminal::Size().dimx - 2;
        if (!state.audio.samples.empty()) {
            cursor_col = static_cast<int>(static_cast<std::size_t>(state.split_points[static_cast<std::size_t>(state.selected_marker)].start_sample * width) / state.audio.samples.size());
        }
    }

    auto component = Renderer([&] {
        int width = Terminal::Size().dimx - 2;
        int height = 20;  // Waveform height
        auto total_samples = static_cast<int64_t>(state.audio.samples.size());
        
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
                    MarkerInfo mi{};
                    mi.column = col;
                    mi.track_number = static_cast<int>(i + 1);
                    mi.selected = (std::cmp_equal(i, state.selected_marker));
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
    
    component = CatchEvent(component, [&](const Event& event) {
        // Handle export progress while exporting
        if (state.export_status.in_progress) {
            return true;  // Block other input during export
        }
        
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            // Mi-10: `screen.Exit()` is the actual quit mechanism — it
            // breaks the event loop. The pre-cure `bool quit` sentinel
            // and the `return quit ? 0 : 1` at function end inverted
            // the documented contract (Ctrl-C and any other non-Q exit
            // returned 1, contradicting the docstring's "0 on normal
            // exit, non-zero only on init failure"). Removed; every
            // event-loop exit is now a normal exit.
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
        
        // Marker fine adjustment. Mi-MARKER-NUDGE-SEMANTIC (re-cure of
        // Mi-8, 2026-05-20): boundary-shift semantic. The mutators
        // in app_handlers.cpp move the BOUNDARY between
        // markers[selected-1] and markers[selected] in lockstep;
        // adjacent track durations change inversely. First marker
        // (selected == 0) and zero-duration-collapse cases no-op.
        // The event handler just dispatches.
        if (event == Event::Character('+') || event == Event::Character('=') || event == Event::Character(']')) {
            nudge_marker_right(state);
            return true;
        }
        if (event == Event::Character('-') || event == Event::Character('_') || event == Event::Character('[')) {
            nudge_marker_left(state);
            return true;
        }
        
        // Zoom in/out. Mi-9: view-bounds normalization (clamp against
        // [0, total_samples], enforce strict-less-than) lives inside
        // the mutators in app_handlers.cpp; the event handler just
        // dispatches.
        if (event == Event::ArrowUp) {
            zoom_in(state);
            return true;
        }
        if (event == Event::ArrowDown) {
            zoom_out(state);
            return true;
        }

        // Jump to start/end (pan view, maintain zoom level).
        if (event == Event::Home) {
            pan_to_start(state);
            return true;
        }
        if (event == Event::End) {
            pan_to_end(state);
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
        
        // Arrow keys for cursor movement (legacy support).
        // Mi-CURSOR-COL-CLAMP: bounds clamping (upper at display_width - 1)
        // lives in the mutators in app_handlers.cpp; the event handler
        // queries Terminal::Size() and dispatches.
        if (event == Event::ArrowLeft) {
            move_cursor_left(cursor_col);
            return true;
        }
        if (event == Event::ArrowRight) {
            const int width = Terminal::Size().dimx - 2;
            move_cursor_right(cursor_col, width);
            return true;
        }
        
        return false;
    });
    
    screen.Loop(component);

    // Mi-10: every loop exit is a normal exit (per docstring contract
    // in src/tui/app.hpp). FTXUI's `Fullscreen()` and `Loop()` are
    // best-effort with no throwing failure modes (verified against the
    // vendored FTXUI source — zero `throw` in screen_interactive.cpp),
    // so initialization failure manifests as a degraded loop rather
    // than a return-or-throw signal that this function could surface
    // via a non-zero return.
    return 0;
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