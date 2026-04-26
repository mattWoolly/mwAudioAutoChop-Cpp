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
    
    size_t min_gap_frames = static_cast<size_t>(min_gap_seconds * static_cast<float>(sample_rate) / static_cast<float>(hop_length));
    size_t max_gap_frames = static_cast<size_t>(max_gap_seconds * static_cast<float>(sample_rate) / static_cast<float>(hop_length));
    
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
    std::vector<float> gap_samples(samples.begin() + static_cast<std::ptrdiff_t>(start_sample),
                                    samples.begin() + static_cast<std::ptrdiff_t>(end_sample));

    // Compute RMS of gap
    float sum_sq = 0.0f;
    for (float s : gap_samples) {
        sum_sq += s * s;
    }
    float gap_rms = std::sqrt(sum_sq / static_cast<float>(gap_samples.size()));
    
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
    VerboseTimer timer("blind mode analysis");
    
    // Load audio
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
    int frame_length = static_cast<int>(0.05f * static_cast<float>(config.analysis_sr));  // 50ms
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
    
    // Gap threshold: just above noise floor (6 dB)
    float threshold = noise_floor * 2.0f;
    
    if (g_verbose) {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(2) << noise_floor;
        std::ostringstream thresh_oss;
        thresh_oss << std::scientific << std::setprecision(2) << threshold;
        verbose("  Noise floor RMS: " + oss.str());
        verbose("  Gap threshold: " + thresh_oss.str() + " (6 dB above noise floor)");
    }
    
    // Find gaps
    verbose("Detecting gaps...");
    auto gaps = detect_gaps(rms, threshold, hop_length, config.analysis_sr,
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
    if (g_verbose) {
        verbose_section("Gap Detection Details");
    }
    
    for (const auto& gap : gaps) {
        int64_t track_start = static_cast<int64_t>(gap.second * static_cast<std::size_t>(hop_length));
        int64_t gap_duration = static_cast<int64_t>((gap.second - gap.first) * static_cast<std::size_t>(hop_length));

        // Score this gap
        float confidence = score_gap(audio.samples,
                                     gap.first * static_cast<std::size_t>(hop_length),
                                     gap.second * static_cast<std::size_t>(hop_length),
                                     config.analysis_sr,
                                     noise_floor);

        if (g_verbose) {
            [[maybe_unused]] double gap_start_sec = static_cast<double>(gap.first * static_cast<std::size_t>(hop_length)) / static_cast<double>(config.analysis_sr);
            double gap_duration_sec = static_cast<double>(gap_duration) / static_cast<double>(config.analysis_sr);
            std::ostringstream conf_oss;
            conf_oss << std::fixed << std::setprecision(3) << confidence;
            std::ostringstream dur_oss;
            dur_oss << std::fixed << std::setprecision(2) << gap_duration_sec;
            verbose("  Gap " + std::to_string(split_points.size()) + ":");
            verbose("    Frame range: " + std::to_string(gap.first) + " - " + std::to_string(gap.second));
            verbose("    Duration: " + dur_oss.str() + "s");
            verbose("    Confidence: " + conf_oss.str());
        }
        
        if (confidence >= config.confidence_threshold) {
            SplitPoint sp;
            sp.start_sample = track_start;
            sp.confidence = static_cast<double>(confidence);
            sp.source = "blind";
            sp.evidence["gap_start_frame"] = static_cast<double>(gap.first);
            sp.evidence["gap_end_frame"] = static_cast<double>(gap.second);
            split_points.push_back(sp);
        }
    }
    
    if (g_verbose) {
        verbose("  Valid gaps (above threshold): " + std::to_string(split_points.size() - 1));
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