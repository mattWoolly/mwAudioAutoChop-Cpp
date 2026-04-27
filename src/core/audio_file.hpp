#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "audio_info.hpp"

namespace mwaac {

// Audio-related error types
enum class AudioError {
    FileNotFound,
    InvalidFormat,
    UnsupportedFormat,
    ReadError,
    WriteError,
    InvalidRange,
};

// Precondition contract macro.
//
// In Debug builds (NDEBUG not defined), expand to assert(cond) so the failure
// is reported with the standard libc++ assertion message and a SIGABRT.
// In Release builds, fall back to std::terminate() so a precondition violation
// is *still* a defined, noisy abort instead of silent UB. The branch is marked
// [[unlikely]] to keep the success path on the hot trace.
//
// Used by Expected<T,E>::value() / ::error() to enforce that callers must
// check has_value() / operator bool() before dereferencing. See
// docs/decisions/expected-api.md for why this lives on top of the existing
// placement-new storage rather than migrating to std::variant (M-14).
#if defined(NDEBUG)
#  define MWAAC_ASSERT_PRECONDITION(cond)               \
    do {                                                 \
        if (!(cond)) [[unlikely]] { std::terminate(); }  \
    } while (0)
#else
#  define MWAAC_ASSERT_PRECONDITION(cond) assert((cond))
#endif

// Simple expected type for C++20 compatibility (std::expected is C++23).
//
// Precondition contract: value() and error() are *unchecked accessors* in
// the sense that they do not return an error code; they require the caller
// to have already verified the discriminant via has_value() (or
// operator bool()). Calling value() on an errored Expected, or error() on
// a value-bearing Expected, is a contract violation and aborts the
// process — assert() in Debug, std::terminate() in Release. Violations
// are never silently undefined behaviour.
//
// The full migration to a std::variant<T,E>-backed implementation is
// tracked under M-14; see docs/decisions/expected-api.md for the C-2
// API-shape decision.
template<typename T, typename E>
class Expected {
public:
    Expected() = default;
    
    // Allow implicit conversion from T
    Expected(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : has_value_(true) {
        new (&storage_) T(value);
    }
    
    // Allow implicit conversion from T via move
    Expected(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(true) {
        new (&storage_) T(std::move(value));
    }
    
    // Allow conversion from E (error case)
    Expected(const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
        : has_value_(false) {
        new (&storage_) E(error);
    }
    
    // Allow conversion from E via move
    Expected(E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : has_value_(false) {
        new (&storage_) E(std::move(error));
    }
    
    Expected(const Expected& other)
        : has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(other.value());
        } else {
            new (&storage_) E(other.error());
        }
    }
    
    Expected(Expected&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(std::move(other.value()));
        } else {
            new (&storage_) E(std::move(other.error()));
        }
    }
    
    ~Expected() {
        if (has_value_) {
            value().~T();
        } else {
            error().~E();
        }
    }
    
    Expected& operator=(const Expected& other) {
        if (this == &other) return *this;
        reset();
        has_value_ = other.has_value_;
        if (has_value_) {
            new (&storage_) T(other.value());
        } else {
            new (&storage_) E(other.error());
        }
        return *this;
    }
    
    Expected& operator=(Expected&& other) {
        if (this == &other) return *this;
        reset();
        has_value_ = other.has_value_;
        if (has_value_) {
            new (&storage_) T(std::move(other.value()));
        } else {
            new (&storage_) E(std::move(other.error()));
        }
        return *this;
    }
    
    [[nodiscard]] bool has_value() const noexcept { return has_value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value_; }
    
    [[nodiscard]] const T& value() const& noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value_);
        return *reinterpret_cast<const T*>(&storage_);
    }

    [[nodiscard]] T& value() & noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value_);
        return *reinterpret_cast<T*>(&storage_);
    }

    [[nodiscard]] T&& value() && noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value_);
        return std::move(*reinterpret_cast<T*>(&storage_));
    }

    [[nodiscard]] const E& error() const& noexcept {
        MWAAC_ASSERT_PRECONDITION(!has_value_);
        return *reinterpret_cast<const E*>(&storage_);
    }

    [[nodiscard]] E&& error() && noexcept {
        MWAAC_ASSERT_PRECONDITION(!has_value_);
        return std::move(*reinterpret_cast<E*>(&storage_));
    }

private:
    void reset() {
        if (has_value_) {
            value().~T();
        } else {
            error().~E();
        }
    }
    
    alignas(T) alignas(E) unsigned char storage_[sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E)];
    bool has_value_;
};

// Audio file with header parsing support
class AudioFile {
public:
    explicit AudioFile(const std::filesystem::path& path, AudioInfo info) noexcept;

    // Prevent copying
    AudioFile(const AudioFile&) = delete;
    AudioFile& operator=(const AudioFile&) = delete;

    // Allow moving
    AudioFile(AudioFile&&) noexcept;
    AudioFile& operator=(AudioFile&&) noexcept;

    ~AudioFile();

    // Open an audio file and parse its header
    [[nodiscard]] static Expected<AudioFile, AudioError> open(const std::filesystem::path& path);

    // Accessors
    [[nodiscard]] const AudioInfo& info() const noexcept { return info_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // Read raw bytes from the sample data region
    [[nodiscard]] Expected<std::vector<uint8_t>, AudioError>
    read_raw_samples(int64_t offset, int64_t size) const;

private:
    std::filesystem::path path_;
    AudioInfo info_;
    bool valid_;
};

// Parse WAV/RF64 header from raw bytes
[[nodiscard]] Expected<AudioInfo, AudioError>
parse_wav_header(const std::vector<uint8_t>& data);

// Parse AIFF header from raw bytes
[[nodiscard]] Expected<AudioInfo, AudioError>
parse_aiff_header(const std::vector<uint8_t>& data);

// Export a track (raw byte copy with new header).
//
// Atomicity: the target path is updated atomically via a temp-sibling
// write followed by std::filesystem::rename. On any failure, no file is
// created at output_path. See the implementation in audio_file.cpp for
// full preconditions, postconditions, and the cross-filesystem caveat
// (atomicity assumes parent directory and temp sibling are on the same
// filesystem; fsync is intentionally not called — see docstring).
//
// Returns path to written file on success, or AudioError on failure.
[[nodiscard]] Expected<std::filesystem::path, AudioError>
write_track(
    const AudioFile& source,
    const std::filesystem::path& output_path,
    int64_t start_sample,
    int64_t end_sample,  // inclusive
    std::string_view output_format = ""  // empty = match source format
);

// Build a valid WAV header for the given parameters
[[nodiscard]] std::vector<std::byte> build_wav_header(
    int channels,
    int sample_rate,
    int bits_per_sample,
    int64_t data_size
);

// Build a valid AIFF header for the given parameters  
[[nodiscard]] std::vector<std::byte> build_aiff_header(
    int channels,
    int sample_rate,
    int bits_per_sample,
    int64_t num_frames,
    int64_t data_size
);

} // namespace mwaac