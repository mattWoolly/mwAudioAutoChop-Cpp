#include <algorithm>
#include <cstring>
#include <fstream>
#include <ios>
#include <iostream>

#include <sndfile.h>

#include "audio_file.hpp"

namespace mwaac {

// Magic bytes for audio formats
namespace magic {
static constexpr uint8_t RIFF[] = {'R', 'I', 'F', 'F'};
static constexpr uint8_t RF64[] = {'R', 'F', '6', '4'};
static constexpr uint8_t WAVE[] = {'W', 'A', 'V', 'E'};
static constexpr uint8_t FORM[] = {'F', 'O', 'R', 'M'};
static constexpr uint8_t AIFF[] = {'A', 'I', 'F', 'F'};
static constexpr uint8_t AIFC[] = {'A', 'I', 'F', 'C'};
static constexpr uint8_t COMM[] = {'C', 'O', 'M', 'M'};
static constexpr uint8_t SSND[] = {'S', 'S', 'N', 'D'};
static constexpr uint8_t fmt_[] = {'f', 'm', 't', ' '};
static constexpr uint8_t data[] = {'d', 'a', 't', 'a'};
static constexpr uint8_t ds64[] = {'d', 's', '6', '4'};
} // namespace magic

// Helper: compare bytes at position
static bool compare_bytes(const std::vector<uint8_t>& data, size_t offset, const uint8_t* magic, size_t len) {
    if (offset + len > data.size()) return false;
    return std::memcmp(data.data() + offset, magic, len) == 0;
}

static uint32_t read_le_u32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

static uint64_t read_le_u64(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 8 > data.size()) return 0;
    uint64_t lo = read_le_u32(data, offset);
    uint64_t hi = read_le_u32(data, offset + 4);
    return lo | (hi << 32);
}

static uint32_t read_be_u32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           (static_cast<uint32_t>(data[offset + 3]));
}

static uint16_t read_le_u16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

static uint16_t read_be_u16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return (static_cast<uint16_t>(data[offset]) << 8) |
           (static_cast<uint16_t>(data[offset + 1]));
}

// AudioFile implementation
AudioFile::AudioFile(const std::filesystem::path& path, AudioInfo info) noexcept
    : path_(path), info_(std::move(info)), valid_(true) {
}

AudioFile::AudioFile(AudioFile&& other) noexcept
    : path_(std::move(other.path_)), info_(std::move(other.info_)), valid_(other.valid_) {
    other.valid_ = false;
}

AudioFile& AudioFile::operator=(AudioFile&& other) noexcept {
    if (this != &other) {
        path_ = std::move(other.path_);
        info_ = std::move(other.info_);
        valid_ = other.valid_;
        other.valid_ = false;
    }
    return *this;
}

AudioFile::~AudioFile() = default;

// Detect format from magic bytes
enum class AudioFormat { Unknown, WAV, RF64, AIFF, AIFC };

static AudioFormat detect_format(const std::vector<uint8_t>& header) {
    if (header.size() >= 12) {
        if (compare_bytes(header, 0, magic::RIFF, 4) &&
            compare_bytes(header, 8, magic::WAVE, 4)) {
            return AudioFormat::WAV;
        }
        if (compare_bytes(header, 0, magic::RF64, 4) &&
            compare_bytes(header, 8, magic::WAVE, 4)) {
            return AudioFormat::RF64;
        }
        if (compare_bytes(header, 0, magic::FORM, 4)) {
            if (compare_bytes(header, 8, magic::AIFF, 4) ||
                compare_bytes(header, 8, magic::AIFC, 4)) {
                return compare_bytes(header, 8, magic::AIFF, 4) ? AudioFormat::AIFF : AudioFormat::AIFC;
            }
        }
    }
    return AudioFormat::Unknown;
}

Expected<AudioFile, AudioError> AudioFile::open(const std::filesystem::path& path) {
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        return Expected<AudioFile, AudioError>(AudioError::FileNotFound);
    }

    // Open file and read header
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Expected<AudioFile, AudioError>(AudioError::ReadError);
    }

    // Read first 64KB for header parsing
    constexpr size_t HEADER_SIZE = 65536;
    std::vector<uint8_t> header(HEADER_SIZE);
    file.read(reinterpret_cast<char*>(header.data()), HEADER_SIZE);
    if (!file) {
        return Expected<AudioFile, AudioError>(AudioError::ReadError);
    }
    header.resize(static_cast<size_t>(file.gcount()));

    // Detect format
    AudioFormat format = detect_format(header);
    if (format == AudioFormat::Unknown) {
        return Expected<AudioFile, AudioError>(AudioError::InvalidFormat);
    }

    // Parse header based on format
    auto info_result = (format == AudioFormat::WAV || format == AudioFormat::RF64)
        ? parse_wav_header(header)
        : parse_aiff_header(header);

    if (!info_result.has_value()) {
        return Expected<AudioFile, AudioError>(info_result.error());
    }

    AudioInfo info = std::move(info_result.value());

    // Cross-validate with libsndfile
    SF_INFO sf_info = {};
    auto* sf = sf_open(path.string().c_str(), SFM_READ, &sf_info);
    if (!sf) {
        return Expected<AudioFile, AudioError>(AudioError::ReadError);
    }
    sf_close(sf);

    // Override with libsndfile validation
    info.sample_rate = sf_info.samplerate;
    info.channels = sf_info.channels;
    info.frames = sf_info.frames;
    info.format = (format == AudioFormat::RF64) ? "RF64" : "WAV";
    if (format == AudioFormat::AIFF || format == AudioFormat::AIFC) {
        info.format = "AIFF";
    }

    // Get subtype string
    int format_minor = sf_info.format & SF_FORMAT_SUBMASK;
    switch (format_minor) {
        case SF_FORMAT_PCM_S8:
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_24:
        case SF_FORMAT_PCM_32:
            info.subtype = "PCM";
            break;
        case SF_FORMAT_FLOAT:
            info.subtype = "FLOAT";
            break;
        case SF_FORMAT_DOUBLE:
            info.subtype = "DOUBLE";
            break;
        case SF_FORMAT_ALAW:
            info.subtype = "ALAW";
            break;
        case SF_FORMAT_ULAW:
            info.subtype = "ULAW";
            break;
        default:
            info.subtype = "UNKNOWN";
            break;
    }

    return AudioFile(path, std::move(info));
}

Expected<std::vector<uint8_t>, AudioError>
AudioFile::read_raw_samples(int64_t offset, int64_t size) const {
    if (!valid_) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::ReadError);
    }
    if (offset < 0 || size < 0) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::InvalidRange);
    }
    if (offset + size > info_.data_size) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::InvalidRange);
    }

    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::ReadError);
    }

    file.seekg(static_cast<std::streamoff>(info_.data_offset + offset));
    if (!file) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::ReadError);
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file) {
        return Expected<std::vector<uint8_t>, AudioError>(AudioError::ReadError);
    }

    return buffer;
}

Expected<AudioInfo, AudioError> parse_wav_header(const std::vector<uint8_t>& data) {
    // Check minimum size
    if (data.size() < 44) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
    }

    AudioInfo info;
    info.format = "WAV";

    bool is_rf64 = compare_bytes(data, 0, magic::RF64, 4);
    if (is_rf64) {
        info.format = "RF64";
    }

    // Parse chunks
    int64_t data_offset = 0;
    int64_t data_size = 0;
    bool found_fmt = false;
    bool found_data = false;
    bool found_ds64 = false;
    uint64_t rf64_data_size = 0;

    size_t chunk_offset = 12;
    while (chunk_offset + 8 <= data.size()) {
        // Read chunk size
        uint32_t chunk_size = read_le_u32(data, chunk_offset + 4);
        size_t chunk_end = chunk_offset + 8 + chunk_size;

        // Check for fmt chunk
        if (compare_bytes(data, chunk_offset, magic::fmt_, 4)) {
            if (chunk_size >= 16) {
                uint16_t audio_format = read_le_u16(data, chunk_offset + 8);
                info.channels = read_le_u16(data, chunk_offset + 10);
                info.sample_rate = static_cast<int>(read_le_u32(data, chunk_offset + 12));
                // bytes_per_frame = read_le_u16(data, chunk_offset + 14);
                info.bits_per_sample = read_le_u16(data, chunk_offset + 16);

                // Check for supported formats
                if (audio_format == 1 ||    // PCM
                    audio_format == 3 ||    // IEEE float
                    audio_format == 6 ||    // A-law
                    audio_format == 7) {   // mu-law
                    found_fmt = true;
                } else {
                    return Expected<AudioInfo, AudioError>(AudioError::UnsupportedFormat);
                }
            }
        }
        // Check for data chunk
        else if (compare_bytes(data, chunk_offset, magic::data, 4)) {
            found_data = true;
            data_offset = static_cast<int64_t>(chunk_offset + 8);
            if (is_rf64) {
                rf64_data_size = chunk_size;
            } else {
                data_size = chunk_size;
            }
        }
        // Check for ds64 chunk (RF64)
        else if (compare_bytes(data, chunk_offset, magic::ds64, 4)) {
            found_ds64 = true;
            // ds64 contains 64-bit size info
            if (chunk_size >= 24) {
                rf64_data_size = read_le_u64(data, chunk_offset + 8);
                // skip other ds64 fields: data size, sample count
            }
        }

        // Move to next chunk
        chunk_offset = chunk_end;
        // Align to word boundary for RF64 style chunks
        if (chunk_size % 2 != 0 && chunk_offset < data.size()) {
            chunk_offset++;
        }
    }

    // Handle RF64 data size from ds64
    if (is_rf64 && found_ds64) {
        data_size = static_cast<int64_t>(rf64_data_size);
    }

    if (!found_fmt || !found_data) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
    }

    info.data_offset = data_offset;
    info.data_size = data_size;

    // Calculate frames
    if (info.bytes_per_frame() > 0) {
        info.frames = data_size / info.bytes_per_frame();
    }

    return info;
}

Expected<AudioInfo, AudioError> parse_aiff_header(const std::vector<uint8_t>& data) {
    // Check minimum size
    if (data.size() < 54) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
    }

    AudioInfo info;
    info.format = "AIFF";

    bool is_aifc = compare_bytes(data, 8, magic::AIFC, 4);

    // Parse chunks
    int64_t data_offset = 0;
    int64_t data_size = 0;
    bool found_comm = false;
    bool found_ssnd = false;

    size_t chunk_offset = 12;
    while (chunk_offset + 8 <= data.size()) {
        uint32_t chunk_size = read_be_u32(data, chunk_offset + 4);
        size_t chunk_end = chunk_offset + 8 + chunk_size;

        // Check for COMM chunk
        if (compare_bytes(data, chunk_offset, magic::COMM, 4)) {
            if (chunk_size >= 18) {
                info.channels = read_be_u16(data, chunk_offset + 8);
                // frames is a 10-byte float80 - skip for now
                info.bits_per_sample = read_be_u16(data, chunk_offset + 18);
                // sample_rate is a 10-byte float80 - skip for now
                // Just get 0 for now, libsndfile validates later
                found_comm = true;
            }
        }
        // Check for SSND chunk
        else if (compare_bytes(data, chunk_offset, magic::SSND, 4)) {
            found_ssnd = true;
            data_offset = static_cast<int64_t>(chunk_offset + 8);
            if (chunk_size >= 8) {
                // SSND has initial offset and block size
                data_size = static_cast<int64_t>(chunk_size - 8);
                data_offset += 8; // Skip the offset and block size fields
            }
        }

        // Move to next chunk
        chunk_offset = chunk_end;
        if (chunk_size % 2 != 0 && chunk_offset < data.size()) {
            chunk_offset++; // Pad byte
        }
    }

    if (!found_comm || !found_ssnd) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
    }

    info.data_offset = data_offset;
    info.data_size = data_size;

    if (is_aifc) {
        info.format = "AIFC";
    }

    return info;
}

} // namespace mwaac