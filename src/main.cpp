#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <iomanip>
#include "modes/reference_mode.hpp"
#include "modes/blind_mode.hpp"
#include "modes/reaper_export.hpp"
#include "core/audio_file.hpp"
#include "core/audio_buffer.hpp"
#include "core/verbose.hpp"
#include "tui/app.hpp"

namespace fs = std::filesystem;

namespace {

// Short human-readable string for an AudioError. Only used in CLI failure
// paths, where we want a one-line description for stderr instead of an
// integer enum. Kept in main.cpp because the AudioError taxonomy is owned
// by audio_file.hpp and we don't want to spread formatting helpers across
// the core library.
std::string_view audio_error_to_string(mwaac::AudioError e) noexcept {
    switch (e) {
        case mwaac::AudioError::FileNotFound:      return "file not found";
        case mwaac::AudioError::InvalidFormat:     return "invalid format";
        case mwaac::AudioError::UnsupportedFormat: return "unsupported format";
        case mwaac::AudioError::ReadError:         return "read error";
        case mwaac::AudioError::WriteError:        return "write error";
        case mwaac::AudioError::InvalidRange:      return "invalid sample range";
        case mwaac::AudioError::ResampleError:     return "resample error";
    }
    return "unknown error";
}

} // namespace

void print_help() {
    std::cout << "mwAudioAutoChop v0.1.0\n\n"
              << "Usage:\n"
              << "  mwaac reference <vinyl> -r <reference_dir> -o <output_dir> [options]\n"
              << "  mwaac blind <vinyl> -o <output_dir> [options]\n"
              << "  mwaac tui <vinyl> -o <output_dir> [options]\n"
              << "\n"
              << "Commands:\n"
              << "  reference               Analyze vinyl using reference tracks\n"
              << "  blind                  Analyze vinyl without reference tracks (gap detection)\n"
              << "  tui                     Interactive waveform editor\n"
              << "\n"
              << "Reference Options:\n"
              << "  -r, --reference <path>   Reference tracks directory (required)\n"
              << "  -o, --output <path>      Output directory (required)\n"
              << "  --reaper <path>          Also write a REAPER .rpp project for A/B\n"
              << "                           comparison and non-destructive fine-tuning\n"
              << "  --dry-run                Preview splits without writing files\n"
              << "  -v, --verbose            Show detailed output\n"
              << "\n"
              << "Blind Options:\n"
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
        fs::path reaper_path;
        bool dry_run = false;
        bool verbose = false;  // TODO: implement verbose output

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-r" || arg == "--reference") && i + 1 < argc) {
                reference_path = argv[++i];
            } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_dir = argv[++i];
            } else if ((arg == "--reaper") && i + 1 < argc) {
                reaper_path = argv[++i];
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            }
        }
        
        // Set verbose mode
        if (verbose) {
            mwaac::set_verbose(true);
            std::cerr << "Verbose mode enabled\n";
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

        // Open the source vinyl once, up-front, and bail on failure. Every
        // downstream use (native_sr, write_track, REAPER export) reads from
        // this single reference, eliminating the previous pattern of
        // re-calling .value() on an Expected that may not hold a value.
        // (C-2: previously the result of .open() was used via the
        // `audio_file ? audio_file.value()...` ternary, but later
        // `audio_file.value()` calls were unguarded.)
        auto opened = mwaac::AudioFile::open(vinyl_path);
        if (!opened) {
            std::cerr << "Error: failed to open " << vinyl_path << ": "
                      << audio_error_to_string(opened.error()) << "\n";
            return 1;
        }
        mwaac::AudioFile& audio_file = opened.value();
        int native_sr = audio_file.info().sample_rate;

        // Reference mode fills in end_sample (including dead-space trimming)
        // before returning; no post-processing needed here.

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
            
            auto write_result = mwaac::write_track(audio_file, output_path,
                                                    sp.start_sample, sp.end_sample);
            if (write_result) {
                std::cout << "  " << filename << "\n";
            } else {
                std::cerr << "  Failed to write " << filename << "\n";
            }
        }

        std::cout << "\nDone! Wrote " << analysis.split_points.size() << " track(s)\n";

        // Optionally write a REAPER project so the user can A/B-compare the
        // chops against the references and drag item edges to fix any
        // boundaries that aren't quite right. Non-destructive — items
        // reference the original vinyl and reference files directly.
        if (!reaper_path.empty()) {
            bool ok = mwaac::write_reaper_project(
                reaper_path, vinyl_path, reference_path,
                analysis.split_points, native_sr);
            if (ok) {
                std::cout << "Wrote REAPER project: " << reaper_path << "\n";
            } else {
                std::cerr << "Failed to write REAPER project to " << reaper_path << "\n";
            }
        }
        return 0;
    }

    if (command == "blind") {
        if (argc < 3) {
            std::cerr << "Error: vinyl path required\n";
            return 1;
        }
        
        fs::path vinyl_path = argv[2];
        fs::path output_dir;
        bool dry_run = false;
        bool verbose = false;
        
        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            }
        }
        
        // Set verbose mode
        if (verbose) {
            mwaac::set_verbose(true);
            std::cerr << "Verbose mode enabled\n";
        }
        
        if (output_dir.empty()) {
            std::cerr << "Error: -o is required\n";
            return 1;
        }
        
        std::cout << "Analyzing vinyl (blind mode): " << vinyl_path << "\n\n";
        
        // Run analysis
        auto result = mwaac::analyze_blind_mode(vinyl_path);
        if (!result) {
            std::cerr << "Error: Analysis failed: ";
            switch (result.error()) {
                case mwaac::BlindError::LoadFailed:
                    std::cerr << "Failed to load audio file\n";
                    break;
                case mwaac::BlindError::AnalysisFailed:
                    std::cerr << "Analysis failed\n";
                    break;
                case mwaac::BlindError::NoGapsFound:
                    std::cerr << "No gaps found in audio\n";
                    break;
            }
            return 1;
        }
        
        auto& analysis = result.value();

        // Print results
        std::cout << "=== BLIND Mode Analysis ===\n";
        std::cout << "Found " << analysis.split_points.size() << " track(s)\n\n";

        // Open the source vinyl once and bail on failure. See the matching
        // comment in the `reference` branch for context. Previously this
        // path used `audio_file ? audio_file.value()... : 0` and then
        // unconditionally proceeded to write tracks — when open() failed,
        // total_frames defaulted to 0, sp.end_sample became -1, and write
        // calls hit unguarded `.value()` on an errored Expected (C-2 UB).
        auto opened = mwaac::AudioFile::open(vinyl_path);
        if (!opened) {
            std::cerr << "Error: failed to open " << vinyl_path << ": "
                      << audio_error_to_string(opened.error()) << "\n";
            return 1;
        }
        mwaac::AudioFile& audio_file = opened.value();
        int native_sr = audio_file.info().sample_rate;

        // Fix end samples (last track goes to end of file)
        int64_t total_frames = audio_file.info().frames;
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
            
            auto write_result = mwaac::write_track(audio_file, output_path,
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
        state.audio = std::move(load_result).value();
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