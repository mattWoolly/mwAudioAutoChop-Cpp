#include "app.hpp"
#include "waveform.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <string>

namespace mwaac::tui {

using namespace ftxui;

int run_tui(AppState& state) {
    auto screen = ScreenInteractive::Fullscreen();
    
    int cursor_col = 0;
    bool quit = false;
    
    auto component = Renderer([&] {
        int width = Terminal::Size().dimx - 2;
        int height = 20;  // Waveform height
        
        // Downsample audio for display
        auto peaks = downsample_for_display(state.audio.samples, width);
        
        // Calculate marker positions
        std::vector<int> markers;
        for (const auto& sp : state.split_points) {
            if (state.audio.samples.size() > 0) {
                int col = static_cast<int>((sp.start_sample * width) / state.audio.samples.size());
                markers.push_back(col);
            }
        }
        
        // Render waveform
        auto wave_lines = render_waveform(peaks, height, cursor_col, markers);
        
        // Build waveform element
        Elements wave_rows;
        for (const auto& line : wave_lines) {
            wave_rows.push_back(text(line) | color(Color::Cyan));
        }
        
        // Help text
        std::string help = "Q: Quit | Arrow Left/Right: Move cursor | H: Toggle help";
        if (state.show_help) {
            help = "Q: Quit | Arrow Left/Right: Move cursor | +/-: Adjust marker | Space: Add marker | Enter: Export | H: Hide help";
        }
        
        // Status line
        std::string status = "Track " + std::to_string(state.selected_marker + 1) + 
                            " of " + std::to_string(state.split_points.size());
        
        return vbox({
            text("mwAudioAutoChop") | bold | center,
            separator(),
            vbox(wave_rows) | border,
            separator(),
            hbox({
                text(status),
                filler(),
                text(state.vinyl_path.filename().string())
            }),
            separator(),
            text(help) | dim
        });
    });
    
    component = CatchEvent(component, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            quit = true;
            screen.Exit();
            return true;
        }
        if (event == Event::ArrowLeft) {
            cursor_col = std::max(0, cursor_col - 1);
            return true;
        }
        if (event == Event::ArrowRight) {
            cursor_col++;
            return true;
        }
        if (event == Event::Character('h') || event == Event::Character('H')) {
            state.show_help = !state.show_help;
            return true;
        }
        return false;
    });
    
    screen.Loop(component);
    
    return quit ? 0 : 1;
}

} // namespace mwaac::tui