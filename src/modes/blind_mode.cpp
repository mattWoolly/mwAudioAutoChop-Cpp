#include "modes/blind_mode.hpp"
#include "core/audio_buffer.hpp"
#include "core/analysis.hpp"
#include "core/music_detection.hpp"
#include "core/verbose.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

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

float compute_edge_sharpness(
    std::span<const float> samples,
    size_t boundary_sample,
    [[maybe_unused]] int sample_rate,
    size_t window_samples)
{
    // Measure how abruptly the signal level changes at the boundary
    // Returns 0-1, where 1 = very sharp transition
    
    if (samples.empty() || boundary_sample >= samples.size()) {
        return 0.0f;
    }
    
    size_t before_start = (boundary_sample > window_samples) ? 
                          (boundary_sample - window_samples) : 0;
    size_t after_end = std::min(boundary_sample + window_samples, samples.size());
    
    // RMS before boundary
    float sum_sq_before = 0.0f;
    size_t count_before = boundary_sample - before_start;
    for (size_t i = before_start; i < boundary_sample && i < samples.size(); ++i) {
        sum_sq_before += samples[i] * samples[i];
    }
    float rms_before = (count_before > 0) ? std::sqrt(sum_sq_before / count_before) : 0.0f;
    
    // RMS after boundary
    float sum_sq_after = 0.0f;
    size_t count_after = after_end - boundary_sample;
    for (size_t i = boundary_sample; i < after_end && i < samples.size(); ++i) {
        sum_sq_after += samples[i] * samples[i];
    }
    float rms_after = (count_after > 0) ? std::sqrt(sum_sq_after / count_after) : 0.0f;
    
    // Sharpness is based on ratio difference
    // High sharpness = one side is much louder than the other
    float max_rms = std::max(rms_before, rms_after);
    float min_rms = std::min(rms_before, rms_after);
    
    if (max_rms < 1e-10f) return 0.0f;
    
    // Ratio of difference to max
    float sharpness = (max_rms - min_rms) / max_rms;
    return std::clamp(sharpness, 0.0f, 1.0f);
}

float score_gap(
    std::span<const float> samples,
    size_t start_sample,
    size_t end_sample,
    int sample_rate,
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
    
    // 1. Energy score: how far below noise floor (higher = better)
    float energy_score = 0.0f;
    if (noise_floor_rms > 1e-10f) {
        float ratio = gap_rms / noise_floor_rms;
        energy_score = std::max(0.0f, 1.0f - ratio);
    }
    
    // 2. Duration score: longer gaps are more likely to be real track boundaries
    // Peaks at ~5s gap, tails off for very short or very long gaps
    float gap_duration_sec = static_cast<float>(end_sample - start_sample) / sample_rate;
    float ideal_gap = 5.0f;  // 5 seconds is a typical inter-track gap
    float duration_score;
    if (gap_duration_sec < ideal_gap) {
        // Short gaps: score increases with duration
        duration_score = gap_duration_sec / ideal_gap;
    } else {
        // Long gaps: slight penalty but still high
        duration_score = 1.0f - 0.1f * std::min((gap_duration_sec - ideal_gap) / 20.0f, 0.5f);
    }
    duration_score = std::clamp(duration_score, 0.0f, 1.0f);
    
    // 3. Edge sharpness: sharp transitions are more likely to be track boundaries
    size_t edge_window = static_cast<size_t>(0.5f * sample_rate);  // 500ms window
    float start_sharpness = compute_edge_sharpness(samples, start_sample, sample_rate, edge_window);
    float end_sharpness = compute_edge_sharpness(samples, end_sample, sample_rate, edge_window);
    float edge_score = (start_sharpness + end_sharpness) / 2.0f;
    
    // Combined score with weights
    // Energy is most important, then duration, then edge sharpness
    float combined = 0.5f * energy_score + 0.3f * duration_score + 0.2f * edge_score;
    
    return std::clamp(combined, 0.0f, 1.0f);
}

Expected<AnalysisResult, BlindError> analyze_blind_mode(
    const std::filesystem::path& vinyl_path,
    const BlindModeConfig& config)
{
    VerboseTimer timer("blind mode analysis");
    
    // Get native sample rate for conversion
    auto audio_file = AudioFile::open(vinyl_path);
    if (!audio_file.has_value()) {
        verbose("ERROR: Failed to open audio file for info");
        return BlindError::LoadFailed;
    }
    int native_sr = audio_file.value().info().sample_rate;
    int64_t native_frames = audio_file.value().info().frames;
    
    // Calculate conversion ratio from analysis rate to native rate
    double sr_ratio = static_cast<double>(native_sr) / config.analysis_sr;
    
    if (g_verbose) {
        verbose("  Native sample rate: " + std::to_string(native_sr) + " Hz");
        verbose("  Analysis sample rate: " + std::to_string(config.analysis_sr) + " Hz");
        verbose("  Conversion ratio: " + std::to_string(sr_ratio));
    }
    
    // Load audio at analysis sample rate
    verbose("Loading audio...");
    auto load_result = load_audio_mono(vinyl_path, config.analysis_sr);
    if (!load_result.ok()) {
        verbose("ERROR: Failed to load audio");
        return BlindError::LoadFailed;
    }
    auto audio = std::move(load_result.value());
    
    if (g_verbose) {
        verbose("  Loaded: " + std::to_string(audio.samples.size()) + 
               " samples at " + std::to_string(audio.sample_rate) + " Hz");
    }
    
    // Compute RMS energy
    int frame_length = static_cast<int>(0.05f * config.analysis_sr);  // 50ms
    int hop_length = frame_length / 4;  // 12.5ms
    
    verbose("Computing RMS energy...");
    auto rms = compute_rms_energy(audio.samples, config.analysis_sr, frame_length, hop_length);
    if (rms.empty()) {
        verbose("ERROR: RMS computation failed");
        return BlindError::AnalysisFailed;
    }
    
    if (g_verbose) {
        verbose("  Frame length: " + std::to_string(frame_length) + " samples");
        verbose("  Hop length: " + std::to_string(hop_length) + " samples");
        verbose("  RMS frames: " + std::to_string(rms.size()));
    }
    
    // Estimate noise floor
    verbose("Estimating noise floor...");
    float noise_floor = estimate_noise_floor(audio.samples, config.analysis_sr);
    
    // Calculate threshold based on sensitivity
    // sensitivity = -1: auto (Otsu's method)
    // sensitivity = 0.0: very low sensitivity (only deep gaps) -> threshold = noise_floor * 10
    // sensitivity = 0.5: moderate -> threshold = noise_floor * 4
    // sensitivity = 1.0: very high sensitivity -> threshold = noise_floor * 1.5
    float threshold;
    float effective_sensitivity = config.sensitivity;
    bool auto_sensitivity = (effective_sensitivity < 0);
    
    if (auto_sensitivity) {
        // Use Otsu's method to find optimal threshold
        effective_sensitivity = compute_auto_sensitivity(rms, noise_floor);
        if (g_verbose) {
            verbose("  Using Otsu's method for auto-threshold...");
        }
    }
    
    // Map sensitivity [0,1] to threshold multiplier [10, 1.5]
    // Higher sensitivity = lower threshold multiplier = more gaps detected
    float min_multiplier = 1.5f;  // Most sensitive
    float max_multiplier = 10.0f; // Least sensitive
    float multiplier = max_multiplier - (max_multiplier - min_multiplier) * effective_sensitivity;
    threshold = noise_floor * multiplier;
    
    if (g_verbose) {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(2) << noise_floor;
        std::ostringstream thresh_oss;
        thresh_oss << std::scientific << std::setprecision(2) << threshold;
        std::ostringstream mult_oss;
        mult_oss << std::fixed << std::setprecision(1) << multiplier;
        std::ostringstream sens_oss;
        sens_oss << std::fixed << std::setprecision(2) << effective_sensitivity;
        verbose("  Noise floor RMS: " + oss.str());
        verbose("  Sensitivity: " + sens_oss.str() + (config.sensitivity < 0 ? " (auto)" : " (manual)"));
        verbose("  Threshold multiplier: " + mult_oss.str() + "x noise floor");
        verbose("  Gap threshold: " + thresh_oss.str());
    }
    
    // Find gaps
    verbose("Detecting gaps...");
    
    // If expected_tracks is specified, use a more permissive threshold to find candidates
    float search_threshold = threshold;
    if (config.expected_tracks > 0) {
        // Use a lower threshold to find more candidates when we know track count
        search_threshold = noise_floor * 1.2f;  // Very sensitive for candidate detection
        if (g_verbose) {
            verbose("  Track count specified: " + std::to_string(config.expected_tracks));
            verbose("  Using permissive threshold for candidate search");
        }
    }
    
    auto gaps = detect_gaps(rms, search_threshold, hop_length, config.analysis_sr,
                            config.min_gap_seconds, config.max_gap_seconds);
    
    if (g_verbose) {
        verbose("  Min gap: " + std::to_string(config.min_gap_seconds) + "s");
        verbose("  Max gap: " + std::to_string(config.max_gap_seconds) + "s");
        verbose("  Candidate gaps found: " + std::to_string(gaps.size()));
    }
    
    if (gaps.empty()) {
        verbose("WARNING: No gaps detected");
        return BlindError::NoGapsFound;
    }
    
    // Score all gaps
    struct ScoredGap {
        std::pair<size_t, size_t> gap;
        float confidence;
        int64_t track_start;
    };
    std::vector<ScoredGap> scored_gaps;
    
    if (g_verbose) {
        verbose_section("Gap Scoring");
    }
    
    for (const auto& gap : gaps) {
        int64_t track_start = static_cast<int64_t>(gap.second * hop_length);
        int64_t gap_duration = static_cast<int64_t>((gap.second - gap.first) * hop_length);
        
        float confidence = score_gap(audio.samples, 
                                     gap.first * hop_length,
                                     gap.second * hop_length,
                                     config.analysis_sr,
                                     noise_floor);
        
        scored_gaps.push_back({gap, confidence, track_start});
        
        if (g_verbose) {
            double gap_start_sec = static_cast<double>(gap.first * hop_length) / config.analysis_sr;
            double gap_duration_sec = static_cast<double>(gap_duration) / config.analysis_sr;
            std::ostringstream conf_oss;
            conf_oss << std::fixed << std::setprecision(3) << confidence;
            std::ostringstream dur_oss;
            dur_oss << std::fixed << std::setprecision(2) << gap_duration_sec;
            std::ostringstream start_oss;
            start_oss << std::fixed << std::setprecision(2) << gap_start_sec;
            verbose("  Gap at " + start_oss.str() + "s: duration=" + dur_oss.str() + "s, confidence=" + conf_oss.str());
        }
    }
    
    // Select gaps based on mode
    std::vector<ScoredGap> selected_gaps;
    
    // Minimum track duration to avoid selecting gaps that create tiny segments
    // For most albums, tracks are at least 90 seconds (1.5 minutes)
    const float min_track_duration_sec = 90.0f;  // At least 1.5 minutes per track
    const size_t min_track_samples = static_cast<size_t>(min_track_duration_sec * config.analysis_sr);
    
    if (config.expected_tracks > 0) {
        // N-best selection with constraint: gaps must not create tracks shorter than min_track_duration
        int gaps_needed = config.expected_tracks - 1;
        
        // Sort by confidence (descending)
        std::sort(scored_gaps.begin(), scored_gaps.end(),
                  [](const ScoredGap& a, const ScoredGap& b) {
                      return a.confidence > b.confidence;
                  });
        
        // Greedy selection: take highest confidence gaps that maintain minimum track length
        for (const auto& candidate : scored_gaps) {
            if (static_cast<int>(selected_gaps.size()) >= gaps_needed) break;
            
            // Check if adding this gap would create a track that's too short
            // First, find where this gap would fit in the currently selected gaps
            auto insert_pos = std::lower_bound(
                selected_gaps.begin(), selected_gaps.end(), candidate,
                [](const ScoredGap& a, const ScoredGap& b) {
                    return a.track_start < b.track_start;
                });
            
            // Check distance to previous track boundary
            int64_t prev_boundary = 0;
            if (insert_pos != selected_gaps.begin()) {
                prev_boundary = std::prev(insert_pos)->track_start;
            }
            
            // Check distance to next track boundary
            int64_t next_boundary = static_cast<int64_t>(audio.samples.size());
            if (insert_pos != selected_gaps.end()) {
                next_boundary = insert_pos->track_start;
            }
            
            // Would the track before this gap be too short?
            bool prev_ok = (candidate.track_start - prev_boundary) >= static_cast<int64_t>(min_track_samples);
            
            // Would the track after this gap be too short?
            bool next_ok = (next_boundary - candidate.track_start) >= static_cast<int64_t>(min_track_samples);
            
            if (prev_ok && next_ok) {
                // Insert in sorted position
                selected_gaps.insert(insert_pos, candidate);
            } else if (g_verbose) {
                double gap_start_sec = static_cast<double>(candidate.track_start) / config.analysis_sr;
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << gap_start_sec;
                verbose("  Skipped gap at " + oss.str() + "s (would create track < " + 
                       std::to_string(static_cast<int>(min_track_duration_sec)) + "s)");
            }
        }
        
        if (g_verbose) {
            verbose("  Selected " + std::to_string(selected_gaps.size()) + 
                   " gaps for " + std::to_string(config.expected_tracks) + " tracks");
            if (static_cast<int>(selected_gaps.size()) < gaps_needed) {
                verbose("  WARNING: Could only find " + std::to_string(selected_gaps.size() + 1) + 
                       " tracks with min duration " + std::to_string(static_cast<int>(min_track_duration_sec)) + "s");
            }
        }
    } else {
        // Threshold-based selection
        for (const auto& sg : scored_gaps) {
            if (sg.confidence >= config.confidence_threshold) {
                selected_gaps.push_back(sg);
            }
        }
        
        if (g_verbose) {
            verbose("  Selected " + std::to_string(selected_gaps.size()) + 
                   " gaps above confidence threshold " + std::to_string(config.confidence_threshold));
        }
    }
    
    // Convert to split points (converting from analysis samples to native samples)
    std::vector<SplitPoint> split_points;
    
    // First track starts at 0
    SplitPoint first_track;
    first_track.start_sample = 0;
    first_track.source = "blind";
    first_track.confidence = 1.0;
    split_points.push_back(first_track);
    
    for (const auto& sg : selected_gaps) {
        SplitPoint sp;
        // Convert from analysis sample rate to native sample rate
        sp.start_sample = static_cast<int64_t>(sg.track_start * sr_ratio);
        sp.confidence = sg.confidence;
        sp.source = "blind";
        sp.evidence["gap_start_frame"] = static_cast<double>(sg.gap.first);
        sp.evidence["gap_end_frame"] = static_cast<double>(sg.gap.second);
        split_points.push_back(sp);
    }
    
    if (g_verbose) {
        verbose("  Total tracks: " + std::to_string(split_points.size()));
    }
    
    // Set end samples (in native sample rate)
    for (size_t i = 0; i < split_points.size(); ++i) {
        if (i + 1 < split_points.size()) {
            split_points[i].end_sample = split_points[i + 1].start_sample - 1;
        } else {
            // Last track ends at end of file (in native samples)
            split_points[i].end_sample = native_frames - 1;
        }
    }
    
    // Build result
    AnalysisResult result;
    result.split_points = std::move(split_points);
    result.mode = "blind";
    result.metadata["noise_floor_rms"] = static_cast<double>(noise_floor);
    result.metadata["num_gaps_found"] = static_cast<double>(gaps.size());
    result.metadata["analysis_sr"] = static_cast<double>(config.analysis_sr);
    
    if (g_verbose) {
        verbose_section("Analysis Parameters");
        verbose("  Analysis sample rate: " + std::to_string(config.analysis_sr) + " Hz");
        verbose("  Min gap duration: " + std::to_string(config.min_gap_seconds) + "s");
        verbose("  Max gap duration: " + std::to_string(config.max_gap_seconds) + "s");
        verbose("  Confidence threshold: " + std::to_string(config.confidence_threshold));
    }
    
    return result;
}

} // namespace mwaac