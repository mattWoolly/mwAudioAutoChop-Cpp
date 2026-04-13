#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace mwaac {

struct AudioInfo {
    int sample_rate{0};
    int channels{0};
    int bits_per_sample{0};
    int64_t frames{0};          // Total sample frames
    std::string format;         // "WAV", "AIFF", etc.
    std::string subtype;        // "PCM_24", "FLOAT", etc.
    int64_t data_offset{0};     // Byte offset to sample data
    int64_t data_size{0};       // Size of sample data in bytes
    
    [[nodiscard]] int64_t bytes_per_frame() const noexcept {
        return channels * (bits_per_sample / 8);
    }
    
    [[nodiscard]] double duration_seconds() const noexcept {
        return sample_rate > 0 ? static_cast<double>(frames) / sample_rate : 0.0;
    }
};

} // namespace mwaac