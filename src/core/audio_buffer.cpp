#include "audio_buffer.hpp"
#include <sndfile.h>
#include <cmath>
#include <limits>

namespace mwaac {

Expected<AudioBuffer, AudioError> load_audio_mono(
    const std::filesystem::path& path,
    int target_sample_rate)
{
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file) {
        return AudioError::FileNotFound;
    }

    // Read all samples as float
    std::vector<float> raw_samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_count_t read = sf_readf_float(file, raw_samples.data(), info.frames);
    sf_close(file);

    if (read != info.frames) {
        return AudioError::ReadError;
    }

    // Convert to mono if needed
    AudioBuffer buffer;
    buffer.sample_rate = info.samplerate;

    if (info.channels == 1) {
        buffer.samples = std::move(raw_samples);
    } else {
        // Average channels to mono
        buffer.samples.resize(static_cast<std::size_t>(info.frames));
        for (sf_count_t i = 0; i < info.frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < info.channels; ++ch) {
                sum += raw_samples[static_cast<std::size_t>(i * info.channels + ch)];
            }
            buffer.samples[static_cast<std::size_t>(i)] = sum / static_cast<float>(info.channels);
        }
    }
    
    // Resample if requested
    if (target_sample_rate > 0 && target_sample_rate != buffer.sample_rate) {
        auto resampled = resample_linear(buffer, target_sample_rate);
        if (!resampled) {
            return resampled.error();
        }
        buffer = std::move(resampled).value();
    }

    return buffer;
}

Expected<AudioBuffer, AudioError> resample_linear(
    const AudioBuffer& input, int target_rate)
{
    // Mi-3: precondition guard — a non-positive source rate would divide
    // by zero (or produce a non-positive ratio that the cast below would
    // either truncate to 0 or interpret as negative/garbage). Surface as
    // a typed error rather than UB.
    if (input.sample_rate <= 0) {
        return AudioError::ResampleError;
    }

    // Simple linear interpolation resampling
    // Good enough for analysis purposes
    double ratio = static_cast<double>(target_rate) / input.sample_rate;

    // Mi-3: guard the unchecked double-to-size_t cast on output_size.
    // If `input.samples.size() * ratio` is non-finite, negative, or
    // larger than size_t can represent, the cast result is unspecified.
    double expected_double =
        static_cast<double>(input.samples.size()) * ratio;
    if (!std::isfinite(expected_double) || expected_double < 0.0 ||
        expected_double > static_cast<double>(std::numeric_limits<size_t>::max())) {
        return AudioError::ResampleError;
    }
    size_t output_size = static_cast<size_t>(expected_double);

    AudioBuffer output;
    output.sample_rate = target_rate;
    output.samples.resize(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        double src_pos = static_cast<double>(i) / ratio;
        size_t src_idx = static_cast<size_t>(src_pos);
        double frac = src_pos - static_cast<double>(src_idx);

        if (src_idx + 1 < input.samples.size()) {
            output.samples[i] = static_cast<float>(
                static_cast<double>(input.samples[src_idx]) * (1.0 - frac)
                + static_cast<double>(input.samples[src_idx + 1]) * frac);
        } else if (src_idx < input.samples.size()) {
            output.samples[i] = input.samples[src_idx];
        }
    }

    return output;
}

} // namespace mwaac