#pragma once
#include <vector>
#include <span>
#include <cstdint>
#include <filesystem>
#include <string>

#include "audio_file.hpp"

namespace mwaac {

// Audio samples as float32, always mono for analysis
struct AudioBuffer {
    std::vector<float> samples;
    int sample_rate{0};

    [[nodiscard]] double duration_seconds() const noexcept {
        return sample_rate > 0 ?
            static_cast<double>(samples.size()) / sample_rate : 0.0;
    }

    [[nodiscard]] std::span<const float> as_span() const noexcept {
        return samples;
    }
};

// Load audio file as mono float samples.
//
// Converts stereo to mono by averaging channels. Optionally resamples to
// `target_sample_rate` (0 = use native rate). On failure, returns an
// `AudioError` discriminating between FileNotFound (sf_open failed),
// ReadError (sf_readf_float read short), or ResampleError (reserved
// for future resampler error paths).
//
// M-14 unified the previous load-specific result wrapper and error
// enum into the single `Expected<T,E>` taxonomy in audio_file.hpp.
// The pre-M-14 enum's FileNotFound, InvalidFormat, and ReadError were
// already values in `AudioError`; the load-specific resample-error
// value was added to `AudioError` as `ResampleError` to preserve the
// distinguishing value with no information loss (option (a) in
// docs/m14-scope.md).
[[nodiscard]] Expected<AudioBuffer, AudioError> load_audio_mono(
    const std::filesystem::path& path,
    int target_sample_rate = 0  // 0 = use native rate
);

// Simple linear resampling (for analysis, not quality-critical).
//
// Failure modes (return `AudioError::ResampleError`):
//   - `input.sample_rate <= 0` — division by zero / non-positive ratio
//     would produce undefined output. This is the precondition guard for
//     the historical Mi-3 div-by-zero hazard.
//   - `output_size` overflow — when
//     `input.samples.size() * (target_rate / input.sample_rate)` is not
//     finite, is negative, or exceeds `size_t`'s representable range,
//     the unchecked double-to-size_t cast would produce a garbage
//     allocation size. Guarded explicitly.
//
// On success the returned buffer has `sample_rate == target_rate` and a
// `samples.size()` rounded down from the exact ratio.
[[nodiscard]] Expected<AudioBuffer, AudioError> resample_linear(
    const AudioBuffer& input, int target_rate);

} // namespace mwaac