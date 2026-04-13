#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <iomanip>
#include "modes/reference_mode.hpp"
#include "core/audio_file.hpp"
#include "core/audio_buffer.hpp"
#include "tui/app.hpp"

namespace fs = std::filesystem;

void print_help() {
    std::cout << "mwAudioAutoChop v0.1.0\n\n"
              << "Usage:\n"
              << "  mwaac reference <vinyl> -r <reference_dir> -o <output_dir> [options]\n"
              << "  mwaac tui <vinyl> -o <output_dir> [options]\n"
              << "\n"
              << "Commands:\n"
              << "  reference               Analyze vinyl using reference tracks\n"
              << "  tui                     Interactive waveform editor\n"
              << "\n"
              << "Reference Options:\n"
              << "  -r, --reference <path>   Reference tracks directory (required)\n"
              << "  -o, --output <path>      Output directory (required)\n"
              << "  --dry-run                Preview splits without writing files\n"
              << "  -v, --verbose            Show detailed output\n"
              << "\n"
              << "TUI Options:\n"
              << "  -o, --output <path>      Output directory (required)\n"
              << "  -h, --help               Show this help\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    std::string command = argv[1];
    
    if (command == "-h" || command == "--help") {
        print_help();
        return 0;
    }
    
    if (command == "reference") {
        if (argc < 3) {
            std::cerr << "Error: vinyl path required\n";
            return 1;
        }
        
        fs::path vinyl_path = argv[2];
        fs::path reference_path;
        fs::path output_dir;
        bool dry_run = false;
        bool verbose = false;
        
        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-r" || arg == "--reference") && i + 1 < argc) {
                reference_path = argv[++i];
            } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            }
        }
        
        if (reference_path.empty() || output_dir.empty()) {
            std::cerr << "Error: -r and -o are required\n";
            return 1;
        }
        
        std::cout << "Analyzing vinyl: " << vinyl_path << "\n";
        std::cout << "Reference: " << reference_path << "\n";
        std::cout << "Output: " << output_dir << "\n\n";
        
        // Run analysis
        auto result = mwaac::analyze_reference_mode(vinyl_path, reference_path);
        if (!result) {
            std::cerr << "Error: Analysis failed\n";
            return 1;
        }
        
        auto& analysis = result.value();
        
        // Print results
        std::cout << "=== REFERENCE Mode Analysis ===\n";
        std::cout << "Found " << analysis.split_points.size() << " track(s)\n\n";
        
        // Get native sample rate
        auto audio_file = mwaac::AudioFile::open(vinyl_path);
        int native_sr = audio_file ? audio_file.value().info().sample_rate : 44100;
        
        // Fix end samples (last track goes to end of file)
        int64_t total_frames = audio_file ? audio_file.value().info().frames : 0;
        for (size_t i = 0; i < analysis.split_points.size(); ++i) {
            auto& sp = analysis.split_points[i];
            if (i + 1 < analysis.split_points.size()) {
                sp.end_sample = analysis.split_points[i + 1].start_sample - 1;
            } else {
                sp.end_sample = total_frames - 1;
            }
        }
        
        // Print track info
        for (size_t i = 0; i < analysis.split_points.size(); ++i) {
            auto& sp = analysis.split_points[i];
            double start_sec = static_cast<double>(sp.start_sample) / native_sr;
            double end_sec = static_cast<double>(sp.end_sample) / native_sr;
            double duration = end_sec - start_sec;
            
            int start_min = static_cast<int>(start_sec) / 60;
            int start_s = static_cast<int>(start_sec) % 60;
            int end_min = static_cast<int>(end_sec) / 60;
            int end_s = static_cast<int>(end_sec) % 60;
            
            std::cout << "Track " << std::setw(2) << std::setfill('0') << (i + 1) << ": "
                      << std::setw(2) << start_min << ":" << std::setw(2) << start_s << " - "
                      << std::setw(2) << end_min << ":" << std::setw(2) << end_s
                      << " (" << static_cast<int>(duration / 60) << "m " 
                      << static_cast<int>(static_cast<int>(duration) % 60) << "s)"
                      << " — confidence: " << std::fixed << std::setprecision(2) << sp.confidence
                      << "\n";
        }
        
        if (dry_run) {
            std::cout << "\nDRY RUN — no files written\n";
            return 0;
        }
        
        // Create output directory
        fs::create_directories(output_dir);
        
        // Export tracks
        std::cout << "\nWriting tracks...\n";
        for (size_t i = 0; i < analysis.split_points.size(); ++i) {
            auto& sp = analysis.split_points[i];
            
            std::string filename = "track_" + std::to_string(i + 1) + ".wav";
            fs::path output_path = output_dir / filename;
            
            auto write_result = mwaac::write_track(audio_file.value(), output_path, 
                                                    sp.start_sample, sp.end_sample);
            if (write_result) {
                std::cout << "  " << filename << "\n";
            } else {
                std::cerr << "  Failed to write " << filename << "\n";
            }
        }
        
        std::cout << "\nDone! Wrote " << analysis.split_points.size() << " track(s)\n";
        return 0;
    }
    
    if (command == "tui") {
        if (argc < 3) {
            std::cerr << "Error: vinyl path required\n";
            return 1;
        }
        
        fs::path vinyl_path = argv[2];
        fs::path output_dir;
        
        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_dir = argv[++i];
            }
        }
        
        if (output_dir.empty()) {
            std::cerr << "Error: -o is required\n";
            return 1;
        }
        
        // Load audio
        std::cout << "Loading: " << vinyl_path << "\n";
        auto load_result = mwaac::load_audio_mono(vinyl_path);
        if (!load_result) {
            std::cerr << "Error: Failed to load audio: " << static_cast<int>(load_result.error()) << "\n";
            return 1;
        }
        
        mwaac::tui::AppState state;
        state.audio = std::move(load_result.value());
        state.vinyl_path = vinyl_path;
        state.output_dir = output_dir;
        
        std::cout << "Loaded " << state.audio.samples.size() << " samples at "
                 << state.audio.sample_rate << " Hz\n";
        std::cout << "Starting TUI...\n";
        
        return mwaac::tui::run_tui(state);
    }
    
    std::cerr << "Unknown command: " << command << "\n";
    print_help();
    return 1;
}