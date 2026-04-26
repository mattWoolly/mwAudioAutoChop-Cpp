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
    [[maybe_unused]] float window_seconds)
{
    if (samples.empty()) return 0.0f;

    // Use 50ms frames with 25% hop
    int frame_length = static_cast<int>(0.05f * static_cast<float>(sample_rate));
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
    int frame_length = static_cast<int>(0.05f * static_cast<float>(sample_rate));
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
    int min_music_frames = static_cast<int>(min_music_seconds * static_cast<float>(sample_rate) / static_cast<float>(hop_length));
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

} // namespace mwaac