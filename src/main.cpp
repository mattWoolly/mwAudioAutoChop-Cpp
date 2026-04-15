#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <filesystem>
#include <iomanip>
#include "modes/reference_mode.hpp"
#include "modes/blind_mode.hpp"
#include "core/audio_file.hpp"
#include "core/audio_buffer.hpp"
#include "core/analysis.hpp"
#include "core/verbose.hpp"
#include "tui/app.hpp"

namespace fs = std::filesystem;

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
              << "  --trim-silence           Trim trailing silence from tracks\n"
              << "  --trim-threshold <rms>   Silence threshold (default: auto from noise floor)\n"
              << "  --trim-buffer <seconds>  Keep this much silence for fade-out (default: 0.3)\n"
              << "  --dry-run                Preview splits without writing files\n"
              << "  -v, --verbose            Show detailed output\n"
              << "\n"
              << "Blind Options:\n"
              << "  -o, --output <path>      Output directory (required)\n"
              << "  -s, --sensitivity <0-1>  Gap detection sensitivity (default: auto)\n"
              << "  -n, --tracks <N>         Expected number of tracks (optional)\n"
              << "  --min-gap <seconds>      Minimum gap duration (default: 1.0)\n"
              << "  --max-gap <seconds>      Maximum gap duration (default: 30.0)\n"
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
        bool trim_silence = false;
        float trim_threshold = -1.0f;  // -1 = auto from noise floor
        float trim_buffer = 0.3f;      // seconds of silence to keep
        
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
            } else if (arg == "--trim-silence") {
                trim_silence = true;
            } else if (arg == "--trim-threshold" && i + 1 < argc) {
                trim_threshold = std::stof(argv[++i]);
            } else if (arg == "--trim-buffer" && i + 1 < argc) {
                trim_buffer = std::stof(argv[++i]);
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
        
        // Trim trailing silence if requested
        if (trim_silence && audio_file.has_value()) {
            std::cout << "Trimming trailing silence...\n";
            
            // Load audio at a lower sample rate for faster analysis
            int analysis_sr = 22050;
            auto audio_result = mwaac::load_audio_mono(vinyl_path, analysis_sr);
            if (audio_result.ok()) {
                auto& audio = audio_result.value();
                double sr_ratio = static_cast<double>(native_sr) / analysis_sr;
                
                for (size_t i = 0; i < analysis.split_points.size(); ++i) {
                    auto& sp = analysis.split_points[i];
                    
                    // Convert sample positions to analysis rate
                    int64_t start_analysis = static_cast<int64_t>(sp.start_sample / sr_ratio);
                    int64_t end_analysis = static_cast<int64_t>(sp.end_sample / sr_ratio);
                    
                    // Clamp to valid range
                    start_analysis = std::max<int64_t>(0, start_analysis);
                    end_analysis = std::min<int64_t>(static_cast<int64_t>(audio.samples.size()) - 1, end_analysis);
                    
                    if (end_analysis > start_analysis) {
                        // Extract track samples
                        std::span<const float> track_samples(
                            audio.samples.data() + start_analysis,
                            end_analysis - start_analysis + 1
                        );
                        
                        // Find where trailing silence starts
                        int64_t trim_pos = mwaac::find_trailing_silence_start(
                            track_samples, analysis_sr, trim_threshold, trim_buffer
                        );
                        
                        // Convert back to native rate and update end_sample
                        int64_t new_end = sp.start_sample + static_cast<int64_t>(trim_pos * sr_ratio);
                        
                        // Only trim if we found significant silence (at least 1 second saved)
                        int64_t saved_samples = sp.end_sample - new_end;
                        if (saved_samples > native_sr) {  // More than 1 second
                            if (verbose) {
                                double saved_sec = static_cast<double>(saved_samples) / native_sr;
                                std::cout << "  Track " << (i + 1) << ": trimmed " 
                                          << std::fixed << std::setprecision(1) << saved_sec << "s\n";
                            }
                            sp.end_sample = new_end;
                        }
                    }
                }
            }
            std::cout << "\n";
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
    
    if (command == "blind") {
        if (argc < 3) {
            std::cerr << "Error: vinyl path required\n";
            return 1;
        }
        
        fs::path vinyl_path = argv[2];
        fs::path output_dir;
        bool dry_run = false;
        bool verbose = false;
        mwaac::BlindModeConfig config;
        
        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_dir = argv[++i];
            } else if (arg == "--dry-run") {
                dry_run = true;
            } else if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            } else if ((arg == "-s" || arg == "--sensitivity") && i + 1 < argc) {
                config.sensitivity = std::stof(argv[++i]);
                if (config.sensitivity < 0.0f || config.sensitivity > 1.0f) {
                    std::cerr << "Error: sensitivity must be between 0.0 and 1.0\n";
                    return 1;
                }
            } else if ((arg == "-n" || arg == "--tracks") && i + 1 < argc) {
                config.expected_tracks = std::stoi(argv[++i]);
                if (config.expected_tracks < 1) {
                    std::cerr << "Error: track count must be at least 1\n";
                    return 1;
                }
            } else if (arg == "--min-gap" && i + 1 < argc) {
                config.min_gap_seconds = std::stof(argv[++i]);
            } else if (arg == "--max-gap" && i + 1 < argc) {
                config.max_gap_seconds = std::stof(argv[++i]);
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
        auto result = mwaac::analyze_blind_mode(vinyl_path, config);
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