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

// Read the LAST 64 KiB of `path`. parse_wav_header in production walks
// only the first window, so for the ds64-after-data variant the trailing
// ds64 is invisible to the current implementation. The M-4 fix will need
// to make the parser look at the tail too — we expose this helper here
// so that, post-fix, a richer test can verify the recovered data_size.
std::vector<uint8_t> rf64_read_full_with_tail(const fs::path& path) {
    // For the M-4 case the handcrafted parser will need access to the
    // trailer. Returning the full file here would mean materialising 4 GiB
    // into RAM. Instead we splice: first 64 KiB + last 1 MiB (the ds64
    // trailer is at most 36 bytes; 1 MiB is overkill but cheap because
    // the middle is sparse and we read the actual physical tail only).
    //
    // The current parser will not look here. Once M-4 lands, the parser
    // is expected to either two-pass or fall through and we will revise
    // this helper to feed it whatever shape it expects. For NOW we just
    // return the head — the test below documents the failure mode.
    return rf64_read_header_window(path);
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

// [!shouldfail] — fails today because parse_wav_header advances past the
// 0xFFFFFFFF data placeholder and never reaches the trailing ds64.
// Removing this tag without a fix would re-mask the M-4 defect.
TEST_CASE("parse_wav_header: RF64 with ds64 after data",
          "[lossless][rf64][!shouldfail]") {
    fs::path dir = rf64_fixture_dir();
    fs::path file = dir / "rf64_ds64_after.wav";
    fs::path manifest = dir / "manifest.txt";

    REQUIRE(fs::exists(file));
    REQUIRE(fs::exists(manifest));

    // Use the helper that, post-M-4, will feed whatever shape the parser
    // expects (head + tail, two-pass, etc.). Today it just returns the
    // first 64 KiB, which is what production sees through AudioFile::open.
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
