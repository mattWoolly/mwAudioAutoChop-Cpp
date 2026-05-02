#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

// Microsoft KSDATAFORMAT_SUBTYPE_* GUIDs for WAVE_FORMAT_EXTENSIBLE.
// 16-byte wire layout: little-endian Data1 (4) + little-endian Data2 (2)
// + little-endian Data3 (2) + raw Data4 (8). See Microsoft mmreg.h and
// the FIXTURE-WAVEEXT generator at tests/fixtures/waveext/generate.cpp.
//
// PCM:        00000001-0000-0010-8000-00aa00389b71
// IEEE float: 00000003-0000-0010-8000-00aa00389b71
static constexpr std::array<uint8_t, 16> KSDATAFORMAT_SUBTYPE_PCM = {
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
};
static constexpr std::array<uint8_t, 16> KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
};
} // namespace magic

// Head-window size for header parsing. AudioFile::open reads this many
// bytes from the start of the file, then for RF64 inputs splices on the
// last `kTailWindow` bytes. parse_wav_header's M-4 ds64 tail-scan uses
// kHeadSize as the lower bound of its scan range so head sample bytes
// cannot false-match the `ds64` fourcc. M-4-FU-TAILSCAN: lifted from a
// block-local constant inside AudioFile::open so both the splice site
// and the tail-scan reference the same value (no magic-number coupling).
static constexpr size_t kHeadSize = 65536;
static constexpr size_t kTailWindow = 1 * 1024 * 1024;  // 1 MiB

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
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[offset]) |
        (static_cast<uint16_t>(data[offset + 1]) << 8));
}

static uint16_t read_be_u16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(data[offset]) << 8) |
        static_cast<uint16_t>(data[offset + 1]));
}

// Decode a 10-byte IEEE 754 80-bit extended-precision big-endian value
// at `data[offset..offset+10)` into a non-negative integer (the AIFF
// sampleRate use case). Returns std::nullopt on:
//   * truncated input (fewer than 10 bytes available),
//   * negative sign,
//   * non-finite encoding (NaN/Inf — biased exponent = 0x7FFF),
//   * subnormal encoding (biased exponent = 0 with non-zero mantissa),
//   * non-integer value (mantissa low bits set after the integer part),
//   * value exceeding INT32_MAX (sample rates are stored as `int` in
//     AudioInfo — anything larger cannot be represented).
//
// Wire format (mirrors `encode_float80` documentation below):
//   byte 0: sign(1) | exp_high(7)
//   byte 1: exp_low(8)
//   bytes 2..9: 64-bit mantissa, MSB first, with the explicit leading
//               integer bit (so byte 2's top bit = 1 for finite normals).
//
// Algorithm: with biased exponent `b`, unbiased exponent e = b - 16383.
// The mantissa M is interpreted as a Q1.63 fixed-point number, so the
// real value is M * 2^(e - 63). For non-negative integer outputs this
// reduces to `value = M >> (63 - e)`, valid when 0 <= e <= 63 and the
// shifted-out low bits are zero.
static std::optional<uint64_t>
decode_float80_to_u64(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 10 > data.size()) return std::nullopt;

    const uint8_t b0 = data[offset];
    const uint8_t b1 = data[offset + 1];

    // Sign bit must be clear; AIFF sample rates are non-negative.
    if ((b0 & 0x80u) != 0) return std::nullopt;

    const uint16_t biased_exp =
        static_cast<uint16_t>((static_cast<uint16_t>(b0 & 0x7Fu) << 8) |
                               static_cast<uint16_t>(b1));

    uint64_t mantissa = 0;
    for (size_t i = 0; i < 8; ++i) {
        mantissa = (mantissa << 8) |
                   static_cast<uint64_t>(data[offset + 2 + i]);
    }

    // All-zero encoding -> 0.
    if (biased_exp == 0 && mantissa == 0) return uint64_t{0};

    // Subnormal (biased=0, mantissa!=0): not produced by encode_float80
    // for non-zero finite values; reject for parser strictness.
    if (biased_exp == 0) return std::nullopt;

    // Inf / NaN: biased exponent of all 1s.
    constexpr uint16_t kAllOnesExp = 0x7FFF;
    if (biased_exp == kAllOnesExp) return std::nullopt;

    constexpr int kBias = 16383;
    const int unbiased = static_cast<int>(biased_exp) - kBias;

    // Negative unbiased exponent: value < 1, can't be a positive integer.
    if (unbiased < 0) return std::nullopt;

    // Unbiased exponent > 63: value >= 2^64, overflows uint64_t.
    if (unbiased > 63) return std::nullopt;

    const int shift = 63 - unbiased;
    // Low `shift` bits of the mantissa are the fractional part; for an
    // integer result they must be zero.
    if (shift > 0) {
        const uint64_t frac_mask = (uint64_t{1} << shift) - 1;
        if ((mantissa & frac_mask) != 0) return std::nullopt;
    }
    const uint64_t value =
        (shift == 64) ? uint64_t{0} : (mantissa >> shift);
    return value;
}

// Convenience wrapper: decode + range-check against int32 (AudioInfo's
// `sample_rate` is `int`). Returns nullopt on overflow.
static std::optional<uint32_t>
decode_float80_to_u32(const std::vector<uint8_t>& data, size_t offset) {
    auto v = decode_float80_to_u64(data, offset);
    if (!v.has_value()) return std::nullopt;
    if (v.value() >
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(v.value());
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

    // Read first 64 KiB for header parsing (or entire file if smaller).
    //
    // M-4: for RF64 files, the ds64 chunk may appear AFTER the data chunk
    // (a layout permitted by EBU Tech 3306). For files larger than the
    // 64 KiB head, that trailing ds64 sits past the head window, so we
    // additionally splice the LAST kTailWindow bytes of the file onto the
    // buffer. parse_wav_header runs a tail-scan when the chunk walker
    // fails to find ds64 in the head, restricted to the spliced tail
    // region so it cannot false-match against in-head sample bytes.
    //
    // The trailer is at most 36 bytes (ds64 chunk header + body); 1 MiB
    // is overkill for the trailer alone but cheap and keeps the in-RAM
    // working set bounded regardless of file size.
    //
    // M-4-FU-TAILSCAN: kHeadSize and kTailWindow are file-scope constants
    // (see top of file). parse_wav_header references kHeadSize for its
    // tail-scan lower bound so the two sites cannot drift apart.
    std::vector<uint8_t> header(kHeadSize);
    file.read(reinterpret_cast<char*>(header.data()), kHeadSize);

    // Check for actual read errors (badbit), not just EOF (failbit)
    if (file.bad()) {
        return Expected<AudioFile, AudioError>(AudioError::ReadError);
    }
    header.resize(static_cast<size_t>(file.gcount()));

    // Minimum valid audio file size
    if (header.size() < 44) {
        return Expected<AudioFile, AudioError>(AudioError::InvalidFormat);
    }

    // Detect format from the head magic bytes.
    AudioFormat format = detect_format(header);
    if (format == AudioFormat::Unknown) {
        return Expected<AudioFile, AudioError>(AudioError::InvalidFormat);
    }

    // For RF64 inputs, append the file's tail bytes so parse_wav_header's
    // ds64 tail-scan can locate a trailing ds64 chunk. Skipped when the
    // file is small enough that the head already covers the whole file
    // (or the head + tail would overlap), and skipped for non-RF64.
    if (format == AudioFormat::RF64) {
        // file.read above set EOF if the file was smaller than kHeadSize,
        // which leaves the stream in a fail state. Clear it before seeking
        // to the tail.
        file.clear();
        // Determine total file size honestly.
        std::error_code size_ec;
        const auto file_size_u = std::filesystem::file_size(path, size_ec);
        if (size_ec) {
            return Expected<AudioFile, AudioError>(AudioError::ReadError);
        }
        // Only splice in a tail when there are file bytes that the head
        // window did not already cover.
        if (file_size_u > header.size()) {
            const std::uintmax_t uncovered = file_size_u - header.size();
            const size_t tail_len = (uncovered < kTailWindow)
                                        ? static_cast<size_t>(uncovered)
                                        : kTailWindow;
            const std::uintmax_t tail_offset = file_size_u - tail_len;
            file.seekg(static_cast<std::streamoff>(tail_offset));
            if (!file) {
                return Expected<AudioFile, AudioError>(AudioError::ReadError);
            }
            const size_t old_size = header.size();
            header.resize(old_size + tail_len);
            file.read(reinterpret_cast<char*>(header.data() + old_size),
                      static_cast<std::streamsize>(tail_len));
            if (file.bad()) {
                return Expected<AudioFile, AudioError>(AudioError::ReadError);
            }
            const auto got = static_cast<size_t>(file.gcount());
            if (got != tail_len) {
                // Tail short-read: file shrank between size query and read
                // (or some other I/O anomaly). InvalidFormat would over-
                // claim — the bytes-vs-claim mismatch is at the I/O layer.
                return Expected<AudioFile, AudioError>(AudioError::ReadError);
            }
        }
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
        // M-4-FU-LIBSNDFILE-GATE: libsndfile 1.2.2 returns "Unspecified
        // internal error" on RF64 ds64-after-data files even when the
        // project's parse_wav_header (M-4-cured) successfully recovers
        // data_offset / data_size from the head+tail splice. Returning
        // ReadError here would discard the parser-recovered AudioInfo —
        // closing the production-pipeline-scoped invariant gap surfaced
        // by M-4-FU-COVERAGE's pre-PR halt. For RF64, fall back to parser
        // truth: skip the libsndfile-success override block below and
        // continue with the AudioInfo populated by parse_wav_header.
        // Non-RF64 libsndfile-failure remains a real error.
        if (format != AudioFormat::RF64) {
            return Expected<AudioFile, AudioError>(AudioError::ReadError);
        }
        // Parser path already populates: format ("RF64"), channels,
        // sample_rate, bits_per_sample, data_offset, data_size, and
        // frames (= data_size / bytes_per_frame() when bytes_per_frame
        // is non-zero — see parse_wav_header tail). Default subtype to
        // "PCM": the only RF64 corpus that exercises this fallback today
        // is 16-bit PCM (rf64_ds64_after.wav); should a libsndfile-
        // rejected RF64 float/A-law/mu-law fixture surface, the parser's
        // fmt-chunk audio_format dispatch would need to thread the
        // subtype through AudioInfo (out of scope for this fix).
        info.subtype = "PCM";
    } else {
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
    bool ds64_truncated = false;  // ds64 found but chunk_size < 24 (M-4 hardening).
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
                } else if (audio_format == 0xFFFE) {
                    // WAVE_FORMAT_EXTENSIBLE — see Microsoft mmreg.h.
                    // Layout after the 16-byte legacy fmt fields:
                    //   uint16  cbSize                (must be >= 22)
                    //   uint16  wValidBitsPerSample
                    //   uint32  dwChannelMask
                    //   byte[16] SubFormat GUID
                    // Total extension size = 24 bytes (cbSize itself + 22).
                    // Bounds-check both the chunk_size and the underlying
                    // buffer before reading. A truncated EXTENSIBLE fmt
                    // chunk is a structural inconsistency the local check
                    // detects -> InvalidFormat (per parser-errors.md
                    // local-view rule).
                    if (chunk_size < 40) {
                        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
                    }
                    uint16_t cb_size = read_le_u16(data, fmt_data + 16);
                    if (cb_size < 22) {
                        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
                    }
                    // SubFormat GUID lives at fmt_data + 16 (cbSize) + 2
                    // + 2 (wValidBitsPerSample) + 4 (dwChannelMask) = +24.
                    size_t guid_offset = fmt_data + 24;
                    if (guid_offset + 16 > data.size()) {
                        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
                    }
                    bool guid_pcm = std::memcmp(data.data() + guid_offset,
                                                magic::KSDATAFORMAT_SUBTYPE_PCM.data(),
                                                16) == 0;
                    bool guid_ieee = std::memcmp(data.data() + guid_offset,
                                                 magic::KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.data(),
                                                 16) == 0;
                    if (guid_pcm || guid_ieee) {
                        // Inner format channels/sample_rate/bits-per-sample
                        // were already populated from the legacy fmt fields
                        // above, which is what WAVE_FORMAT_EXTENSIBLE
                        // requires for these SubFormats.
                        found_fmt = true;
                    } else {
                        // Well-formed EXTENSIBLE chunk, SubFormat we don't
                        // decode -> UnsupportedFormat (local-view rule).
                        return Expected<AudioInfo, AudioError>(AudioError::UnsupportedFormat);
                    }
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
            // M-4: in RF64, the data chunk_size is the 0xFFFFFFFF placeholder
            // when the real size is carried by ds64. Advancing the walker by
            // (8 + 0xFFFFFFFF) jumps it past `data.size()` so any chunks
            // following data (including a trailing ds64) are invisible. Stop
            // the walker here for RF64; the trailing-ds64 case is handled by
            // the tail-scan below. Non-RF64 inputs continue walking so any
            // post-data chunks (LIST, etc.) keep being skipped over as today.
            if (is_rf64 && chunk_size == 0xFFFFFFFFu) {
                break;
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
            } else {
                ds64_truncated = true;
            }
        }

        // Move to next chunk
        chunk_offset = chunk_end;
        // Align to word boundary for RF64 style chunks
        if (chunk_size % 2 != 0 && chunk_offset < data.size()) {
            chunk_offset++;
        }
    }

    // M-4 tail-scan: RF64 file with ds64 not yet seen. The ds64 trailer
    // sits past where the chunk walker can reach (the data chunk header
    // carries a 0xFFFFFFFF placeholder, so the walker would have to step
    // over 4 GiB of payload to find the trailer). AudioFile::open splices
    // the file's last 1 MiB onto the buffer for exactly this case; the
    // direct-call test path in tests/test_lossless.cpp does the same via
    // the rf64_read_full_with_tail helper.
    //
    // M-4-FU-TAILSCAN: the scan is restricted to the byte range
    // [kHeadSize, data.size() - 36] — i.e., only the spliced tail region.
    // A legitimate ds64-after-data trailer can only sit there; bytes at
    // positions < kHeadSize are inside the head window where sample bytes
    // could happen to spell `ds64` followed by a syntactically valid
    // 24-byte body. Excluding the head window eliminates that latent
    // false-match risk (audit-1 + audit-2 of M-4 / PR #35).
    //
    // Edge case: when data.size() <= kHeadSize (file smaller than the
    // head window — whole file already in buffer, no tail spliced), the
    // for-loop iterates zero times because kHeadSize > scan_end. This is
    // correct: a small RF64 file's ds64 should already be located by the
    // chunk walker in pass 1; if the walker missed it (e.g., 0xFFFFFFFF
    // data placeholder break), there is no real ds64 anywhere and the
    // post-loop "no ds64 found" path returns InvalidFormat.
    if (is_rf64 && !found_ds64 && found_data && data.size() >= 36) {
        const size_t scan_end = data.size() - 36;
        for (size_t i = kHeadSize; i <= scan_end; ++i) {
            if (compare_bytes(data, i, magic::ds64, 4)) {
                uint32_t tail_chunk_size = read_le_u32(data, i + 4);
                if (tail_chunk_size < 24) {
                    // ds64 found in tail but the body claim is too small
                    // to carry the data_size field. Structural inconsistency
                    // detected locally -> InvalidFormat (parser-errors.md
                    // local-view rule).
                    ds64_truncated = true;
                    break;
                }
                // Body must fit in the buffer.
                if (i + 8 + 24 > data.size()) {
                    ds64_truncated = true;
                    break;
                }
                rf64_data_size = read_le_u64(data, i + 16);
                found_ds64 = true;
                break;
            }
        }
    }

    // Handle RF64 data size from ds64
    if (is_rf64 && found_ds64) {
        data_size = static_cast<int64_t>(rf64_data_size);
    }

    // M-4 hardening: a truncated ds64 is a structural inconsistency the
    // local check detects -> InvalidFormat (parser-errors.md local-view
    // rule). Today this also catches the FIXTURE-MALFORMED rf64_ds64_truncated
    // entry; previously that fixture was rejected only incidentally because
    // the blob lacked a fmt/data chunk. The explicit check makes the
    // rejection robust to corpus changes.
    if (is_rf64 && ds64_truncated) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
    }

    // M-4 hardening: a well-formed RF64 always carries a ds64 chunk. After
    // both the head walker and the tail-scan have run, an RF64 file with
    // no ds64 is structurally inconsistent -> InvalidFormat.
    if (is_rf64 && !found_ds64) {
        return Expected<AudioInfo, AudioError>(AudioError::InvalidFormat);
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
                // COMM body layout (AIFF 1.3, "Audio IFF Specification",
                // Apple, 1989):
                //   body off | field            | bytes
                //   --------- ---------------- ------
                //   0        | numChannels     | 2
                //   2        | numSampleFrames | 4 (u32, *not* float80)
                //   6        | sampleSize      | 2 (bits per sample)
                //   8        | sampleRate      | 10 (IEEE 754 80-bit ext)
                // Body starts at chunk_offset + 8.
                info.channels = read_be_u16(data, chunk_offset + 8);
                // numSampleFrames lives at chunk_offset + 10 (u32 BE).
                // The parser does not currently expose this through
                // AudioInfo because libsndfile cross-validates `frames`
                // at AudioFile::open time; intentionally not decoded
                // here to keep the parser-side surface narrow.
                info.bits_per_sample =
                    read_be_u16(data, chunk_offset + 14);
                // Decode the 10-byte IEEE 80-bit extended sampleRate.
                // Failure (non-finite, negative, non-integer, out of
                // range) returns InvalidFormat per parser-errors.md.
                auto sr = decode_float80_to_u32(data, chunk_offset + 16);
                if (!sr.has_value()) {
                    return Expected<AudioInfo, AudioError>(
                        AudioError::InvalidFormat);
                }
                info.sample_rate = static_cast<int>(sr.value());
                found_comm = true;
            }
        }
        // Check for SSND chunk
        else if (compare_bytes(data, chunk_offset, magic::SSND, 4)) {
            found_ssnd = true;
            // SSND body layout (AIFF 1.3):
            //   u32 offset | u32 block_size | <offset bytes of pad> | sample bytes
            // Sample data begins at (SSND body start) + 8 + offset; the
            // offset value is alignment padding the writer may insert.
            // Available sample-byte budget is chunk_size - 8 - offset.
            data_offset = static_cast<int64_t>(chunk_offset + 8);
            if (chunk_size >= 8) {
                // Read the 4-byte big-endian SSND offset field. M-5: the
                // pre-fix code skipped past these bytes without reading
                // them; non-zero offsets silently mis-located data.
                const uint32_t ssnd_offset =
                    read_be_u32(data, chunk_offset + 8);
                // Reject if the offset alone exceeds the remaining chunk
                // body (would yield negative data_size). Local-view rule
                // per docs/decisions/parser-errors.md: structural
                // inconsistency the local check detects -> InvalidFormat.
                if (static_cast<uint64_t>(ssnd_offset) >
                    static_cast<uint64_t>(chunk_size - 8)) {
                    return Expected<AudioInfo, AudioError>(
                        AudioError::InvalidFormat);
                }
                data_size = static_cast<int64_t>(chunk_size - 8 - ssnd_offset);
                data_offset += 8; // Skip the offset and block_size fields
                data_offset += static_cast<int64_t>(ssnd_offset); // Skip alignment pad
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

// Build a sibling temp path next to `target` so that std::filesystem::rename
// will be filesystem-local (and therefore POSIX-atomic). The leading dot makes
// the temp file hidden on POSIX, and the .mwaac.tmp.<pid>.<rand> suffix makes
// it unmistakable as a transient artifact while staying unique enough that
// concurrent write_track calls in the same directory will not collide.
//
// Audit-1 caught a previous implementation that built the name via snprintf
// into a fixed 64-byte buffer; for filenames around 30+ chars that silently
// truncated the random suffix, causing sibling collisions and corrupt target
// files under concurrency. The current implementation builds the name in a
// std::ostringstream so length is unbounded by design.
//
// Returns std::nullopt if the resulting temp filename would exceed the
// filesystem's NAME_MAX (255 on POSIX), which would cause `open` / `rename`
// to fail with ENAMETOOLONG anyway and is treated as a WriteError.
static std::optional<std::filesystem::path>
make_temp_sibling_path(const std::filesystem::path& target) {
#ifdef _WIN32
    auto pid = static_cast<unsigned long>(_getpid());
#else
    auto pid = static_cast<unsigned long>(::getpid());
#endif
    static thread_local std::mt19937_64 rng{
        std::random_device{}() ^
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&pid))
    };
    uint64_t r = rng();

    std::string filename = target.filename().string();

    // Build the temp filename in a string — no fixed buffer, no truncation.
    std::ostringstream oss;
    oss << '.' << filename
        << ".mwaac.tmp." << pid
        << '.' << std::hex << std::setw(16) << std::setfill('0') << r;
    std::string temp_name = oss.str();

    // POSIX caps a single path component at NAME_MAX. <climits> does not
    // expose this portably, so hard-code 255 with a comment. (pathconf can
    // refine this per-mount, but 255 is the universal floor on every
    // mainstream filesystem we ship on.)
    constexpr std::size_t NAME_MAX_BYTES = 255;
    if (temp_name.size() > NAME_MAX_BYTES) {
        return std::nullopt;
    }

    std::filesystem::path parent = target.parent_path();
    if (parent.empty()) {
        parent = std::filesystem::path(".");
    }
    return parent / temp_name;
}

// ============================================================================
// Helper: Encode 80-bit IEEE extended precision float (for AIFF sample rate)
// ============================================================================

// IEEE 754 80-bit extended-precision wire format constants.
//
// References:
//   * Apple "Standard Apple Numeric Environment (SANE)" — defines the
//     extended80 layout used by AIFF.
//   * EBU Tech 3306 (BWF) Annex on AIFF, which reproduces the SANE
//     wire format diagram.
//   * AIFF 1.3 spec, "Audio IFF Specification", Apple, 1989, COMM chunk.
//
// Wire-format byte-layout invariant (the *only* layout the 10-byte
// output buffer can satisfy; reviewer should verify the code matches):
//
//   byte 0: sign bit (high) || top 7 bits of 15-bit biased exponent
//   byte 1:                    low  8 bits of 15-bit biased exponent
//   bytes 2..9: 64-bit mantissa, big-endian, *with the explicit leading 1 bit*
//
// Total: 1 + 1 + 8 = 10 bytes. There is no separate sign byte.
//
// Note: the older Motorola 6888x "1+2+8 = 11 byte" packed-decimal layout
// is *not* the AIFF wire format. The previous implementation followed
// that layout and overran a 10-byte buffer at out[10]; that defect is
// what this fix addresses (backlog item C-1).
namespace float80_layout {
    // 15-bit biased-exponent bias (per IEEE 754 80-bit / SANE / AIFF spec).
    static constexpr int kExponentBias = 16383;
    // Exponent field is 15 bits → max biased value 2^15 - 1 = 32767.
    static constexpr int kMaxBiasedExp = 32767;
    static constexpr int kMinBiasedExp = 1;
    // The exponent is split: high 7 bits live in the low 7 bits of byte 0
    // (alongside the sign), and the low 8 bits live in byte 1.
    static constexpr unsigned kExpHighShift = 8;
    static constexpr uint8_t  kExpHighMask  = 0x7F;
    static constexpr uint8_t  kSignMask     = 0x80;
    // Mantissa width.
    static constexpr int kMantissaBits = 64;
}

// Compile-time check that bias + 0 fits in the 15-bit exponent field.
static_assert(float80_layout::kExponentBias <= float80_layout::kMaxBiasedExp,
              "biased-zero exponent must fit in 15 bits");

// Encode a finite IEEE-754 80-bit extended-precision big-endian value
// into the 10-byte buffer pointed to by `out`.
//
// Inputs:
//   * `value`: must be finite. The AIFF use case (sample rate, frame count)
//     only ever passes non-negative values; this is asserted in Debug. The
//     negative-value branch is retained for completeness so callers that
//     want a signed encoder can rely on the wire format. NaN and ±∞ are
//     handled explicitly (NaN encodes as +0 — AIFF has no NaN sample rate;
//     callers must not pass NaN, which is asserted in Debug).
//
// Output:
//   * Writes *exactly* 10 bytes to `out`, in the byte layout described in
//     the `float80_layout` block above. No bytes beyond `out[9]` are touched.
//
// This function is the only place in the codebase that emits 80-bit
// extended floats. The inverse decoder (`decode_float80_to_u64` near
// the read-helpers above) is used by `parse_aiff_header` to recover the
// integer sampleRate from the COMM chunk; if you change the wire layout
// here, update the decoder in lockstep.
static void encode_float80(double value, std::byte* out) {
    using namespace float80_layout;

    assert(out != nullptr);
    // AIFF sample rates and frame counts are finite and non-negative;
    // we still encode negatives correctly for the general contract.
    assert(!std::isnan(value) && "encode_float80: NaN not supported");

    bool negative = false;
    if (value < 0.0) {
        negative = true;
        value = -value;
    }
    const uint8_t sign_bits = negative ? kSignMask : uint8_t{0};

    // Zero: all bytes zero except for a possible sign bit.
    if (value == 0.0) {
        out[0] = std::byte{sign_bits};
        for (int i = 1; i < 10; ++i) {
            out[i] = std::byte{0};
        }
        return;
    }

    // Infinity: exponent field is all 1s, mantissa explicit-leading-1 only.
    // Per SANE, +∞ is encoded with mantissa MSB set; we follow that here.
    if (std::isinf(value)) {
        out[0] = std::byte{static_cast<uint8_t>(sign_bits | kExpHighMask)};
        out[1] = std::byte{0xFF};
        out[2] = std::byte{0x80};
        for (int i = 3; i < 10; ++i) {
            out[i] = std::byte{0};
        }
        return;
    }

    // Decompose into exponent and mantissa.
    // frexp returns mant in [0.5, 1); shift to mantissa in [1, 2) and
    // adjust the exponent accordingly so the leading bit is the explicit
    // integer bit of the IEEE 80-bit mantissa.
    int exp = 0;
    double mant = std::frexp(value, &exp);  // mant ∈ [0.5, 1)
    mant *= 2.0;                            // mant ∈ [1, 2)
    exp -= 1;

    int biased_exp = exp + kExponentBias;
    // Clamp to representable range. (Beyond 80-bit precision we lose bits,
    // but for AIFF sample rates this never triggers.)
    if (biased_exp < kMinBiasedExp) biased_exp = kMinBiasedExp;
    if (biased_exp > kMaxBiasedExp) biased_exp = kMaxBiasedExp;

    // Build the 64-bit mantissa, MSB first, *including* the explicit
    // leading 1 bit (mant ∈ [1, 2) so the first iteration emits 1).
    //
    // The previous implementation pre-multiplied by 2 inside the loop,
    // which dropped the explicit leading bit and produced a mantissa
    // with the wrong bit pattern (e.g. 1.0 → all-ones). Order matters:
    // sample, then shift, then advance.
    uint64_t mantissa = 0;
    for (int i = 0; i < kMantissaBits; ++i) {
        const int bit = (mant >= 1.0) ? 1 : 0;
        if (bit) {
            mant -= 1.0;
        }
        mantissa = (mantissa << 1) | static_cast<uint64_t>(bit);
        mant *= 2.0;
    }

    // Pack per the byte-layout invariant above.
    const uint8_t exp_high = static_cast<uint8_t>(
        (static_cast<unsigned>(biased_exp) >> kExpHighShift) & kExpHighMask);
    const uint8_t exp_low  = static_cast<uint8_t>(
        static_cast<unsigned>(biased_exp) & 0xFFu);

    out[0] = std::byte{static_cast<uint8_t>(sign_bits | exp_high)};
    out[1] = std::byte{exp_low};
    out[2] = std::byte{static_cast<uint8_t>((mantissa >> 56) & 0xFFu)};
    out[3] = std::byte{static_cast<uint8_t>((mantissa >> 48) & 0xFFu)};
    out[4] = std::byte{static_cast<uint8_t>((mantissa >> 40) & 0xFFu)};
    out[5] = std::byte{static_cast<uint8_t>((mantissa >> 32) & 0xFFu)};
    out[6] = std::byte{static_cast<uint8_t>((mantissa >> 24) & 0xFFu)};
    out[7] = std::byte{static_cast<uint8_t>((mantissa >> 16) & 0xFFu)};
    out[8] = std::byte{static_cast<uint8_t>((mantissa >>  8) & 0xFFu)};
    out[9] = std::byte{static_cast<uint8_t>( mantissa        & 0xFFu)};
    // Invariant: exactly 10 bytes written (out[0] through out[9]).
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
    [[maybe_unused]] int bytes_per_frame = channels * bytes_per_sample;
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
    
    // numSampleFrames: AIFF spec defines this as a 4-byte big-endian
    // unsigned integer (the COMM chunk size of 18 already accounts for
    // 2+4+2+10 = 18 bytes; encoding it as 10-byte float80 would push the
    // remainder of the COMM payload out of the chunk and make libsndfile
    // (and any other conformant decoder) reject the file). See AIFF 1.3
    // spec, COMM chunk, "numSampleFrames" field.
    const uint32_t nsf = static_cast<uint32_t>(num_frames);
    header.push_back(std::byte{static_cast<uint8_t>((nsf >> 24) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((nsf >> 16) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>((nsf >>  8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>( nsf        & 0xFF)});

    // Bits per sample (big-endian)
    header.push_back(std::byte{static_cast<uint8_t>((bits_per_sample >> 8) & 0xFF)});
    header.push_back(std::byte{static_cast<uint8_t>(bits_per_sample & 0xFF)});

    // Sample rate as 80-bit float (big-endian, exactly 10 bytes)
    std::byte float80[10];
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
// ----------------------------------------------------------------------------
// Preconditions:
//   - `source` is an open AudioFile whose underlying file is readable.
//   - The sample range [start_sample, end_sample] is non-empty and within
//     [0, info.frames).
//   - `output_path`'s parent directory exists and is writable. The parent
//     directory must reside on the same filesystem the user expects the
//     output to land on (atomicity relies on a same-filesystem rename).
//
// Postconditions:
//   - On success: a complete file (valid header + sample bytes) exists at
//     `output_path`. No `.<name>.mwaac.tmp.*` sibling remains in the parent
//     directory for this call.
//   - On failure: `output_path` is unchanged from before the call. In
//     particular, no partial file is created at `output_path`, and any
//     temp sibling created during the attempt has been removed (best-effort
//     cleanup; see Caveats).
//
// Atomicity:
//   The function writes the full output to a temp-sibling path next to
//   `output_path`, closes the file, then commits via
//   std::filesystem::rename(temp, target). On a single filesystem this
//   rename is POSIX-atomic: a concurrent reader either sees the previous
//   state of `output_path` (or no file) or the fully-written new file —
//   never a partial write.
//
// Caveats:
//   - Cross-filesystem renames are not atomic. The temp sibling is placed
//     in the same directory as the target so this is the normal case;
//     however, mount points, bind mounts, or per-directory overlays could
//     cause `rename` to fall back to copy+unlink, which is not atomic.
//     If you need cross-filesystem atomicity, that's a separate concern.
//   - This function does NOT call fsync(2). std::ofstream::close flushes
//     the C++ stream buffer to the OS, but the OS may delay flushing to
//     disk. On a power loss between rename and OS flush, the file at
//     `output_path` may be zero-length or partially populated. The
//     "lossless by design" promise covers sample fidelity, not crash
//     safety; if a future caller needs crash-safe output, add fsync at
//     that point.
//
// Returns:
//   Expected<std::filesystem::path, AudioError>
//     - On success: the value is `output_path`.
//     - On failure:
//         InvalidRange — sample range out of bounds.
//         ReadError   — failed to read source samples.
//         WriteError  — could not open temp sibling, write failed,
//                       close/flush reported error, or rename to target
//                       failed (target is a directory, parent missing,
//                       permission denied, etc.).
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

    // ------------------------------------------------------------------
    // Atomic write: temp sibling -> close -> rename.
    // The target path is only ever created via rename of a fully-written
    // file. If anything goes wrong before rename, we remove the temp
    // sibling (best-effort) and leave `output_path` untouched.
    // ------------------------------------------------------------------
    auto temp_path_opt = make_temp_sibling_path(output_path);
    if (!temp_path_opt.has_value()) {
        // Target filename is so long that any sibling temp name would
        // exceed NAME_MAX. Refuse the write rather than rely on the
        // OS to surface ENAMETOOLONG mid-stream.
        return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
    }
    const std::filesystem::path temp_path = std::move(*temp_path_opt);

    auto cleanup_temp = [&]() noexcept {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        // best-effort: ignore ec
    };

    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            // Couldn't create the temp file at all (parent missing,
            // permission denied, etc.). Nothing to clean up beyond a
            // possible zero-byte file the OS may have left behind.
            cleanup_temp();
            return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
        }

        file.write(reinterpret_cast<const char*>(file_data.data()),
                   static_cast<std::streamsize>(file_data.size()));
        if (!file.good()) {
            cleanup_temp();
            return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
        }

        file.flush();
        if (!file.good()) {
            cleanup_temp();
            return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
        }

        file.close();
        if (file.fail()) {
            cleanup_temp();
            return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
        }
    }

    // Commit: same-filesystem rename is POSIX-atomic.
    std::error_code rename_ec;
    std::filesystem::rename(temp_path, output_path, rename_ec);
    if (rename_ec) {
        cleanup_temp();
        return Expected<std::filesystem::path, AudioError>(AudioError::WriteError);
    }

    return Expected<std::filesystem::path, AudioError>(output_path);
}

} // namespace mwaac