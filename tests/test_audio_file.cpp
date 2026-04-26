#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <cstdio>

#include "core/audio_file.hpp"

namespace {

// Create a temporary test WAV file for testing
[[maybe_unused]] std::filesystem::path create_test_wav() {
    auto temp_dir = std::filesystem::temp_directory_path();
    auto test_file = temp_dir / "test_audio_file.wav";
    
    // Use libsndfile to create a simple test file
    // For now, just check that the file doesn't exist
    // The actual file creation would use libsndfile
    
    return test_file;
}

} // namespace

TEST_CASE("AudioError enum values", "[audio_file]") {
    // Verify AudioError values exist
    CHECK(static_cast<int>(mwaac::AudioError::FileNotFound) >= 0);
    CHECK(static_cast<int>(mwaac::AudioError::InvalidFormat) >= 0);
    CHECK(static_cast<int>(mwaac::AudioError::UnsupportedFormat) >= 0);
    CHECK(static_cast<int>(mwaac::AudioError::ReadError) >= 0);
    CHECK(static_cast<int>(mwaac::AudioError::WriteError) >= 0);
    CHECK(static_cast<int>(mwaac::AudioError::InvalidRange) >= 0);
}

TEST_CASE("Expected basic operations", "[audio_file]") {
    // Test Expected with int and AudioError
    mwaac::Expected<int, mwaac::AudioError> expected_value(42);
    REQUIRE(expected_value.has_value());
    CHECK(expected_value.value() == 42);
    
    mwaac::Expected<int, mwaac::AudioError> expected_error(mwaac::AudioError::FileNotFound);
    CHECK(!expected_error.has_value());
    CHECK(expected_error.error() == mwaac::AudioError::FileNotFound);
}

TEST_CASE("AudioFile::open non-existent file", "[audio_file]") {
    auto result = mwaac::AudioFile::open("/nonexistent/path/to/file.wav");
    CHECK(!result.has_value());
    CHECK(result.error() == mwaac::AudioError::FileNotFound);
}

TEST_CASE("AudioInfo basic fields", "[audio_file]") {
    mwaac::AudioInfo info;
    CHECK(info.sample_rate == 0);
    CHECK(info.channels == 0);
    CHECK(info.bits_per_sample == 0);
    CHECK(info.frames == 0);
    CHECK(info.format.empty());
    CHECK(info.subtype.empty());
    CHECK(info.data_offset == 0);
    CHECK(info.data_size == 0);
}

TEST_CASE("AudioInfo bytes_per_frame", "[audio_file]") {
    mwaac::AudioInfo info;
    info.channels = 2;
    info.bits_per_sample = 16;
    CHECK(info.bytes_per_frame() == 4);
    
    info.channels = 1;
    info.bits_per_sample = 24;
    CHECK(info.bytes_per_frame() == 3);
    
    info.channels = 6;
    info.bits_per_sample = 32;
    CHECK(info.bytes_per_frame() == 24);
}

TEST_CASE("AudioInfo duration_seconds", "[audio_file]") {
    mwaac::AudioInfo info;
    info.sample_rate = 48000;
    info.frames = 48000;
    CHECK(info.duration_seconds() == 1.0);
    
    info.frames = 96000;
    CHECK(info.duration_seconds() == 2.0);
    
    info.sample_rate = 0;
    CHECK(info.duration_seconds() == 0.0);
}