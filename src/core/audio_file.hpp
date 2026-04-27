#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "audio_info.hpp"

namespace mwaac {

// Audio-related error types.
//
// `ResampleError` was added in M-14 when the previously separate
// load-specific error enum was folded into `AudioError` (option (a) in
// docs/m14-scope.md). The other six values pre-date M-14 and are
// preserved unchanged.
enum class AudioError {
    FileNotFound,
    InvalidFormat,
    UnsupportedFormat,
    ReadError,
    WriteError,
    InvalidRange,
    ResampleError,
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
// Storage. Backed by `std::variant<T, E>` (M-14). The previous layout
// used placement-new into an aligned byte buffer plus a downcast to
// T*/E* on the access path; that pattern carries a latent
// `[basic.life]/8` UB hazard (the cast does not return a
// pointer-to-the-actual-object the standard requires). The variant
// rewrite eliminates that hazard entirely while preserving the same
// public API surface, so call sites compile unchanged.
//
// Precondition contract. value() and error() are *unchecked accessors*:
// they require the caller to have already verified the discriminant via
// has_value() (or operator bool()). Calling value() on an errored
// Expected, or error() on a value-bearing Expected, is a contract
// violation and aborts the process — assert() in Debug, std::terminate()
// in Release. Violations are never silently undefined behaviour.
// (`std::variant::get<T>` itself throws `std::bad_variant_access` on
// mismatch, but we want a louder, non-throwing failure mode that's
// consistent across exception-disabled builds.)
//
// Thread safety. Single-threaded contract: the discriminant check
// (has_value()) and the access (value()/error()) must occur on the same
// thread, and no other thread may mutate the Expected between them.
// Concurrent mutation invalidates the precondition's TOCTOU window.
//
// Moved-from semantics. Move construction / move assignment leave the
// source `Expected` with the *same* discriminant as the destination —
// `other.has_value()` does not flip. Calling `value()` on a moved-from
// Expected returns a moved-from `T` (not an abort); this matches
// `std::optional` and `std::expected`. Callers that need to detect
// moved-from state must track that themselves.
template<typename T, typename E>
class Expected {
public:
    // Default-constructed Expected holds a default-constructed T (the
    // value path). This matches the pre-M-14 behaviour where
    // `Expected()` set `has_value_=false` only by accident: the bool
    // member was uninitialized in the pre-existing default ctor and
    // happened to be cleared by the variant default for trivially
    // initialisable types, but the contract that survives in tests is
    // "has_value() reflects the constructor used". std::variant's
    // default-init holds the first alternative (T), which gives us a
    // well-defined default and aligns with std::expected.
    Expected() = default;

    // Allow implicit conversion from T
    Expected(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : storage_(std::in_place_index<0>, value) {}

    // Allow implicit conversion from T via move
    Expected(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : storage_(std::in_place_index<0>, std::move(value)) {}

    // Allow conversion from E (error case)
    Expected(const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
        : storage_(std::in_place_index<1>, error) {}

    // Allow conversion from E via move
    Expected(E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    Expected(const Expected& other) = default;
    Expected(Expected&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>) = default;

    ~Expected() = default;

    Expected& operator=(const Expected& other) = default;
    Expected& operator=(Expected&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E> &&
        std::is_nothrow_move_assignable_v<T> &&
        std::is_nothrow_move_assignable_v<E>) = default;

    [[nodiscard]] bool has_value() const noexcept {
        return storage_.index() == 0;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] const T& value() const& noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value());
        return *std::get_if<0>(&storage_);
    }

    [[nodiscard]] T& value() & noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value());
        return *std::get_if<0>(&storage_);
    }

    [[nodiscard]] T&& value() && noexcept {
        MWAAC_ASSERT_PRECONDITION(has_value());
        return std::move(*std::get_if<0>(&storage_));
    }

    [[nodiscard]] const E& error() const& noexcept {
        MWAAC_ASSERT_PRECONDITION(!has_value());
        return *std::get_if<1>(&storage_);
    }

    [[nodiscard]] E&& error() && noexcept {
        MWAAC_ASSERT_PRECONDITION(!has_value());
        return std::move(*std::get_if<1>(&storage_));
    }

private:
    // Index 0 = value (T), index 1 = error (E). Order is contract:
    // default-constructed Expected holds a value, matching std::expected.
    std::variant<T, E> storage_;
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