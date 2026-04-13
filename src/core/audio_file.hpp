#pragma once
#include "audio_info.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <optional>

namespace mwaac {

enum class AudioError {
    FileNotFound,
    InvalidFormat,
    UnsupportedFormat,
    ReadError,
    WriteError,
    InvalidRange
};

// Simple expected-like wrapper
template<typename T>
struct Result {
    std::optional<T> value;
    std::optional<AudioError> error;
    
    bool ok() const noexcept { return value.has_value() && !error.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    
    T& operator*() { return *value; }
    const T& operator*() const { return *value; }
};

// Forward declarations
Result<AudioInfo> parse_wav_header(std::ifstream& file);
Result<AudioInfo> parse_aiff_header(std::ifstream& file);

class AudioFile {
public:
    static Result<AudioFile> open(const std::filesystem::path& path);
    const AudioInfo& info() const noexcept { return info_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    Result<std::vector<std::byte>> read_raw_samples(int64_t a, int64_t b) const;
    
private:
    std::filesystem::path path_;
    AudioInfo info_;
};

} // namespace mwaac