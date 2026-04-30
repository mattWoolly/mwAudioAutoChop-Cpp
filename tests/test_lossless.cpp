#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/audio_file.hpp"
#include <array>
#include <atomic>
#include <fstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <random>
#include <cstring>
#include <string>
#include <thread>
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

// AIFF header byte-layout reference (after C-1 fix; encode_float80 emits
// exactly 10 bytes per the IEEE 754 80-bit extended big-endian wire format
// described in Apple SANE / AIFF 1.3 spec). numSampleFrames is a 4-byte
// big-endian u32 per AIFF 1.3 COMM chunk definition:
//
//   [ 0.. 3] "FORM"
//   [ 4.. 7] form_size (big-endian u32)
//   [ 8..11] "AIFF"
//   [12..15] "COMM"
//   [16..19] comm chunk size (= 18, big-endian u32)
//   [20..21] channels (big-endian u16)
//   [22..25] numSampleFrames (big-endian u32)
//   [26..27] bits_per_sample (big-endian u16)
//   [28..37] sample_rate as 80-bit float (10 bytes)
//   [38..41] "SSND"
//   [42..45] ssnd_size
//   [46..49] offset (= 0)
//   [50..53] block_size (= 0)
//
// Previously this test was disabled with `[.]` and the comment
// "temporarily disabled - investigate stack smash issue". The crash was
// real: encode_float80 wrote 11 bytes into a 10-byte std::byte buffer
// (see C-1 in REMEDIATION_REPORT/BACKLOG). The original test assertions
// did not match the spec layout either — they straddled the buggy code's
// "frames-as-float80" mistake. Once encode_float80 emits exactly 10 bytes
// and numSampleFrames is restored to its spec-mandated u32, the SSND
// chunk lands at byte 38 — which is where the original test was *trying*
// to look. That intent is preserved here.
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

    // Check SSND chunk: starts at byte 38 (= 12 [COMM] + 8 [chunk hdr]
    // + 18 [comm_size payload]).
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

    // Channels at bytes 20-21 (big-endian): 2 = 0x0002
    REQUIRE(header[20] == std::byte{0x00});
    REQUIRE(header[21] == std::byte{0x02});

    // Bits per sample at bytes 26-27 (big-endian): 24 = 0x0018
    REQUIRE(header[26] == std::byte{0x00});
    REQUIRE(header[27] == std::byte{0x18});
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

// =============================================================================
// FIXTURE-WAVEEXT — data-byte round-trip from a 24-bit WAVE_FORMAT_EXTENSIBLE
// source. M-3 made AudioFile::open accept audio_format == 0xFFFE for the
// PCM and IEEE-float SubFormat GUIDs. write_track still emits a plain RIFF
// (PCM) header, not EXTENSIBLE — that header-format-lossy behavior is
// tracked separately as NEW-WAVEEXT-WRITE but is not gated by this test,
// which checks data-byte identity over the extracted region plus the
// re-opened output's metadata, not header-format identity.
// =============================================================================

#ifndef MWAAC_FIXTURE_WAVEEXT_DIR
#error "MWAAC_FIXTURE_WAVEEXT_DIR must be defined by tests/fixtures/waveext/CMakeLists.txt"
#endif

TEST_CASE("Lossless: 24-bit 2-ch extensible WAV round-trip preserves bytes",
          "[lossless][waveext]") {
    fs::path source_path =
        fs::path(MWAAC_FIXTURE_WAVEEXT_DIR) / "pcm_24bit_stereo.wav";
    REQUIRE(fs::exists(source_path));

    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_waveext";
    fs::create_directories(test_dir);
    fs::path output_path = test_dir / "output_waveext_stereo.wav";
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    auto& audio_file = open_result.value();
    const auto& info = audio_file.info();
    REQUIRE(info.channels == 2);
    REQUIRE(info.sample_rate == 48000);
    REQUIRE(info.bits_per_sample == 24);
    REQUIRE(info.frames == 48000);

    // Extract a deterministic mid-file region so we know we are
    // exercising the offset arithmetic, not just a from-zero copy.
    constexpr int64_t start_sample = 1000;
    constexpr int64_t end_sample   = 5000;
    constexpr int64_t extract_frames = end_sample - start_sample + 1;

    auto write_result = mwaac::write_track(audio_file, output_path,
                                           start_sample, end_sample);
    REQUIRE(write_result.has_value());

    int64_t bytes_per_frame = info.bytes_per_frame();
    int64_t source_offset   = info.data_offset + start_sample * bytes_per_frame;
    int64_t extract_bytes   = extract_frames * bytes_per_frame;

    auto source_data_bytes = read_raw_bytes(source_path,
                                            static_cast<size_t>(source_offset),
                                            static_cast<size_t>(extract_bytes));

    // The output must itself be a valid (re-openable) WAV whose data
    // section equals the source's extracted region byte-for-byte. We
    // re-open through AudioFile rather than assuming a 44-byte header,
    // because a correctly-written EXTENSIBLE output will have a larger
    // header than 44 bytes.
    auto output_open = mwaac::AudioFile::open(output_path);
    REQUIRE(output_open.has_value());
    const auto& out_info = output_open.value().info();
    CHECK(out_info.channels == 2);
    CHECK(out_info.sample_rate == 48000);
    CHECK(out_info.bits_per_sample == 24);
    CHECK(out_info.frames == extract_frames);

    auto output_data_bytes = read_raw_bytes(output_path,
                                            static_cast<size_t>(out_info.data_offset),
                                            static_cast<size_t>(extract_bytes));
    REQUIRE(source_data_bytes.size() == output_data_bytes.size());
    REQUIRE(source_data_bytes == output_data_bytes);
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

// =============================================================================
// FIXTURE-RF64 — RF64 corpus tests (INV-RF64-1, -2, -3)
//
// These tests consume artefacts produced by tests/fixtures/rf64/generate.cpp,
// laid out in the directory baked in at compile time as MWAAC_RF64_FIXTURE_DIR.
// See tests/fixtures/rf64/README.md for the layout. Two of these tests are
// known-failing today (C-3 and M-4) and are tagged [!shouldfail] — Catch2's
// primitive for "this assertion currently does not hold; failing is the
// expected outcome until the underlying bug is fixed". Disabling them
// would lose the regression coverage when C-3/M-4 land.
// =============================================================================
#ifdef MWAAC_RF64_FIXTURE_DIR
namespace {

// Read a single KEY=VALUE line out of manifest.txt. Comment lines start
// with '#'. We don't pull in a JSON/INI parser for ten keys — that would
// be the wrong end of the cost/benefit curve.
std::string rf64_manifest_get(const fs::path& manifest, const std::string& key) {
    std::ifstream f(manifest);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == key) {
            return line.substr(eq + 1);
        }
    }
    return {};
}

uint64_t rf64_manifest_u64(const fs::path& manifest, const std::string& key) {
    std::string v = rf64_manifest_get(manifest, key);
    REQUIRE_FALSE(v.empty());
    // Manifest stores plain decimal for sizes/offsets and 0xHEX for seeds.
    // We only call this on the decimal keys; hex would need std::stoull
    // with base 0 and is not needed by these tests.
    return std::stoull(v);
}

// Read the first 64 KiB of `path` to feed into parse_wav_header. The
// production code in AudioFile::open uses the same window size, so this
// faithfully reproduces what the parser sees in production.
std::vector<uint8_t> rf64_read_header_window(const fs::path& path) {
    constexpr size_t kHeaderWindow = 65536;
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    std::vector<uint8_t> buf(kHeaderWindow);
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(f.gcount()));
    return buf;
}

// Read the first 64 KiB of `path` *spliced* with the last 1 MiB of `path`,
// matching the buffer shape AudioFile::open feeds parse_wav_header for RF64
// inputs (M-4). The spliced buffer keeps the middle of the file out of RAM
// (so a 4 GiB+ fixture costs 1 MiB + 64 KiB to feed the parser, not 4 GiB)
// while still letting parse_wav_header's tail-scan see a trailing ds64
// chunk.
//
// Caveat for parser callers: bytes inside the spliced buffer at positions
// >= head_size do NOT correspond to file offsets equal to their buffer
// index. The chunk walker in parse_wav_header consumes only chunks that
// live inside the head (the data chunk header is always near the start of
// the file for well-formed WAV/RF64), so this is fine in practice; the
// tail-scan reads ds64 body fields by buffer offset, not by file offset.
std::vector<uint8_t> rf64_read_full_with_tail(const fs::path& path) {
    constexpr size_t kHeaderWindow = 65536;
    constexpr size_t kTailWindow = 1 * 1024 * 1024;  // 1 MiB

    std::error_code ec;
    const auto file_size_u = fs::file_size(path, ec);
    REQUIRE_FALSE(ec);

    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());

    // Head.
    std::vector<uint8_t> buf(kHeaderWindow);
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(f.gcount()));
    f.clear();  // file may be smaller than the header window; clear EOF.

    // Tail: last min(file_size - head_len, kTailWindow) bytes, if any.
    if (file_size_u > buf.size()) {
        const std::uintmax_t uncovered = file_size_u - buf.size();
        const size_t tail_len = (uncovered < kTailWindow)
                                    ? static_cast<size_t>(uncovered)
                                    : kTailWindow;
        const std::uintmax_t tail_offset = file_size_u - tail_len;
        f.seekg(static_cast<std::streamoff>(tail_offset));
        REQUIRE(f.good());
        const size_t old_size = buf.size();
        buf.resize(old_size + tail_len);
        f.read(reinterpret_cast<char*>(buf.data() + old_size),
               static_cast<std::streamsize>(tail_len));
        REQUIRE(static_cast<size_t>(f.gcount()) == tail_len);
    }
    return buf;
}

// ---- SHA-256, public-domain reference implementation ----
//
// We need a content-addressable comparator for the round-trip test. A raw
// std::vector<uint8_t> equality check would also work, but the invariant
// in BACKLOG.md ("Round-trip SHA-256 on sample region matches source") is
// phrased in terms of a hash, and using one keeps the failure message
// compact: a 64-character mismatch is easier to eyeball than two 1024-byte
// vectors. Implementation is verbatim from FIPS 180-4 §6.2.
//
// Adding a third-party SHA-256 dependency for this is forbidden by the
// task scope; a self-contained ~80-line implementation is the
// right-sized solution.
struct Sha256 {
    uint32_t h[8];
    uint64_t length_bits;
    uint8_t buffer[64];
    size_t buffer_len;

    static uint32_t rotr(uint32_t x, unsigned n) {
        return (x >> n) | (x << (32 - n));
    }

    void init() {
        // Initial hash values — first 32 bits of the fractional parts of the
        // square roots of the first eight primes (FIPS 180-4 §5.3.3).
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        length_bits = 0;
        buffer_len = 0;
    }

    void process_block(const uint8_t* block) {
        // Round constants (FIPS 180-4 §4.2.2): first 32 bits of the
        // fractional parts of the cube roots of the first 64 primes.
        static constexpr uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i*4+0]) << 24)
                 | (static_cast<uint32_t>(block[i*4+1]) << 16)
                 | (static_cast<uint32_t>(block[i*4+2]) <<  8)
                 |  static_cast<uint32_t>(block[i*4+3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2],  19) ^ (w[i-2]  >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* data, size_t n) {
        length_bits += static_cast<uint64_t>(n) * 8;
        while (n > 0) {
            size_t take = std::min(static_cast<size_t>(64) - buffer_len, n);
            std::memcpy(buffer + buffer_len, data, take);
            buffer_len += take;
            data += take;
            n -= take;
            if (buffer_len == 64) {
                process_block(buffer);
                buffer_len = 0;
            }
        }
    }

    std::string finish(uint64_t original_bits) {
        uint8_t pad = 0x80;
        // Buffer is in mid-update. We need to pad so that final block
        // length mod 64 == 56, then append original_bits big-endian.
        update(&pad, 1);
        uint8_t zero = 0;
        while (buffer_len != 56) {
            update(&zero, 1);
        }
        uint8_t lenbuf[8];
        for (int i = 0; i < 8; ++i) {
            lenbuf[i] = static_cast<uint8_t>((original_bits >> (56 - 8*i)) & 0xFFu);
        }
        // Direct copy into buffer; do not go through update() (which would
        // double-count the length).
        std::memcpy(buffer + 56, lenbuf, 8);
        buffer_len = 64;
        process_block(buffer);
        buffer_len = 0;

        char out[65];
        for (int i = 0; i < 8; ++i) {
            std::snprintf(out + i*8, 9, "%08x", h[i]);
        }
        out[64] = '\0';
        return std::string(out);
    }
};

std::string sha256_hex(const std::vector<uint8_t>& data) {
    Sha256 s;
    s.init();
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    s.update(data.data(), data.size());
    return s.finish(bits);
}

// Resolve the fixture directory path baked in by CMake.
fs::path rf64_fixture_dir() {
    return fs::path(MWAAC_RF64_FIXTURE_DIR);
}

}  // anonymous namespace

TEST_CASE("parse_wav_header: RF64 with ds64 before data", "[lossless][rf64]") {
    fs::path dir = rf64_fixture_dir();
    fs::path file = dir / "rf64_ds64_first.wav";
    fs::path manifest = dir / "manifest.txt";

    REQUIRE(fs::exists(file));
    REQUIRE(fs::exists(manifest));

    auto bytes = rf64_read_header_window(file);
    auto info_result = mwaac::parse_wav_header(bytes);
    REQUIRE(info_result.has_value());

    const auto& info = info_result.value();
    REQUIRE(info.format == "RF64");
    REQUIRE(info.channels == 2);
    REQUIRE(info.sample_rate == 48000);
    REQUIRE(info.bits_per_sample == 16);

    // Manifest reports the SAME data layout the generator wrote.
    int64_t expected_data_offset = static_cast<int64_t>(
        rf64_manifest_u64(manifest, "DS64_FIRST_DATA_OFFSET"));
    int64_t expected_data_size = static_cast<int64_t>(
        rf64_manifest_u64(manifest, "DATA_SIZE"));
    REQUIRE(info.data_offset == expected_data_offset);
    REQUIRE(info.data_size == expected_data_size);
}

// M-4 cure: parse_wav_header walks the head as before; when an RF64 file
// shows a `data` chunk with the 0xFFFFFFFF placeholder, the walker stops
// there and a tail-scan locates the trailing ds64 chunk. The helper above
// (rf64_read_full_with_tail) feeds the parser the head + last-1 MiB shape
// AudioFile::open uses in production. Keeping this assertion green is the
// positive check that M-4 closed its invariant; if it regresses, ds64
// recovery has broken again.
TEST_CASE("parse_wav_header: RF64 with ds64 after data",
          "[lossless][rf64]") {
    fs::path dir = rf64_fixture_dir();
    fs::path file = dir / "rf64_ds64_after.wav";
    fs::path manifest = dir / "manifest.txt";

    REQUIRE(fs::exists(file));
    REQUIRE(fs::exists(manifest));

    // Helper returns the head + last-1 MiB splice, matching what
    // AudioFile::open feeds parse_wav_header for RF64 inputs in
    // production. The trailing ds64 lives in the tail slice.
    auto bytes = rf64_read_full_with_tail(file);
    auto info_result = mwaac::parse_wav_header(bytes);
    REQUIRE(info_result.has_value());

    const auto& info = info_result.value();
    REQUIRE(info.format == "RF64");
    REQUIRE(info.channels == 2);
    REQUIRE(info.sample_rate == 48000);
    REQUIRE(info.bits_per_sample == 16);

    int64_t expected_data_offset = static_cast<int64_t>(
        rf64_manifest_u64(manifest, "DS64_AFTER_DATA_OFFSET"));
    int64_t expected_data_size = static_cast<int64_t>(
        rf64_manifest_u64(manifest, "DATA_SIZE"));
    REQUIRE(info.data_offset == expected_data_offset);
    REQUIRE(info.data_size == expected_data_size);
}

// [!shouldfail] — fails today because build_wav_header writes data_size
// as a uint32, truncating the 4 GiB+ payload range. The output file's
// header claims the wrong length, write_track silently produces a
// corrupt file, and the SHA-256 over Region B does not match the source.
TEST_CASE("RF64 round-trip: sample region byte-identical",
          "[lossless][rf64][!shouldfail]") {
    fs::path dir = rf64_fixture_dir();
    fs::path src = dir / "rf64_ds64_first.wav";
    fs::path manifest = dir / "manifest.txt";
    REQUIRE(fs::exists(src));
    REQUIRE(fs::exists(manifest));

    fs::path out_dir = fs::temp_directory_path() / "mwaac_rf64_roundtrip";
    fs::create_directories(out_dir);
    fs::path out = out_dir / "rf64_roundtrip_out.wav";
    TempFile out_cleanup(out);

    auto opened = mwaac::AudioFile::open(src);
    REQUIRE(opened.has_value());
    auto& af = opened.value();
    const auto& info = af.info();
    REQUIRE(info.format == "RF64");

    // Region B is the load-bearing one: it sits at 4 GiB exactly and
    // exercises the uint32 cliff. We round-trip it via write_track.
    const int64_t bpf = info.bytes_per_frame();
    REQUIRE(bpf > 0);

    const uint64_t region_b_offset =
        rf64_manifest_u64(manifest, "REGION_B_OFFSET");
    const uint64_t payload_bytes =
        rf64_manifest_u64(manifest, "PAYLOAD_BYTES");
    REQUIRE(region_b_offset % static_cast<uint64_t>(bpf) == 0);
    REQUIRE(payload_bytes % static_cast<uint64_t>(bpf) == 0);

    const int64_t start_frame =
        static_cast<int64_t>(region_b_offset) / bpf;
    const int64_t num_frames =
        static_cast<int64_t>(payload_bytes) / bpf;
    const int64_t end_frame = start_frame + num_frames - 1;

    auto wr = mwaac::write_track(af, out, start_frame, end_frame);
    REQUIRE(wr.has_value());

    // Source bytes for Region B: read directly from the fixture. data_offset
    // is the FIXTURE's data_offset, not the OUTPUT file's. Reading raw bytes
    // bypasses AudioFile so a 4 GiB seekg works on every platform.
    const uint64_t src_data_offset =
        rf64_manifest_u64(manifest, "DS64_FIRST_DATA_OFFSET");
    auto src_bytes = read_raw_bytes(src,
        static_cast<size_t>(src_data_offset + region_b_offset),
        static_cast<size_t>(payload_bytes));
    REQUIRE(src_bytes.size() == payload_bytes);

    // Output bytes: write_track lays the payload starting at the output's
    // data_offset. Re-open the output via AudioFile to discover that
    // offset honestly — if write_track produced an RF64 header, the
    // offset is past the ds64 chunk; if it produced a vanilla RIFF
    // (truncated; the C-3 bug), the offset is 44.
    auto out_open = mwaac::AudioFile::open(out);
    REQUIRE(out_open.has_value());
    const auto& out_info = out_open.value().info();
    const int64_t out_data_offset = out_info.data_offset;

    auto out_bytes = read_raw_bytes(out,
        static_cast<size_t>(out_data_offset),
        static_cast<size_t>(payload_bytes));

    // INV-RF64-3 has two halves and BOTH must hold:
    //
    //   (a) the named payload region survives byte-identical (source ==
    //       output bytes), and
    //   (b) RF64 input produces RF64 output (format identity preserved).
    //
    // Half (a) coincidentally passes today on small extracts because
    // write_track does a straight read+copy and the 1024-byte payload
    // does not strain the 32-bit data_size field. Half (b) is where C-3
    // actually shows up: build_wav_header always emits RIFF, never RF64,
    // and write_track passes the output through that one builder.
    //
    // Both assertions are kept so that:
    //   - if a future change to write_track breaks byte identity, this
    //     test catches it,
    //   - and the format-identity assertion is the one that drives the
    //     [!shouldfail] tag today.
    //
    // SHA-256 framing per BACKLOG.md ("Round-trip SHA-256 on sample region
    // matches source"). The hex digest renders compactly in failure logs.
    REQUIRE(out_bytes.size() == src_bytes.size());
    INFO("source SHA-256: " << sha256_hex(src_bytes));
    INFO("output SHA-256: " << sha256_hex(out_bytes));
    REQUIRE(sha256_hex(src_bytes) == sha256_hex(out_bytes));

    // Format identity: an RF64 source must produce an RF64 output.
    // build_wav_header today writes a fixed RIFF header, so this fails.
    REQUIRE(out_info.format == "RF64");
}

#endif  // MWAAC_RF64_FIXTURE_DIR

// =============================================================================
// encode_float80 wire-format tests (C-1)
// =============================================================================
//
// `encode_float80` is a static helper inside audio_file.cpp; we observe it
// indirectly through the 10-byte sample-rate slot of `build_aiff_header`,
// which lives at bytes 28..37 (see the AIFF byte-layout reference above).
// The expected byte patterns are the canonical IEEE 754 80-bit
// extended-precision big-endian encodings, taken from Apple SANE / the
// AIFF 1.3 spec.
//
// Layout reminder (10 bytes total):
//   byte 0: sign(1) || top 7 bits of 15-bit biased exponent
//   byte 1: low 8 bits of biased exponent
//   bytes 2..9: 64-bit big-endian mantissa, with explicit leading 1 bit
//   bias = 16383

namespace {

// Pull bytes 28..37 (the sample_rate float80 slot) out of an AIFF header
// built with the given sample rate. Frame count / bits / channels are
// arbitrary because we are only inspecting the float80 region.
std::array<uint8_t, 10> sample_rate_float80_bytes(int sample_rate) {
    auto header = mwaac::build_aiff_header(/*channels*/ 1,
                                            sample_rate,
                                            /*bits_per_sample*/ 16,
                                            /*num_frames*/ 4,
                                            /*data_size*/ 4 * 1 * 2);
    REQUIRE(header.size() >= 38);
    std::array<uint8_t, 10> out{};
    for (size_t i = 0; i < 10; ++i) {
        out[i] = static_cast<uint8_t>(header[28 + i]);
    }
    return out;
}

} // namespace

TEST_CASE("encode_float80: wire-format value 1.0", "[lossless][float80]") {
    // 1.0 in IEEE 754 80-bit extended:
    //   sign=0, biased_exp=16383=0x3FFF, mantissa=0x8000_0000_0000_0000
    //   Wire bytes: 3F FF 80 00 00 00 00 00 00 00
    // Source: Apple SANE / AIFF 1.3 spec, COMM sampleRate examples.
    //
    // We use sample_rate=1 to drive encode_float80 with the value 1.0;
    // the 10 encoded bytes live at offsets 28..37 of the AIFF header.
    auto bytes = sample_rate_float80_bytes(1);
    const std::array<uint8_t, 10> expected = {
        0x3F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    for (size_t i = 0; i < 10; ++i) {
        INFO("byte " << i);
        REQUIRE(bytes[i] == expected[i]);
    }
}

TEST_CASE("encode_float80: wire-format value 44100.0", "[lossless][float80]") {
    // 44100.0 in IEEE 754 80-bit extended:
    //   44100 = 0xAC44 = 1010 1100 0100 0100 (16 bits, MSB at bit 15)
    //   exp=15, biased=16398=0x400E
    //   mantissa with explicit leading 1 (left-justified into 64 bits)
    //     = 0xAC44_0000_0000_0000
    //   sign=0
    //   Wire bytes: 40 0E AC 44 00 00 00 00 00 00
    // Source: Apple SANE / cross-checked against libsndfile output.
    auto bytes = sample_rate_float80_bytes(44100);
    const std::array<uint8_t, 10> expected = {
        0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    for (size_t i = 0; i < 10; ++i) {
        INFO("byte " << i);
        REQUIRE(bytes[i] == expected[i]);
    }
}

TEST_CASE("AIFF sample-rate round-trip via libsndfile", "[lossless][float80]") {
    // End-to-end check: build an AIFF file with build_aiff_header at each
    // standard rate, write a tiny synthetic body, and have libsndfile
    // (an independent IEEE-754 80-bit decoder) read it back. Asserts the
    // sample rate it recovers matches what we encoded.
    //
    // This proves encode_float80 produces bytes that match the AIFF spec
    // as understood by a third-party implementation, not just the bit
    // pattern we computed by hand.
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_aiff_sr_roundtrip";
    fs::create_directories(test_dir);
    TempFile dir_cleanup(test_dir);

    constexpr int channels = 1;
    constexpr int bits_per_sample = 16;
    constexpr int64_t num_frames = 4;
    constexpr int64_t data_size = num_frames * channels * (bits_per_sample / 8);

    for (int rate : {44100, 48000, 88200, 96000, 176400, 192000}) {
        INFO("sample_rate = " << rate);

        fs::path path = test_dir / ("sr_" + std::to_string(rate) + ".aiff");
        TempFile cleanup(path);

        auto header = mwaac::build_aiff_header(channels, rate, bits_per_sample,
                                               num_frames, data_size);

        // Write header + a tiny zeroed body of `data_size` bytes so SSND
        // is well-formed.
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
        std::vector<char> body(static_cast<size_t>(data_size), '\0');
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
        REQUIRE(fs::file_size(path) == header.size() + body.size());

        SF_INFO info{};
        SNDFILE* sf = sf_open(path.string().c_str(), SFM_READ, &info);
        REQUIRE(sf != nullptr);
        const int sf_rate = info.samplerate;
        const int sf_channels = info.channels;
        sf_close(sf);

        REQUIRE(sf_rate == rate);
        REQUIRE(sf_channels == channels);
    }
}

// =============================================================================
// M-16: Atomic write — write_track must produce either a complete output
// file or no file at all at the target path. The implementation writes to a
// temp-sibling first and commits via std::filesystem::rename. Tests below
// exercise the success path (no temp leftovers) and two reproducible failure
// modes (missing parent dir, target path is a directory) to confirm that on
// failure the target is untouched and no .mwaac.tmp.* siblings linger.
// =============================================================================

namespace {

// Count temp-sibling artifacts that match the .mwaac.tmp.* naming scheme used
// by the atomic-write implementation. Uses directory_iterator over the parent
// directory; tolerates a missing directory by returning 0.
int count_mwaac_temp_siblings(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return 0;
    int count = 0;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (name.find(".mwaac.tmp.") != std::string::npos) {
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

TEST_CASE("write_track: success leaves only target file at output path", "[lossless][atomic]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_atomic_success";
    fs::create_directories(test_dir);

    fs::path source_path = test_dir / "source.wav";
    fs::path output_path = test_dir / "output.wav";

    TempFile source_cleanup(source_path);
    TempFile output_cleanup(output_path);
    TempFile dir_cleanup(test_dir);

    constexpr int64_t num_frames = 256;
    REQUIRE(create_test_wav(source_path, 2, 44100, 24, num_frames));

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    auto write_result = mwaac::write_track(open_result.value(), output_path,
                                           0, num_frames - 1);
    REQUIRE(write_result.has_value());

    // Target exists and is a regular file.
    REQUIRE(fs::exists(output_path));
    REQUIRE(fs::is_regular_file(output_path));

    // No temp-sibling artifacts remain.
    REQUIRE(count_mwaac_temp_siblings(test_dir) == 0);
}

TEST_CASE("write_track: failure leaves no file at output path (parent dir missing)",
          "[lossless][atomic]") {
    // Source must exist on disk; only the output path's parent is bogus.
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_atomic_missing_parent";
    fs::create_directories(test_dir);

    fs::path source_path = test_dir / "source.wav";

    // Output parent intentionally does not exist.
    fs::path bogus_parent = test_dir / "does_not_exist_subdir";
    fs::path output_path = bogus_parent / "output.wav";

    TempFile source_cleanup(source_path);
    TempFile dir_cleanup(test_dir);
    // Pre-condition: bogus_parent must not exist.
    {
        std::error_code ec;
        fs::remove_all(bogus_parent, ec);
    }
    REQUIRE_FALSE(fs::exists(bogus_parent));

    constexpr int64_t num_frames = 256;
    REQUIRE(create_test_wav(source_path, 2, 44100, 24, num_frames));

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    auto write_result = mwaac::write_track(open_result.value(), output_path,
                                           0, num_frames - 1);
    REQUIRE_FALSE(write_result.has_value());
    REQUIRE(write_result.error() == mwaac::AudioError::WriteError);

    // Target was not created, and no errant directory was created either.
    REQUIRE_FALSE(fs::exists(output_path));
    REQUIRE_FALSE(fs::exists(bogus_parent));

    // No temp-sibling artifacts in the (extant) test_dir.
    REQUIRE(count_mwaac_temp_siblings(test_dir) == 0);
}

TEST_CASE("write_track: failure leaves no file at output path (target is a directory)",
          "[lossless][atomic]") {
    // Pre-create a directory at output_path so that rename(temp, output_path)
    // fails. This exercises the rename-failure cleanup path: the temp-sibling
    // write succeeds, but the commit step fails, and we must remove the temp.
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_atomic_target_is_dir";
    fs::create_directories(test_dir);

    fs::path source_path = test_dir / "source.wav";
    fs::path output_path = test_dir / "output.wav";  // will be a directory

    TempFile source_cleanup(source_path);
    TempFile dir_cleanup(test_dir);

    constexpr int64_t num_frames = 256;
    REQUIRE(create_test_wav(source_path, 2, 44100, 24, num_frames));

    // Pre-create output_path as a directory containing a marker file. We will
    // assert this directory and its content survive the failed call.
    fs::create_directories(output_path);
    fs::path marker = output_path / "marker.txt";
    {
        std::ofstream f(marker);
        f << "preserved";
    }
    REQUIRE(fs::is_directory(output_path));
    REQUIRE(fs::exists(marker));

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    auto write_result = mwaac::write_track(open_result.value(), output_path,
                                           0, num_frames - 1);
    REQUIRE_FALSE(write_result.has_value());
    REQUIRE(write_result.error() == mwaac::AudioError::WriteError);

    // The pre-existing directory at the target path is unchanged.
    REQUIRE(fs::is_directory(output_path));
    REQUIRE(fs::exists(marker));

    // No temp-sibling artifacts remain in the parent dir after rename failure.
    REQUIRE(count_mwaac_temp_siblings(test_dir) == 0);

    // Tear down the directory we placed at output_path so TempFile cleanup
    // for the parent can complete.
    std::error_code ec;
    fs::remove_all(output_path, ec);
}

// -----------------------------------------------------------------------------
// M-16 audit-1 regression: a previous make_temp_sibling_path implementation
// built the temp filename via snprintf into a fixed 64-byte buffer. For
// target filenames around 30+ characters the random-suffix bytes were
// silently truncated, so concurrent write_track calls on the same long
// filename would generate identical temp paths, race on the same temp file,
// and leave a header-less / short file at the target. The audit reproduced
// 40/6400 corruption hits at 32 threads x 200 iterations with a 54-char
// target name. We use a smaller (CI-friendly) thread x iteration grid and
// rely on the assertion that NO success-returning call ever leaves a
// sub-header-sized file at the target.
// -----------------------------------------------------------------------------
TEST_CASE("write_track: long-filename concurrent writes do not collide",
          "[lossless][atomic]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_atomic_long_concurrent";
    {
        std::error_code ec;
        fs::remove_all(test_dir, ec);
    }
    fs::create_directories(test_dir);

    fs::path source_path = test_dir / "src.wav";
    TempFile source_cleanup(source_path);
    TempFile dir_cleanup(test_dir);

    constexpr int64_t num_frames = 1024;
    REQUIRE(create_test_wav(source_path, 1, 44100, 16, num_frames));

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    // Long target filename — 54 chars, comfortably past the old 64-byte
    // buffer's truncation threshold (which clipped the random suffix
    // entirely once the prefix consumed ~30+ bytes).
    const std::string longname = "long_track_filename_used_to_provoke_temp_collision.wav";
    REQUIRE(longname.size() >= 50);
    fs::path target = test_dir / longname;

    constexpr int kThreads = 8;
    constexpr int kIterations = 500;
    std::atomic<int> ok{0};
    std::atomic<int> err{0};
    std::atomic<int> short_file{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]{
            // Each thread shares the same opened source (read-only after open).
            for (int i = 0; i < kIterations; ++i) {
                auto r = mwaac::write_track(open_result.value(), target,
                                            0, num_frames - 1);
                if (!r.has_value()) {
                    err.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                ok.fetch_add(1, std::memory_order_relaxed);
                // Whenever write_track returns success, the target must be
                // at least the WAV header size (44 bytes). A buggy temp-path
                // implementation can leave an empty / short file because two
                // threads renamed colliding temp paths over each other while
                // a third was mid-write.
                std::error_code ec;
                auto sz = fs::file_size(target, ec);
                if (ec || sz < 44) {
                    short_file.fetch_add(1, std::memory_order_relaxed);
                }
                (void)t;
                (void)i;
            }
        });
    }
    for (auto& w : workers) w.join();

    INFO("ok=" << ok.load() << " err=" << err.load()
              << " short=" << short_file.load());

    // The hard invariant: no success-returning call left a sub-header-size
    // file at the target. Any nonzero short_file count means write_track's
    // atomicity broke under concurrency.
    REQUIRE(short_file.load() == 0);

    // Final state: target is a regular file with at least a WAV header.
    REQUIRE(fs::exists(target));
    REQUIRE(fs::is_regular_file(target));
    REQUIRE(fs::file_size(target) >= 44);

    // No temp-sibling artifacts remain in the parent dir.
    REQUIRE(count_mwaac_temp_siblings(test_dir) == 0);

    // Tear down the long-named target so TempFile dir_cleanup can complete.
    std::error_code rm_ec;
    fs::remove(target, rm_ec);
}

// -----------------------------------------------------------------------------
// M-16 audit-1 added: write_track must refuse target filenames whose
// corresponding temp-sibling name would exceed NAME_MAX (255 bytes on
// POSIX). Constructing the temp name and silently truncating it (or letting
// the OS surface ENAMETOOLONG mid-write) is exactly the failure mode that
// motivated the regression test above.
// -----------------------------------------------------------------------------
TEST_CASE("write_track: target filename longer than NAME_MAX returns WriteError",
          "[lossless][atomic]") {
    fs::path test_dir = fs::temp_directory_path() / "mwaac_test_atomic_namemax";
    {
        std::error_code ec;
        fs::remove_all(test_dir, ec);
    }
    fs::create_directories(test_dir);

    fs::path source_path = test_dir / "src.wav";
    TempFile source_cleanup(source_path);
    TempFile dir_cleanup(test_dir);

    constexpr int64_t num_frames = 256;
    REQUIRE(create_test_wav(source_path, 1, 44100, 16, num_frames));

    auto open_result = mwaac::AudioFile::open(source_path);
    REQUIRE(open_result.has_value());

    // 256-char filename. Even before the temp-sibling prefix/suffix is
    // appended, this exceeds NAME_MAX (255) on POSIX, so the temp-path
    // construction must reject it.
    std::string overlong(256, 'a');
    fs::path target = test_dir / overlong;

    auto write_result = mwaac::write_track(open_result.value(), target,
                                           0, num_frames - 1);
    REQUIRE_FALSE(write_result.has_value());
    REQUIRE(write_result.error() == mwaac::AudioError::WriteError);

    // Target must not exist. Use the std::error_code overload — the
    // throwing fs::exists raises filesystem_error on ENAMETOOLONG on some
    // platforms (e.g. macOS HFS+/APFS), which would otherwise mask the
    // real assertion behind an unrelated stat() failure.
    {
        std::error_code ec;
        REQUIRE_FALSE(fs::exists(target, ec));
    }

    // No temp-sibling artifacts remain.
    REQUIRE(count_mwaac_temp_siblings(test_dir) == 0);
}
