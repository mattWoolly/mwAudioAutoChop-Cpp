#include "music_detection.hpp"
#include "analysis.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>
#include <numeric>

namespace mwaac {

float estimate_noise_floor(
    std::span<const float> samples,
    int sample_rate,
    float window_seconds)
{
    if (samples.empty()) return 0.0f;
    
    // Use 50ms frames with 25% hop
    int frame_length = static_cast<int>(0.05f * sample_rate);
    int hop_length = frame_length / 4;
    
    auto rms = compute_rms_energy(samples, sample_rate, frame_length, hop_length);
    if (rms.empty()) return 0.0f;
    
    // Sort RMS values and take the 10th percentile as noise floor estimate
    std::vector<float> sorted_rms = rms;
    std::sort(sorted_rms.begin(), sorted_rms.end());
    
    // Take the 10th percentile (or first element if too few)
    size_t percentile_idx = std::min(sorted_rms.size() / 10, sorted_rms.size() - 1);
    return sorted_rms[percentile_idx];
}

int64_t detect_music_start(
    std::span<const float> samples,
    int sample_rate,
    float min_music_seconds)
{
    if (samples.empty()) return 0;
    
    // Frame parameters: 50ms frame, 12.5ms hop
    int frame_length = static_cast<int>(0.05f * sample_rate);
    int hop_length = frame_length / 4;
    
    auto rms = compute_rms_energy(samples, sample_rate, frame_length, hop_length);
    if (rms.empty()) return 0;
    
    // Estimate noise floor
    float noise_floor = estimate_noise_floor(samples, sample_rate);
    if (noise_floor < 1e-10f) return 0;
    
    // Threshold: 12 dB above noise floor (factor of 4)
    float threshold = noise_floor * 4.0f;
    
    // Find frames above threshold
    std::vector<bool> is_music(rms.size());
    for (size_t i = 0; i < rms.size(); ++i) {
        is_music[i] = rms[i] > threshold;
    }
    
    // Find first sustained region of min_music_seconds
    int min_music_frames = static_cast<int>(min_music_seconds * sample_rate / hop_length);
    min_music_frames = std::max(1, min_music_frames);
    
    for (size_t i = 0; i + static_cast<size_t>(min_music_frames) <= is_music.size(); ++i) {
        bool all_music = true;
        for (int j = 0; j < min_music_frames; ++j) {
            if (!is_music[i + static_cast<size_t>(j)]) {
                all_music = false;
                break;
            }
        }
        
        if (all_music) {
            return static_cast<int64_t>(i) * hop_length;
        }
    }
    
    return 0;  // No sustained music found
}

float compute_otsu_threshold(std::span<const float> rms_values) {
    if (rms_values.empty()) return 0.0f;
    
    // Find min and max for histogram range
    float min_val = *std::min_element(rms_values.begin(), rms_values.end());
    float max_val = *std::max_element(rms_values.begin(), rms_values.end());
    
    if (max_val - min_val < 1e-10f) return min_val;
    
    // Build histogram with 256 bins
    constexpr int num_bins = 256;
    std::vector<int> histogram(num_bins, 0);
    float bin_width = (max_val - min_val) / num_bins;
    
    for (float val : rms_values) {
        int bin = static_cast<int>((val - min_val) / bin_width);
        bin = std::clamp(bin, 0, num_bins - 1);
        histogram[bin]++;
    }
    
    // Compute total mean
    float total = static_cast<float>(rms_values.size());
    float sum = 0.0f;
    for (int i = 0; i < num_bins; ++i) {
        sum += i * histogram[i];
    }
    
    // Otsu's method: find threshold that maximizes between-class variance
    float sum_b = 0.0f;
    float w_b = 0.0f;
    float max_variance = 0.0f;
    int best_threshold = 0;
    
    for (int t = 0; t < num_bins; ++t) {
        w_b += histogram[t];  // Weight of background
        if (w_b == 0) continue;
        
        float w_f = total - w_b;  // Weight of foreground
        if (w_f == 0) break;
        
        sum_b += t * histogram[t];
        
        float mean_b = sum_b / w_b;           // Mean of background
        float mean_f = (sum - sum_b) / w_f;   // Mean of foreground
        
        // Between-class variance
        float variance = w_b * w_f * (mean_b - mean_f) * (mean_b - mean_f);
        
        if (variance > max_variance) {
            max_variance = variance;
            best_threshold = t;
        }
    }
    
    // Convert bin index back to RMS value
    return min_val + (best_threshold + 0.5f) * bin_width;
}

float compute_auto_sensitivity(
    std::span<const float> rms_values,
    float noise_floor)
{
    if (rms_values.empty() || noise_floor < 1e-10f) return 0.5f;
    
    // Compute Otsu threshold
    float otsu_threshold = compute_otsu_threshold(rms_values);
    
    // Convert threshold to sensitivity
    // threshold = noise_floor * multiplier
    // multiplier = max_mult - (max_mult - min_mult) * sensitivity
    // sensitivity = (max_mult - multiplier) / (max_mult - min_mult)
    
    float multiplier = otsu_threshold / noise_floor;
    
    constexpr float min_mult = 1.5f;
    constexpr float max_mult = 10.0f;
    
    float sensitivity = (max_mult - multiplier) / (max_mult - min_mult);
    return std::clamp(sensitivity, 0.0f, 1.0f);
}

} // namespace mwaac