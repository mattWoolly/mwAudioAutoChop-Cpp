#include "analysis.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace mwaac {

std::vector<float> compute_rms_energy(
    std::span<const float> samples,
    [[maybe_unused]] int sample_rate,
    int frame_length,
    int hop_length)
{
    if (samples.empty() || frame_length <= 0 || hop_length <= 0) {
        return {};
    }

    size_t num_frames = 1 + (samples.size() - static_cast<std::size_t>(frame_length)) / static_cast<std::size_t>(hop_length);
    if (samples.size() < static_cast<size_t>(frame_length)) {
        num_frames = 1;  // Single frame for short signals
    }

    std::vector<float> rms(num_frames);

    for (size_t i = 0; i < num_frames; ++i) {
        size_t start = i * static_cast<std::size_t>(hop_length);
        size_t end = std::min(start + static_cast<std::size_t>(frame_length), samples.size());

        float sum_sq = 0.0f;
        for (size_t j = start; j < end; ++j) {
            sum_sq += samples[j] * samples[j];
        }

        rms[i] = std::sqrt(sum_sq / static_cast<float>(end - start));
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
    
    size_t num_frames = 1 + (samples.size() - static_cast<std::size_t>(frame_length)) / static_cast<std::size_t>(hop_length);
    if (samples.size() < static_cast<size_t>(frame_length)) {
        num_frames = 1;
    }

    std::vector<float> zcr(num_frames);

    for (size_t i = 0; i < num_frames; ++i) {
        size_t start = i * static_cast<std::size_t>(hop_length);
        size_t end = std::min(start + static_cast<std::size_t>(frame_length), samples.size());

        int crossings = 0;
        for (size_t j = start + 1; j < end; ++j) {
            if ((samples[j] >= 0) != (samples[j-1] >= 0)) {
                crossings++;
            }
        }

        // Normalize to [0, 1]
        zcr[i] = static_cast<float>(crossings) / static_cast<float>(end - start - 1);
    }
    
    return zcr;
}

// Note: Spectral flatness requires FFT - implement as TODO or basic version
std::vector<float> compute_spectral_flatness(
    std::span<const float> samples,
    [[maybe_unused]] int sample_rate,
    int frame_length,
    int hop_length)
{
    // TODO: Implement with FFT
    // For now, return zeros (placeholder)
    size_t num_frames = 1 + (samples.size() - static_cast<std::size_t>(frame_length)) / static_cast<std::size_t>(hop_length);
    return std::vector<float>(num_frames, 0.5f);  // Placeholder
}

} // namespace mwaac