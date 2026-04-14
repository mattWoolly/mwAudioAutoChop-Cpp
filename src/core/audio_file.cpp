#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>

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

    // Read first 64KB for header parsing (or entire file if smaller)
    constexpr size_t HEADER_SIZE = 65536;
    std::vector<uint8_t> header(HEADER_SIZE);
    file.read(reinterpret_cast<char*>(header.data()), HEADER_SIZE);
    
    // Check for actual read errors (badbit), not just EOF (failbit)
    if (file.bad()) {
        return Expected<AudioFile, AudioError>(AudioError::ReadError);
    }
    header.resize(static_cast<size_t>(file.gcount()));
    
    // Minimum valid audio file size
    if (header.size() < 44) {
        return Expected<AudioFile, AudioError>(AudioError::InvalidFormat);
    }

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
                // fmt chunk data starts at chunk_offset + 8 (after ID and size)
                size_t fmt_data = chunk_offset + 8;
                uint16_t audio_format = read_le_u16(data, fmt_data + 0);
                info.channels = read_le_u16(data, fmt_data + 2);
                info.sample_rate = static_cast<int>(read_le_u32(data, fmt_data + 4));
                // byte_rate at fmt_data + 8
                // block_align at fmt_data + 12
                info.bits_per_sample = read_le_u16(data, fmt_data + 14);

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
            // For RF64, data chunk size is 0xFFFFFFFF placeholder - actual size comes from ds64
            // For regular WAV, use the chunk size directly
            if (!is_rf64) {
                data_size = chunk_size;
            }
        }
        // Check for ds64 chunk (RF64)
        else if (compare_bytes(data, chunk_offset, magic::ds64, 4)) {
            found_ds64 = true;
            // ds64 chunk layout (after 8-byte header):
            //   bytes 0-7:   RIFF size (64-bit)
            //   bytes 8-15:  data size (64-bit) <-- this is what we need
            //   bytes 16-23: sample count (64-bit)
            // chunk_offset points to "ds64", so data size is at chunk_offset + 8 + 8 = +16
            if (chunk_size >= 24) {
                rf64_data_size = read_le_u64(data, chunk_offset + 16);
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

// ============================================================================
// Helper: Write bytes to file
// ============================================================================

static bool write_bytes_to_file(const std::filesystem::path& path, const std::vector<std::byte>& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return file.good();
}

// ============================================================================
// Helper: Encode 80-bit IEEE extended precision float (for AIFF sample rate)
// ============================================================================

static void encode_float80(double value, std::byte* out) {
    // IEEE 80-bit extended precision format:
    // 1 sign bit, 15 exponent bits (biased 16383), 64 mantissa bits
    // Normalized mantissa has leading 1 bit
    
    // Handle special cases
    bool negative = false;
    if (value < 0.0) {
        negative = true;
        value = -value;
    }
    
    if (value == 0.0 || value != value) {  // Zero or NaN
        // Zero: all zeros except possibly sign
        out[0] = std::byte{static_cast<uint8_t>(negative ? 0x80 : 0x00)};
        out[1] = std::byte{0};
        out[2] = std::byte{0};
        for (int i = 3; i < 10; ++i) {
            out[i] = std::byte{0};
        }
        return;
    }
    
    // Check for infinity
    if (value == std::numeric_limits<double>::infinity()) {
        // Infinity: exponent all 1s, mantissa all 0s
        out[0] = std::byte{static_cast<uint8_t>(negative ? 0x80 : 0x00)};
        out[1] = std::byte{0x7F};
        out[2] = std::byte{0xFF};
        for (int i = 3; i < 10; ++i) {
            out[i] = std::byte{0};
        }
        return;
    }
    
    // Get exponent and mantissa using frexp
    // frexp returns mant in [0.5, 1) and sets exp
    int exp = 0;
    double mant = std::frexp(value, &exp);  // mant is in [0.5, 1)
    mant *= 2.0;  // Now in [1, 2)
    exp--;
    
    // Bias exponent (16383). Note: exp can be negative for small values.
    // For finite values, exp is typically around log2(value) which is reasonable.
    int biased_exp = exp + 16383;
    
    // Clamp to valid range (1-32767)
    if (biased_exp < 1) biased_exp = 1;
    if (biased_exp > 32767) biased_exp = 32767;
    
    // Build mantissa (64 bits)
    // The mantissa is normally in [1, 2), but the leading 1 is implicit in IEEE754
    // In IEEE 80-bit, we include the explicit leading bit
    uint64_t mantissa = 0;
    for (int i = 0; i < 64; ++i) {
        mant *= 2.0;
        int bit = (mant >= 1.0) ? 1 : 0;
        if (bit) {
            mant -= 1.0;
        }
        mantissa = (mantissa << 1) | static_cast<uint64_t>(bit);
    }
    
    // Write bytes (big-endian)
    out[0] = std::byte{static_cast<uint8_t>(negative ? 0x80 : 0x00)};
    out[1] = std::byte{static_cast<uint8_t>((biased_exp >> 8) & 0xFF)};
    out[2] = std::byte{static_cast<uint8_t>(biased_exp & 0xFF)};
    out[3] = std::byte{static_cast<uint8_t>((mantissa >> 56) & 0xFF)};
    out[4] = std::byte{static_cast<uint8_t>((mantissa >> 48) & 0xFF)};
    out[5] = std::byte{static_cast<uint8_t>((mantissa >> 40) & 0xFF)};
    out[6] = std::byte{static_cast<uint8_t>((mantissa >> 32) & 0xFF)};
    out[7] = std::byte{static_cast<uint8_t>((mantissa >> 24) & 0xFF)};
    out[8] = std::byte{static_cast<uint8_t>((mantissa >> 16) & 0xFF)};
    out[9] = std::byte{static_cast<uint8_t>((mantissa >> 8) & 0xFF)};
    out[10] = std::byte{static_cast<uint8_t>(mantissa & 0xFF)};
}

// ============================================================================
// build_wav_header: Build a valid RIFF/WAV header (little-endian)
// ============================================================================

std::vector<std::byte> build_wav_header(
    int channels,
    int sample_rate,
    int bits_per_sample,
    int64_t data_size
) {
    // WAV header structure (44 bytes for standard PCM):
    // - RIFF header: "RIFF" + file size - 8
    // - WAVE format: "WAVE"
    // - fmt chunk: "fmt " + 16 + format + channels + sample_rate + bytes/sec + block align + bits/sample
    // - data chunk: "data" + data size
    
    std::vector<std::byte> header;
    header.reserve(44);
    
    int bytes_per_sample = (bits_per_sample + 7) / 8;
    int bytes_per_frame = channels * bytes_per_sample;
    int bytes_per_second = sample_rate * bytes_per_frame;
    uint16_t block_align = static_cast<uint16_t>(bytes_per_frame);
    uint16_t audio_format = 1;  // PCM
    
    // RIFF header
    header.push_back(std::byte{'R'});
    header.push_back(std::byte{'I'});
    header.push_back(std::byte{'F'});
    header.push_back(std::byte{'F'});
    
    // File size - 8 (little-endian)
    uint32_t file_size = static_cast<uint32_t>(data_size + 36);
    header.push_back(std::byte{static_cast<uint8_t>(file_size & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((file_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((file_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((file_size >> 24) & 0xFF)});
    
    // WAVE
    header.push_back(std::byte{'W'});
    header.push_back(std::byte{'A'});
    header.push_back(std::byte{'V'});
    header.push_back(std::byte{'E'});
    
    // fmt chunk
    header.push_back(std::byte{'f'});
    header.push_back(std::byte{'m'});
    header.push_back(std::byte{'t'});
    header.push_back(std::byte{' '});
    
    // fmt chunk size (16)
    uint32_t fmt_size = 16;
    header.push_back(std::byte{static_cast<uint8_t>(fmt_size & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((fmt_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((fmt_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((fmt_size >> 24) & 0xFF)});
    
    // Audio format (PCM = 1)
    header.push_back(std::byte{static_cast<uint8_t>(audio_format & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((audio_format >> 8) & 0xFF)});
    
    // Channels
    header.push_back(std::byte{static_cast<uint8_t>(channels & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((channels >> 8) & 0xFF)});
    
    // Sample rate
    header.push_back(std::byte{static_cast<uint8_t>(sample_rate & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((sample_rate >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((sample_rate >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((sample_rate >> 24) & 0xFF)});
    
    // Bytes per second
    header.push_back(std::byte{static_cast<uint8_t>(bytes_per_second & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((bytes_per_second >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((bytes_per_second >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((bytes_per_second >> 24) & 0xFF)});
    
    // Block align
    header.push_back(std::byte{static_cast<uint8_t>(block_align & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((block_align >> 8) & 0xFF)});
    
    // Bits per sample
    header.push_back(std::byte{static_cast<uint8_t>(bits_per_sample & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((bits_per_sample >> 8) & 0xFF)});
    
    // data chunk
    header.push_back(std::byte{'d'});
    header.push_back(std::byte{'a'});
    header.push_back(std::byte{'t'});
    header.push_back(std::byte{'a'});
    
    // Data size
    header.push_back(std::byte{static_cast<uint8_t>(data_size & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((data_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((data_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((data_size >> 24) & 0xFF)});
    
    return header;
}

// ============================================================================
// build_aiff_header: Build a valid AIFF header (big-endian)
// ============================================================================

std::vector<std::byte> build_aiff_header(
    int channels,
    int sample_rate,
    int bits_per_sample,
    int64_t num_frames,
    int64_t data_size
) {
    // AIFF header structure:
    // - FORM chunk: "FORM" + size + "AIFF"
    // - COMM chunk: "COMM" + 18 + channels + frames (10-byte float) + bits/sample + sample_rate (10-byte float80)
    // - SSND chunk: "SSND" + size + offset (4) + block_size (4) + data
    
    std::vector<std::byte> header;
    header.reserve(54);
    
    int bytes_per_sample = (bits_per_sample + 7) / 8;
    int bytes_per_frame = channels * bytes_per_sample;
    int64_t form_size = data_size + 46;  // COMM(26) + SSND(8) + data + 12 overhead
    int64_t ssnd_size = data_size + 8;  // includes offset and block size
    
    // FORM chunk
    header.push_back(std::byte{'F'});
    header.push_back(std::byte{'O'});
    header.push_back(std::byte{'R'});
    header.push_back(std::byte{'M'});
    
    // FORM size (big-endian)
    header.push_back(std::byte{static_cast<uint8_t>((form_size >> 24) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((form_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((form_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(form_size & 0xFF)});
    
    // AIFF
    header.push_back(std::byte{'A'});
    header.push_back(std::byte{'I'});
    header.push_back(std::byte{'F'});
    header.push_back(std::byte{'F'});
    
    // COMM chunk
    header.push_back(std::byte{'C'});
    header.push_back(std::byte{'O'});
    header.push_back(std::byte{'M'});
    header.push_back(std::byte{'M'});
    
    // COMM chunk size (18)
    uint32_t comm_size = 18;
    header.push_back(std::byte{static_cast<uint8_t>((comm_size >> 24) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((comm_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((comm_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(comm_size & 0xFF)});
    
    // Channels (big-endian)
    header.push_back(std::byte{static_cast<uint8_t>((channels >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(channels & 0xFF)});
    
    // Frames as 80-bit float (big-endian)
    std::byte float80[10];
    encode_float80(static_cast<double>(num_frames), float80);
    for (int i = 0; i < 10; ++i) {
        header.push_back(float80[i]);
    }
    
    // Bits per sample (big-endian)
    header.push_back(std::byte{static_cast<uint8_t>((bits_per_sample >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(bits_per_sample & 0xFF)});
    
    // Sample rate as 80-bit float (big-endian)
    encode_float80(static_cast<double>(sample_rate), float80);
    for (int i = 0; i < 10; ++i) {
        header.push_back(float80[i]);
    }
    
    // SSND chunk
    header.push_back(std::byte{'S'});
    header.push_back(std::byte{'S'});
    header.push_back(std::byte{'N'});
    header.push_back(std::byte{'D'});
    
    // SSND size (big-endian)
    header.push_back(std::byte{static_cast<uint8_t>((ssnd_size >> 24) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((ssnd_size >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((ssnd_size >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(ssnd_size & 0xFF)});
    
    // Offset (0)
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    
    // Block size (0)
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    header.push_back(std::byte{0});
    
    return header;
}

// ============================================================================
// write_track: Export a track with new header (raw byte copy)
// ============================================================================

Expected<std::filesystem::path, AudioError>
write_track(
    const AudioFile& source,
    const std::filesystem::path& output_path,
    int64_t start_sample,
    int64_t end_sample,
    std::string_view output_format
) {
    const AudioInfo& info = source.info();
    
    // Validate sample range
    if (start_sample < 0 || end_sample < start_sample || end_sample >= info.frames) {
        return Expected<std::filesystem::path, AudioError>(AudioError::InvalidRange);
    }
    
    // Calculate the number of frames
    int64_t num_frames = end_sample - start_sample + 1;
    int64_t bytes_to_read = num_frames * info.bytes_per_frame();
    
    // Read raw bytes from source
    int64_t byte_offset = start_sample * info.bytes_per_frame();
    auto raw_result = source.read_raw_samples(byte_offset, bytes_to_read);
    if (!raw_result.has_value()) {
        return Expected<std::filesystem::path, AudioError>(AudioError::ReadError);
    }
    
    // Determine output format
    std::string out_fmt = (output_format.empty()) ? info.format : std::string(output_format);
    
    // Build header based on format
    std::vector<std::byte> header;
    if (out_fmt == "WAV" || out_fmt == "RF64") {
        header = build_wav_header(info.channels, info.sample_rate, info.bits_per_sample, bytes_to_read);
    } else if (out_fmt == "AIFF" || out_fmt == "AIFC") {
        header = build_aiff_header(info.channels, info.sample_rate, info.bits_per_sample, num_frames, bytes_to_read);
    } else {
        // Default to WAV
        header = build_wav_header(info.channels, info.sample_rate, info.bits_per_sample, bytes_to_read);
    }
    
    // Combine header and data
    std::vector<std::byte> file_data;
    file_data.reserve(header.size() + raw_result.value().size());
    file_data.insert(file_data.end(), header.begin(), header.end());
    
    // Convert uint8_t vector to std::byte vector
    const auto& raw_bytes = raw_result.value();
    for (uint8_t b : raw_bytes) {
        file_data.push_back(static_cast<std::byte>(b));
    }
    
    // Write to file
    if (!write_bytes_to_file(output_path, file_data)) {
        return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
    }
    
    return Expected<std::filesystem::path, AudioError>(output_path);
}

} // namespace mwaac