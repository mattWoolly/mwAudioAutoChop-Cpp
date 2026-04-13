#include "modes/blind_mode.hpp"
#include "core/audio_buffer.hpp"
#include "core/analysis.hpp"
#include "core/music_detection.hpp"
#include <algorithm>
#include <cmath>

namespace mwaac {

std::vector<std::pair<size_t, size_t>> detect_gaps(
    std::span<const float> rms_values,
    float threshold,
    int hop_length,
    int sample_rate,
    float min_gap_seconds,
    float max_gap_seconds)
{
    std::vector<std::pair<size_t, size_t>> gaps;
    
    size_t min_gap_frames = static_cast<size_t>(min_gap_seconds * sample_rate / hop_length);
    size_t max_gap_frames = static_cast<size_t>(max_gap_seconds * sample_rate / hop_length);
    
    bool in_gap = false;
    size_t gap_start = 0;
    
    for (size_t i = 0; i < rms_values.size(); ++i) {
        bool below_threshold = rms_values[i] < threshold;
        
        if (below_threshold && !in_gap) {
            // Start of gap
            gap_start = i;
            in_gap = true;
        } else if (!below_threshold && in_gap) {
            // End of gap
            size_t gap_len = i - gap_start;
            if (gap_len >= min_gap_frames && gap_len <= max_gap_frames) {
                gaps.push_back({gap_start, i});
            }
            in_gap = false;
        }
    }
    
    // Handle gap at end
    if (in_gap) {
        size_t gap_len = rms_values.size() - gap_start;
        if (gap_len >= min_gap_frames && gap_len <= max_gap_frames) {
            gaps.push_back({gap_start, rms_values.size()});
        }
    }
    
    return gaps;
}

float score_gap(
    std::span<const float> samples,
    size_t start_sample,
    size_t end_sample,
    [[maybe_unused]] int sample_rate,
    float noise_floor_rms)
{
    if (end_sample <= start_sample || samples.empty()) {
        return 0.0f;
    }
    
    // Clamp to valid range
    start_sample = std::min(start_sample, samples.size());
    end_sample = std::min(end_sample, samples.size());
    
    // Extract gap segment
    std::vector<float> gap_samples(samples.begin() + start_sample, 
                                    samples.begin() + end_sample);
    
    // Compute RMS of gap
    float sum_sq = 0.0f;
    for (float s : gap_samples) {
        sum_sq += s * s;
    }
    float gap_rms = std::sqrt(sum_sq / gap_samples.size());
    
    // Energy score: how far below noise floor (higher = better)
    float energy_score = 0.0f;
    if (noise_floor_rms > 1e-10f) {
        float ratio = gap_rms / noise_floor_rms;
        energy_score = std::max(0.0f, 1.0f - ratio);
    }
    
    // Confidence is primarily energy-based for now
    // Could add spectral flatness later
    return std::clamp(energy_score, 0.0f, 1.0f);
}

Expected<AnalysisResult, BlindError> analyze_blind_mode(
    const std::filesystem::path& vinyl_path,
    const BlindModeConfig& config)
{
    // Load audio
    auto load_result = load_audio_mono(vinyl_path, config.analysis_sr);
    if (!load_result.ok()) {
        return BlindError::LoadFailed;
    }
    auto audio = std::move(load_result.value());
    
    // Compute RMS energy
    int frame_length = static_cast<int>(0.05f * config.analysis_sr);  // 50ms
    int hop_length = frame_length / 4;  // 12.5ms
    
    auto rms = compute_rms_energy(audio.samples, config.analysis_sr, frame_length, hop_length);
    if (rms.empty()) {
        return BlindError::AnalysisFailed;
    }
    
    // Estimate noise floor
    float noise_floor = estimate_noise_floor(audio.samples, config.analysis_sr);
    
    // Gap threshold: just above noise floor (6 dB)
    float threshold = noise_floor * 2.0f;
    
    // Find gaps
    auto gaps = detect_gaps(rms, threshold, hop_length, config.analysis_sr,
                            config.min_gap_seconds, config.max_gap_seconds);
    
    if (gaps.empty()) {
        return BlindError::NoGapsFound;
    }
    
    // Convert gaps to split points
    // Each gap boundary marks the START of a new track
    std::vector<SplitPoint> split_points;
    
    // First track starts at 0
    SplitPoint first_track;
    first_track.start_sample = 0;
    first_track.source = "blind";
    first_track.confidence = 1.0;  // First track is certain
    split_points.push_back(first_track);
    
    // Each gap end marks start of new track
    for (const auto& gap : gaps) {
        int64_t track_start = static_cast<int64_t>(gap.second * hop_length);
        
        // Score this gap
        float confidence = score_gap(audio.samples, 
                                     gap.first * hop_length,
                                     gap.second * hop_length,
                                     config.analysis_sr,
                                     noise_floor);
        
        if (confidence >= config.confidence_threshold) {
            SplitPoint sp;
            sp.start_sample = track_start;
            sp.confidence = confidence;
            sp.source = "blind";
            sp.evidence["gap_start_frame"] = static_cast<double>(gap.first);
            sp.evidence["gap_end_frame"] = static_cast<double>(gap.second);
            split_points.push_back(sp);
        }
    }
    
    // Set end samples
    for (size_t i = 0; i < split_points.size(); ++i) {
        if (i + 1 < split_points.size()) {
            split_points[i].end_sample = split_points[i + 1].start_sample - 1;
        } else {
            split_points[i].end_sample = static_cast<int64_t>(audio.samples.size()) - 1;
        }
    }
    
    // Build result
    AnalysisResult result;
    result.split_points = std::move(split_points);
    result.mode = "blind";
    result.metadata["noise_floor_rms"] = static_cast<double>(noise_floor);
    result.metadata["num_gaps_found"] = static_cast<double>(gaps.size());
    result.metadata["analysis_sr"] = static_cast<double>(config.analysis_sr);
    
    return result;
}

} // namespace mwaac