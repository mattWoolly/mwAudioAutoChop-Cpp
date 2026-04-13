#include "audio_buffer.hpp"
#include <sndfile.h>
#include <cmath>

namespace mwaac {

LoadResult<AudioBuffer> load_audio_mono(
    const std::filesystem::path& path,
    int target_sample_rate) 
{
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file) {
        return LoadError::FileNotFound;
    }
    
    // Read all samples as float
    std::vector<float> raw_samples(info.frames * info.channels);
    sf_count_t read = sf_readf_float(file, raw_samples.data(), info.frames);
    sf_close(file);
    
    if (read != info.frames) {
        return LoadError::ReadError;
    }
    
    // Convert to mono if needed
    AudioBuffer buffer;
    buffer.sample_rate = info.samplerate;
    
    if (info.channels == 1) {
        buffer.samples = std::move(raw_samples);
    } else {
        // Average channels to mono
        buffer.samples.resize(info.frames);
        for (sf_count_t i = 0; i < info.frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < info.channels; ++ch) {
                sum += raw_samples[i * info.channels + ch];
            }
            buffer.samples[i] = sum / info.channels;
        }
    }
    
    // Resample if requested
    if (target_sample_rate > 0 && target_sample_rate != buffer.sample_rate) {
        buffer = resample_linear(buffer, target_sample_rate);
    }
    
    return buffer;
}

AudioBuffer resample_linear(const AudioBuffer& input, int target_rate) {
    // Simple linear interpolation resampling
    // Good enough for analysis purposes
    double ratio = static_cast<double>(target_rate) / input.sample_rate;
    size_t output_size = static_cast<size_t>(input.samples.size() * ratio);
    
    AudioBuffer output;
    output.sample_rate = target_rate;
    output.samples.resize(output_size);
    
    for (size_t i = 0; i < output_size; ++i) {
        double src_pos = i / ratio;
        size_t src_idx = static_cast<size_t>(src_pos);
        double frac = src_pos - src_idx;
        
        if (src_idx + 1 < input.samples.size()) {
            output.samples[i] = input.samples[src_idx] * (1.0f - frac) 
                              + input.samples[src_idx + 1] * frac;
        } else if (src_idx < input.samples.size()) {
            output.samples[i] = input.samples[src_idx];
        }
    }
    
    return output;
}

} // namespace mwaac