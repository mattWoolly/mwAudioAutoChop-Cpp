#include "analysis.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace mwaac {

std::vector<float> compute_rms_energy(
    std::span<const float> samples,
    int sample_rate,
    int frame_length,
    int hop_length)
{
    if (samples.empty() || frame_length <= 0 || hop_length <= 0) {
        return {};
    }
    
    size_t num_frames = 1 + (samples.size() - frame_length) / hop_length;
    if (samples.size() < static_cast<size_t>(frame_length)) {
        num_frames = 1;  // Single frame for short signals
    }
    
    std::vector<float> rms(num_frames);
    
    for (size_t i = 0; i < num_frames; ++i) {
        size_t start = i * hop_length;
        size_t end = std::min(start + frame_length, samples.size());
        
        float sum_sq = 0.0f;
        for (size_t j = start; j < end; ++j) {
            sum_sq += samples[j] * samples[j];
        }
        
        rms[i] = std::sqrt(sum_sq / (end - start));
    }
    
    return rms;
}

std::vector<float> compute_zero_crossing_rate(
    std::span<const float> samples,
    int frame_length,
    int hop_length)
{
    if (samples.empty() || frame_length <= 0 || hop_length <= 0) {
        return {};
    }
    
    size_t num_frames = 1 + (samples.size() - frame_length) / hop_length;
    if (samples.size() < static_cast<size_t>(frame_length)) {
        num_frames = 1;
    }
    
    std::vector<float> zcr(num_frames);
    
    for (size_t i = 0; i < num_frames; ++i) {
        size_t start = i * hop_length;
        size_t end = std::min(start + frame_length, samples.size());
        
        int crossings = 0;
        for (size_t j = start + 1; j < end; ++j) {
            if ((samples[j] >= 0) != (samples[j-1] >= 0)) {
                crossings++;
            }
        }
        
        // Normalize to [0, 1]
        zcr[i] = static_cast<float>(crossings) / (end - start - 1);
    }
    
    return zcr;
}

// Note: Spectral flatness requires FFT - implement as TODO or basic version
std::vector<float> compute_spectral_flatness(
    std::span<const float> samples,
    int sample_rate,
    int frame_length,
    int hop_length)
{
    // TODO: Implement with FFT
    // For now, return zeros (placeholder)
    size_t num_frames = 1 + (samples.size() - frame_length) / hop_length;
    return std::vector<float>(num_frames, 0.5f);  // Placeholder
}

int64_t find_trailing_silence_start(
    std::span<const float> samples,
    int sample_rate,
    float threshold,
    float buffer_seconds)
{
    if (samples.empty()) {
        return 0;
    }
    
    // Frame parameters for RMS computation
    int frame_length = static_cast<int>(0.05f * sample_rate);  // 50ms frames
    int hop_length = frame_length / 4;  // 12.5ms hops
    
    // Auto-detect threshold from noise floor if not specified
    if (threshold < 0) {
        // Compute RMS of last 10% of samples to estimate noise floor
        size_t check_start = samples.size() * 9 / 10;
        float sum_sq = 0.0f;
        for (size_t i = check_start; i < samples.size(); ++i) {
            sum_sq += samples[i] * samples[i];
        }
        float tail_rms = std::sqrt(sum_sq / (samples.size() - check_start));
        
        // Also check first 5% for lead-in silence
        check_start = 0;
        size_t check_end = samples.size() / 20;
        sum_sq = 0.0f;
        for (size_t i = check_start; i < check_end; ++i) {
            sum_sq += samples[i] * samples[i];
        }
        float head_rms = std::sqrt(sum_sq / check_end);
        
        // Use the quieter of the two as noise floor estimate
        float noise_floor = std::min(tail_rms, head_rms);
        
        // Set threshold at 3x noise floor (allowing some margin)
        threshold = noise_floor * 3.0f;
        
        // Minimum threshold to avoid false positives on very quiet material
        threshold = std::max(threshold, 0.001f);
    }
    
    // Scan backwards from end to find where audio content stops
    // Use frame-based RMS to avoid being triggered by individual quiet samples
    int64_t buffer_samples = static_cast<int64_t>(buffer_seconds * sample_rate);
    
    // Start from the end and work backwards
    int64_t pos = static_cast<int64_t>(samples.size()) - frame_length;
    int64_t last_audio_pos = static_cast<int64_t>(samples.size()) - 1;
    int consecutive_silence_frames = 0;
    int required_silence_frames = 3;  // Require several consecutive silent frames
    
    while (pos > 0) {
        // Compute RMS for this frame
        float sum_sq = 0.0f;
        int64_t frame_end = std::min(pos + frame_length, static_cast<int64_t>(samples.size()));
        for (int64_t i = pos; i < frame_end; ++i) {
            sum_sq += samples[i] * samples[i];
        }
        float frame_rms = std::sqrt(sum_sq / (frame_end - pos));
        
        if (frame_rms < threshold) {
            consecutive_silence_frames++;
        } else {
            // Found audio content
            if (consecutive_silence_frames >= required_silence_frames) {
                // We had a run of silence - audio ends here
                last_audio_pos = pos + frame_length;
                break;
            }
            consecutive_silence_frames = 0;
            last_audio_pos = pos + frame_length;
        }
        
        pos -= hop_length;
    }
    
    // Add buffer for natural fade-out
    int64_t trim_pos = std::min(last_audio_pos + buffer_samples, 
                                 static_cast<int64_t>(samples.size()) - 1);
    
    return trim_pos;
}

} // namespace mwaac