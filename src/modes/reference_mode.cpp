#include "modes/reference_mode.hpp"
#include "core/audio_buffer.hpp"
#include "core/correlation.hpp"
#include "core/music_detection.hpp"
#include <algorithm>
#include <filesystem>
#include <regex>

namespace mwaac {

namespace {

// Natural sort helper
std::vector<std::filesystem::path> natural_sort(
    std::vector<std::filesystem::path> paths) 
{
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return a.filename().string() < b.filename().string();
    });
    return paths;
}

// Check if path is audio file
bool is_audio_file(const std::filesystem::path& p) {
    static const std::vector<std::string> exts = {
        ".wav", ".aiff", ".aif", ".flac", ".mp3", ".ogg", ".m4a"
    };
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

} // anonymous namespace

Expected<std::vector<ReferenceTrack>, ReferenceError> load_reference_tracks(
    const std::filesystem::path& reference_dir,
    int sample_rate)
{
    if (!std::filesystem::is_directory(reference_dir)) {
        return ReferenceError::ReferenceLoadFailed;
    }
    
    std::vector<std::filesystem::path> audio_files;
    for (const auto& entry : std::filesystem::directory_iterator(reference_dir)) {
        if (entry.is_regular_file() && is_audio_file(entry.path())) {
            audio_files.push_back(entry.path());
        }
    }
    
    if (audio_files.empty()) {
        return ReferenceError::NoTracksFound;
    }
    
    audio_files = natural_sort(audio_files);
    
    std::vector<ReferenceTrack> tracks;
    for (const auto& path : audio_files) {
        auto result = load_audio_mono(path, sample_rate);
        if (!result.ok()) {
            continue;  // Skip failed loads
        }
        
        ReferenceTrack track;
        track.path = path;
        track.audio = std::move(result.value());
        track.duration_samples = track.audio.samples.size();
        tracks.push_back(std::move(track));
    }
    
    if (tracks.empty()) {
        return ReferenceError::ReferenceLoadFailed;
    }
    
    return tracks;
}

std::vector<std::pair<int64_t, double>> align_per_track(
    const AudioBuffer& vinyl,
    const std::vector<ReferenceTrack>& tracks,
    int64_t music_start_sample)
{
    std::vector<std::pair<int64_t, double>> offsets;
    
    int64_t expected_pos = music_start_sample;
    
    for (const auto& track : tracks) {
        // Cross-correlate track against vinyl
        auto result = cross_correlate(track.audio.samples, vinyl.samples);
        
        // Convert lag to absolute position
        int64_t vinyl_start = result.lag;
        if (vinyl_start < 0) vinyl_start = 0;
        if (vinyl_start >= static_cast<int64_t>(vinyl.samples.size())) {
            vinyl_start = vinyl.samples.size() - 1;
        }
        
        offsets.push_back({vinyl_start, result.peak_value});
        
        // Update expected position for next track
        expected_pos = vinyl_start + track.duration_samples;
    }
    
    return offsets;
}

Expected<AnalysisResult, ReferenceError> analyze_reference_mode(
    const std::filesystem::path& vinyl_path,
    const std::filesystem::path& reference_path,
    int analysis_sr)
{
    // Load vinyl
    auto vinyl_result = load_audio_mono(vinyl_path, analysis_sr);
    if (!vinyl_result.ok()) {
        return ReferenceError::VinylLoadFailed;
    }
    auto vinyl = std::move(vinyl_result.value());
    
    // Detect music start
    int64_t music_start = detect_music_start(vinyl.samples, analysis_sr);
    
    // Load reference tracks
    auto tracks_result = load_reference_tracks(reference_path, analysis_sr);
    if (!tracks_result) {
        return tracks_result.error();
    }
    auto tracks = std::move(tracks_result.value());
    
    // Align each track
    auto offsets = align_per_track(vinyl, tracks, music_start);
    
    // Build split points
    std::vector<SplitPoint> split_points;
    
    // Get native sample rate for output (assume same as analysis for now)
    // TODO: Use AudioFile to get actual native rate
    int native_sr = analysis_sr;  // Placeholder
    
    for (size_t i = 0; i < offsets.size(); ++i) {
        SplitPoint sp;
        sp.start_sample = offsets[i].first * native_sr / analysis_sr;
        
        // End sample is start of next track - 1, or end of file
        if (i + 1 < offsets.size()) {
            sp.end_sample = (offsets[i + 1].first * native_sr / analysis_sr) - 1;
        } else {
            sp.end_sample = -1;  // Will be filled in later
        }
        
        sp.confidence = offsets[i].second;
        sp.source = "reference";
        sp.evidence["track_index"] = static_cast<double>(i);
        sp.evidence["track_name"] = tracks[i].path.filename().string();
        
        split_points.push_back(sp);
    }
    
    // Build result
    AnalysisResult result;
    result.split_points = std::move(split_points);
    result.mode = "reference";
    result.metadata["music_start_sample"] = static_cast<double>(music_start);
    result.metadata["analysis_sr"] = static_cast<double>(analysis_sr);
    result.metadata["num_tracks"] = static_cast<double>(tracks.size());
    
    return result;
}

} // namespace mwaac