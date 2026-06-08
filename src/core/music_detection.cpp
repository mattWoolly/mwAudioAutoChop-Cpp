#include "music_detection.hpp"
#include "analysis.hpp"
#include "frame_sample_bridge.hpp"  // M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE
#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>
#include <numeric>

namespace mwaac {

// ─── Mi-5-MUSIC-DETECTION threshold catalog ────────────────────────
//
// Per the Mi-5 invariant "Every decision threshold is a `constexpr`
// at top of translation unit with a comment citing the observation
// or corpus that produced it." Mi-5-MUSIC-DETECTION extends Mi-5's
// strict reading to `src/core/music_detection.cpp`. The constants
// below are shared between `estimate_noise_floor` and
// `detect_music_start` — both implement the same envelope-analysis
// pipeline (50 ms frames at 25% hop) — so promoting to file scope
// also dedups two prior literal copies inside each function.

// Envelope-frame duration as a fraction of sample_rate. 50 ms gives
// one envelope sample per 50 ms of audio, matching the project's
// standard envelope granularity (compare `reference_mode.cpp`'s
// `kEnvelopeDefaultFrameMs = 50.0` and `blind_mode.cpp`'s
// `kBlindAnalysisFrameSeconds = 0.05f`).
static constexpr float kMusicDetAnalysisFrameSeconds = 0.05F;

// Hop length as a denominator of frame length — frame / 4 = 12.5 ms
// hop at 50 ms frame (75% frame overlap). The 25% hop is the
// project's standard analysis-rate hop ratio.
static constexpr int kMusicDetAnalysisHopFrameDenominator = 4;

// Percentile-of-RMS for the noise-floor estimate. 1/10 = 10th
// percentile sits in the silence band on any vinyl rip where silence
// is at least 10% of total duration (well within all realistic
// material). Caller-side cautioned by NEW-BLIND-GAP: on fixtures
// where silence dominates (>50% of signal), the 10th-percentile
// estimator may sit inside silence — see blind_mode's p90
// signal-reference cure for the score_gap path. The noise-floor
// estimator itself is unaffected; only the score_gap denominator
// needed re-anchoring.
static constexpr std::size_t kNoiseFloorPercentileDenominator = 10;

// Music-onset threshold expressed as a multiplier above the noise
// floor. 4× linear = +12.04 dB (20 * log10(4)). Frames whose RMS
// sits more than 12 dB above the noise floor count as music, not
// background. Compare `blind_mode.cpp`'s
// `kBlindGapThresholdNoiseFloorMultiplier = 2.0f` (+6 dB) — the
// blind-mode gap detector uses the lower 6 dB cutoff because it's
// labeling gap vs music; the music-onset detector uses 12 dB
// because it's labeling sustained-music vs leading silence (which
// can include vinyl surface noise above the noise floor).
static constexpr float kMusicOnsetNoiseFloorMultiplier = 4.0F;

float estimate_noise_floor(
    std::span<const float> samples,
    int sample_rate)
{
    if (samples.empty()) { return 0.0F;
}

    // Use 50ms frames with 25% hop
    int frame_length = static_cast<int>(kMusicDetAnalysisFrameSeconds * static_cast<float>(sample_rate));
    int hop_length = frame_length / kMusicDetAnalysisHopFrameDenominator;
    
    auto rms = compute_rms_energy(samples, sample_rate, frame_length, hop_length);
    if (rms.empty()) { return 0.0F;
}
    
    // Sort RMS values and take the 10th percentile as noise floor estimate
    std::vector<float> sorted_rms = rms;
    std::ranges::sort(sorted_rms);
    
    // Take the 10th percentile (or first element if too few; see
    // kNoiseFloorPercentileDenominator).
    size_t percentile_idx = std::min(sorted_rms.size() / kNoiseFloorPercentileDenominator,
                                     sorted_rms.size() - 1);
    return sorted_rms[percentile_idx];
}

int64_t detect_music_start(
    std::span<const float> samples,
    int sample_rate,
    float min_music_seconds)
{
    if (samples.empty()) { return 0;
}
    
    // Frame parameters: 50ms frame, 12.5ms hop
    int frame_length = static_cast<int>(kMusicDetAnalysisFrameSeconds * static_cast<float>(sample_rate));
    int hop_length = frame_length / kMusicDetAnalysisHopFrameDenominator;
    
    auto rms = compute_rms_energy(samples, sample_rate, frame_length, hop_length);
    if (rms.empty()) { return 0;
}
    
    // Estimate noise floor
    float noise_floor = estimate_noise_floor(samples, sample_rate);
    if (noise_floor < 1e-10F) { return 0;
}
    
    // Threshold: 12 dB above noise floor (factor of 4; see
    // kMusicOnsetNoiseFloorMultiplier).
    float threshold = noise_floor * kMusicOnsetNoiseFloorMultiplier;
    
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
            // M-MUSIC-DETECT-FRAME-SAMPLE-BRIDGE: cross the frame-index
            // → sample-index boundary through the typed bridge in
            // core/frame_sample_bridge.hpp. Pre-cure this was raw
            // `static_cast<int64_t>(i) * hop_length` — same untagged-
            // arithmetic shape M-6 cured in blind_mode. The bridge
            // guarantees `i` is treated as a frame index (not, say,
            // a same-typed loop iterator like `j` from the inner
            // for-loop, which would compile under the raw form but
            // produce wrong-by-(j-i) sample offsets).
            return detail::frame_to_sample(
                detail::FrameIdx{i}, hop_length).value;
        }
    }

    return 0;  // No sustained music found
}

} // namespace mwaac