#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/audio_file.hpp"
#include <fstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <random>
#include <cstring>
#include <sndfile.h>

namespace fs = std::filesystem;

namespace {

// Read entire file to byte vector
[[maybe_unused]] std::vector<uint8_t> read_file_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Read raw bytes from a file region
std::vector<uint8_t> read_raw_bytes(const fs::path& path, size_t offset, size_t size) {
    std::ifstream file(path, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(offset));
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

// Create a valid WAV file using libsndfile with deterministic test data
bool create_test_wav(const fs::path& path, int channels, int sample_rate, int bits_per_sample, int64_t num_frames) {
    SF_INFO info = {};
    info.samplerate = sample_rate;
    info.channels = channels;
    info.frames = num_frames;
    
    // Determine format
    int format = SF_FORMAT_WAV;
    switch (bits_per_sample) {
        case 16: format |= SF_FORMAT_PCM_16; break;
        case 24: format |= SF_FORMAT_PCM_24; break;
        case 32: format |= SF_FORMAT_PCM_32; break;
        default: format |= SF_FORMAT_PCM_16; break;
    }
    info.format = format;
    
    SNDFILE* sf = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (!sf) return false;
    
    // Generate deterministic test pattern (alternating samples for easy verification)
    std::vector<int32_t> samples(static_cast<size_t>(num_frames * channels));
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int32_t> dist(-8388608, 8388607);  // 24-bit range
    
    for (auto& s : samples) {
        s = dist(rng);
    }
    
    // Write samples
    sf_count_t written = sf_write_int(sf, samples.data(), static_cast<sf_count_t>(samples.size()));
    sf_close(sf);
    
    return written == static_cast<sf_count_t>(samples.size());
}

// RAII cleanup helper
struct TempFile {
    fs::path path;
    explicit TempFile(const fs::path& p) : path(p) {}
    ~TempFile() { 
        std::error_code ec;
        fs::remove(path, ec); 
    }
};

} // anonymous namespace

// =============================================================================
// Header generation tests
// =============================================================================

TEST_CASE("WAV header generation is correct", "[lossless]") {
    auto header = mwaac::build_wav_header(2, 44100, 24, 44100 * 6);
    
    REQUIRE(header.size() == 44);
    
    // Verify RIFF header
    REQUIRE(header[0] == std::byte{'R'});
    REQUIRE(header[1] == std::byte{'I'});
    REQUIRE(header[2] == std::byte{'F'});
    REQUIRE(header[3] == std::byte{'F'});
    
    // Verify WAVE
    REQUIRE(header[8] == std::byte{'W'});
    REQUIRE(header[9] == std::byte{'A'});
    REQUIRE(header[10] == std::byte{'V'});
    REQUIRE(header[11] == std::byte{'E'});
    
    // Verify fmt
    REQUIRE(header[12] == std::byte{'f'});
    REQUIRE(header[13] == std::byte{'m'});
    REQUIRE(header[14] == std::byte{'t'});
    REQUIRE(header[15] == std::byte{' '});
    
    // Verify data
    REQUIRE(header[36] == std::byte{'d'});
    REQUIRE(header[37] == std::byte{'a'});
    REQUIRE(header[38] == std::byte{'t'});
    REQUIRE(header[39] == std::byte{'a'});
}

TEST_CASE("WAV header has correct parameters", "[lossless]") {
    // Test with specific values
    auto header = mwaac::build_wav_header(2, 48000, 24, 48000 * 6);
    
    REQUIRE(header.size() == 44);
    
    // Checks sample rate (bytes 24-27, little-endian)
    // 48000 = 0x0000BB80
    REQUIRE(header[24] == std::byte{0x80});
    REQUIRE(header[25] == std::byte{0xBB});
    REQUIRE(header[26] == std::byte{0x00});
    REQUIRE(header[27] == std::byte{0x00});
    
    // Check bits per sample (bytes 34-35, little-endian)
    // 24 = 0x0018
    REQUIRE(header[34] == std::byte{0x18});
    REQUIRE(header[35] == std::byte{0x00});
}

// Previously disabled with the [.] tag and the comment
// "temporarily disabled - investigate stack smash issue". Disabling the
// test masked a real live defect: encode_float80 writes 11 bytes into a
// 10-byte std::byte buffer (see C-1 in REMEDIATION_REPORT/BACKLOG).
// Re-enabled so the stack smash surfaces under ASan. Do not re-tag [.]
// without fixing the underlying encode_float80 overrun.
TEST_CASE("AIFF header has correct structure", "[lossless]") {
    // Use very small values for testing
    int64_t num_frames = 100;
    int64_t data_size = num_frames * 2 * 3;  // 100 frames * 2 channels * 3 bytes per sample
    
    auto header = mwaac::build_aiff_header(2, 44100, 24, num_frames, data_size);
    
    // Check FORM chunk
    REQUIRE(header[0] == std::byte{'F'});
    REQUIRE(header[1] == std::byte{'O'});
    REQUIRE(header[2] == std::byte{'R'});
    REQUIRE(header[3] == std::byte{'M'});
    
    // Check AIFF
    REQUIRE(header[8] == std::byte{'A'});
    REQUIRE(header[9] == std::byte{'I'});
    REQUIRE(header[10] == std::byte{'F'});
    REQUIRE(header[11] == std::byte{'F'});
    
    // Check COMM chunk
    REQUIRE(header[12] == std::byte{'C'});
    REQUIRE(header[13] == std::byte{'O'});
    REQUIRE(header[14] == std::byte{'M'});
    REQUIRE(header[15] == std::byte{'M'});
    
    // Check SSND chunk
    REQUIRE(header[38] == std::byte{'S'});
    REQUIRE(header[39] == std::byte{'S'});
    REQUIRE(header[40] == std::byte{'N'});
    REQUIRE(header[41] == std::byte{'D'});
}

// Re-enabled alongside the "has correct structure" case above. See the
// comment there for context on the encode_float80 defect this exposes.
TEST_CASE("AIFF header has correct parameters", "[lossless]") {
    int64_t num_frames = 48000;
    int64_t data_size = num_frames * 2 * 3;
    
    auto header = mwaac::build_aiff_header(2, 48000, 24, num_frames, data_size);
    
    // COMM chunk starts at byte 12
    // Channels at byte 22 (big-endian): 2 = 0x0002
    REQUIRE(header[22] == std::byte{0x00});
    REQUIRE(header[23] == std::byte{0x02});
    
    // Bits per sample at byte 32-33 (big-endian): 24 = 0x0018
    REQUIRE(header[32] == std::byte{0x00});
    REQUIRE(header[33] == std::byte{0x18});
}

// =============================================================================
// Lossless byte-copy tests
// =============================================================================

TEST_CASE("Lossless round-trip preserves exact bytes", "[lossless]") {
    // Create a test WAV file with known content
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test";
    fs::create_directories(test_dir);
    
    fs::path source_path = test_dir / "source.wav";
    fs::path output_path = test_dir / "output.wav";
    
    TempFile source_cleanup(source_path);
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);  // Note: only removes if empty
    
    // Create test file: stereo, 44100Hz, 24-bit, 1000 frames
    constexpr int channels = 2;
    constexpr int sample_rate = 44100;
    constexpr int bits_per_sample = 24;
    constexpr int64_t num_frames = 1000;
    
    REQUIRE(create_test_wav(source_path, channels, sample_rate, bits_per_sample, num_frames));
    
    // Open with AudioFile
    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());
    
    auto& audio_file = open_result.value();
    const auto& info = audio_file.info();
    
    // Verify file was opened correctly
    REQUIRE(info.channels == channels);
    REQUIRE(info.sample_rate == sample_rate);
    REQUIRE(info.frames == num_frames);
    
    // Extract a subset of samples (frames 100-500)
    constexpr int64_t start_sample = 100;
    constexpr int64_t end_sample = 500;
    constexpr int64_t extract_frames = end_sample - start_sample + 1;  // 401 frames
    
    // Write track
    auto write_result = mwaac::write_track(audio_file, output_path, start_sample, end_sample);
    REQUIRE(write_result.has_value());
    
    // Read raw sample data from source
    int64_t bytes_per_frame = info.bytes_per_frame();
    int64_t source_offset = info.data_offset + start_sample * bytes_per_frame;
    int64_t extract_bytes = extract_frames * bytes_per_frame;
    
    auto source_bytes = read_raw_bytes(source_path, static_cast<size_t>(source_offset), 
                                       static_cast<size_t>(extract_bytes));
    
    // Read raw sample data from output (after the 44-byte WAV header)
    auto output_bytes = read_raw_bytes(output_path, 44, static_cast<size_t>(extract_bytes));
    
    // Verify byte-for-byte match
    REQUIRE(source_bytes.size() == output_bytes.size());
    REQUIRE(source_bytes == output_bytes);
}

TEST_CASE("Lossless export handles full file extraction", "[lossless]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_full";
    fs::create_directories(test_dir);
    
    fs::path source_path = test_dir / "source_full.wav";
    fs::path output_path = test_dir / "output_full.wav";
    
    TempFile source_cleanup(source_path);
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);
    
    // Create test file
    constexpr int channels = 2;
    constexpr int sample_rate = 44100;
    constexpr int bits_per_sample = 24;
    constexpr int64_t num_frames = 500;
    
    REQUIRE(create_test_wav(source_path, channels, sample_rate, bits_per_sample, num_frames));
    
    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());
    
    auto& audio_file = open_result.value();
    const auto& info = audio_file.info();
    
    // Extract all samples
    auto write_result = mwaac::write_track(audio_file, output_path, 0, info.frames - 1);
    REQUIRE(write_result.has_value());
    
    // Read source data section
    auto source_bytes = read_raw_bytes(source_path, static_cast<size_t>(info.data_offset), 
                                       static_cast<size_t>(info.data_size));
    
    // Read output data section
    auto output_bytes = read_raw_bytes(output_path, 44, static_cast<size_t>(info.data_size));
    
    // Verify byte-for-byte match
    REQUIRE(source_bytes.size() == output_bytes.size());
    REQUIRE(source_bytes == output_bytes);
}

TEST_CASE("Lossless export rejects invalid sample range", "[lossless]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_range";
    fs::create_directories(test_dir);
    
    fs::path source_path = test_dir / "source_range.wav";
    fs::path output_path = test_dir / "output_range.wav";
    
    TempFile source_cleanup(source_path);
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);
    
    // Create small test file
    constexpr int64_t num_frames = 100;
    REQUIRE(create_test_wav(source_path, 2, 44100, 24, num_frames));
    
    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());
    
    auto& audio_file = open_result.value();
    
    // Test: end_sample beyond file length
    auto result1 = mwaac::write_track(audio_file, output_path, 0, num_frames + 100);
    REQUIRE_FALSE(result1.has_value());
    REQUIRE(result1.error() == mwaac::AudioError::InvalidRange);
    
    // Test: start_sample after end_sample
    auto result2 = mwaac::write_track(audio_file, output_path, 50, 20);
    REQUIRE_FALSE(result2.has_value());
    REQUIRE(result2.error() == mwaac::AudioError::InvalidRange);
    
    // Test: negative start_sample
    auto result3 = mwaac::write_track(audio_file, output_path, -1, 50);
    REQUIRE_FALSE(result3.has_value());
    REQUIRE(result3.error() == mwaac::AudioError::InvalidRange);
}

TEST_CASE("Lossless export with different bit depths", "[lossless]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_bits";
    fs::create_directories(test_dir);
    
    TempFile dir_cleanup(test_dir);
    
    for (int bits : {16, 24, 32}) {
        INFO("Testing bit depth: " << bits);
        
        fs::path source_path = test_dir / ("source_" + std::to_string(bits) + ".wav");
        fs::path output_path = test_dir / ("output_" + std::to_string(bits) + ".wav");
        
        TempFile source_cleanup(source_path);
        TempFile output_cleanup(output_path);
        
        constexpr int64_t num_frames = 256;
        REQUIRE(create_test_wav(source_path, 2, 44100, bits, num_frames));
        
        auto open_result = mwaac::AudioFile::open(source_path);
        REQUIRE(open_result.has_value());
        
        auto& audio_file = open_result.value();
        const auto& info = audio_file.info();
        
        // Extract middle portion
        constexpr int64_t start = 50;
        constexpr int64_t end = 200;
        
        auto write_result = mwaac::write_track(audio_file, output_path, start, end);
        REQUIRE(write_result.has_value());
        
        // Verify bytes match
        int64_t bytes_per_frame = info.bytes_per_frame();
        int64_t extract_bytes = (end - start + 1) * bytes_per_frame;
        
        auto source_bytes = read_raw_bytes(source_path, 
                                           static_cast<size_t>(info.data_offset + start * bytes_per_frame),
                                           static_cast<size_t>(extract_bytes));
        auto output_bytes = read_raw_bytes(output_path, 44, static_cast<size_t>(extract_bytes));
        
        REQUIRE(source_bytes.size() == output_bytes.size());
        REQUIRE(source_bytes == output_bytes);
    }
}

TEST_CASE("Output WAV header has correct metadata", "[lossless]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_header";
    fs::create_directories(test_dir);
    
    fs::path source_path = test_dir / "source_hdr.wav";
    fs::path output_path = test_dir / "output_hdr.wav";
    
    TempFile source_cleanup(source_path);
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);
    
    constexpr int channels = 2;
    constexpr int sample_rate = 48000;
    constexpr int bits_per_sample = 24;
    constexpr int64_t num_frames = 1000;
    
    REQUIRE(create_test_wav(source_path, channels, sample_rate, bits_per_sample, num_frames));
    
    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());
    
    // Extract 500 frames
    constexpr int64_t extract_frames = 500;
    auto write_result = mwaac::write_track(open_result.value(), output_path, 0, extract_frames - 1);
    REQUIRE(write_result.has_value());
    
    // Open output and verify metadata
    auto output_open = mwaac::AudioFile::open(output_path);
    REQUIRE(output_open.has_value());
    
    const auto& output_info = output_open.value().info();
    REQUIRE(output_info.channels == channels);
    REQUIRE(output_info.sample_rate == sample_rate);
    REQUIRE(output_info.frames == extract_frames);
}
