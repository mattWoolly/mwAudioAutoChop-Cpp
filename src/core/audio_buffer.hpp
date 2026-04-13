#pragma once
#include <vector>
#include <span>
#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>

namespace mwaac {

enum class LoadError {
    FileNotFound,
    InvalidFormat,
    ReadError,
    ResampleError
};

// Simple expected-like wrapper for C++20 compatibility
template<typename T>
class LoadResult {
public:
    LoadResult() : error_(LoadError::ReadError) {}
    LoadResult(LoadError e) : error_(e) {}
    LoadResult(const T& v) : value_(v), error_(std::nullopt) {}
    LoadResult(T&& v) : value_(std::move(v)), error_(std::nullopt) {}
    
    bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    
    T& value() { return value_; }
    const T& value() const { return value_; }
    LoadError error() const { return error_.value(); }
    
private:
    T value_;
    std::optional<LoadError> error_;
};

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

// Load audio file as mono float samples
// Converts stereo to mono by averaging channels
// Optionally resamples to target sample rate
LoadResult<AudioBuffer> load_audio_mono(
    const std::filesystem::path& path,
    int target_sample_rate = 0  // 0 = use native rate
);

// Simple linear resampling (for analysis, not quality-critical)
AudioBuffer resample_linear(const AudioBuffer& input, int target_rate);

} // namespace mwaac