#include "analysis.hpp"
#include "frame_sample_bridge.hpp"  // M-ANALYSIS-FRAME-SAMPLE-BRIDGE
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
        // M-ANALYSIS-FRAME-SAMPLE-BRIDGE: frame-index `i` → sample-index
        // `start` via the typed bridge. `compute_rms_energy` is the
        // upstream of every site already cured by the FrameIdx /
        // frame_to_sample family (analyze_blind_mode, detect_music_start,
        // estimate_noise_floor, compute_rms_envelope), so the type
        // discipline here closes the family at its root rather than only
        // at the leaves. Same RMS-frame semantics (12.5 ms hop at
        // analysis_sr) as the M-MUSIC-DETECT cure site; the existing
        // FrameIdx bridge applies directly.
        const size_t start = static_cast<std::size_t>(
            detail::frame_to_sample(detail::FrameIdx{i}, hop_length).value);
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
        // M-ANALYSIS-FRAME-SAMPLE-BRIDGE: same frame-index → sample-index
        // crossing as `compute_rms_energy` above; routed through the
        // typed bridge so the two halves of analysis.cpp share one
        // statement of intent.
        const size_t start = static_cast<std::size_t>(
            detail::frame_to_sample(detail::FrameIdx{i}, hop_length).value);
        size_t end = std::min(start + static_cast<std::size_t>(frame_length), samples.size());

        // M-10: per-frame degenerate-length guard. The normalization below
        // divides by `end - start - 1`, which is the count of adjacent-pair
        // comparisons performed in the inner loop. When `end - start < 2`
        // (i.e. the frame contains 0 or 1 samples — reachable, e.g., when
        // samples.size() == 1 and the short-signal branch above sets
        // num_frames = 1 with end == 1), the inner loop performs zero
        // iterations (crossings == 0) and the divisor is also 0, producing
        // 0.0f / 0.0f = NaN per IEEE-754. Pin the BACKLOG invariant
        // verbatim — "ZCR is defined as 0 for frames of length less than 2"
        // — at the per-frame granularity adjacent to the offending
        // divisor. A function-entry early-return on samples.size() < 2
        // would be coarser and would conflate frame-length with
        // input-length; the in-loop guard is the cleaner shape.
        if (end - start < 2) {
            zcr[i] = 0.0f;
            continue;
        }

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