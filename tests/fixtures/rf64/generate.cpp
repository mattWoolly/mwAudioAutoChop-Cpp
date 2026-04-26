// tests/fixtures/rf64/generate.cpp
//
// Deterministic generator for the FIXTURE-RF64 corpus. Emits two RF64 files
// whose logical data size exceeds 4 GiB but whose on-disk footprint is near
// zero thanks to sparse-file semantics (lseek past the end + 1-byte write).
//
// Why hand-write the bytes instead of going through libsndfile:
//
//   The whole point of these fixtures is to exercise the project's own
//   parse_wav_header and the round-trip path that will be fixed in C-3 and
//   M-4. libsndfile's RF64 writer would short-circuit those code paths —
//   it produces files that conform to libsndfile's own preferred layout,
//   which is not the layout the parser bugs care about. We need full
//   control over chunk order and size-field bytes.
//
// File layouts produced (EBU Tech 3306):
//
//   rf64_ds64_first.wav (canonical):
//     +0x00  "RF64"                              (4 bytes)
//     +0x04  0xFFFFFFFF                          (RIFF-size placeholder)
//     +0x08  "WAVE"                              (4 bytes)
//     +0x0C  "ds64" + size=28                    (8 bytes header)
//     +0x14  riff_size  (uint64 LE)              (8 bytes)
//     +0x1C  data_size  (uint64 LE)              (8 bytes)
//     +0x24  sample_count (uint64 LE)            (8 bytes)
//     +0x2C  table_length=0 (uint32 LE)          (4 bytes)
//     +0x30  "fmt " + size=16                    (8 bytes header)
//     +0x38  fmt body (16 bytes)
//     +0x48  "data" + 0xFFFFFFFF placeholder     (8 bytes header)
//     +0x50  <sparse data region, data_size bytes>
//
//   rf64_ds64_after.wav (M-4 trigger):
//     +0x00  "RF64" + 0xFFFFFFFF + "WAVE"        (12 bytes)
//     +0x0C  "fmt " + size=16                    (8 bytes header)
//     +0x14  fmt body (16 bytes)
//     +0x24  "data" + 0xFFFFFFFF                 (8 bytes header)
//     +0x2C  <sparse data region, data_size bytes>
//     +0x2C+data_size  "ds64" + size=28          (8 bytes header)
//     +0x34+data_size  ds64 body (28 bytes)
//
// Both files contain real PCM payload at two pre-declared regions inside
// the sparse range. The byte-identity round-trip in C-3 reads the source
// region, calls write_track, and asserts the written bytes equal the
// source bytes. Sparse-region bytes are zero by definition.
//
// Determinism: payload bytes are produced from a 64-bit linear congruential
// generator seeded per-file. No std::random_device, no time-of-day, no
// std::mt19937 state-leak concerns.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

// PCM format parameters for both fixtures. 16-bit stereo 48 kHz keeps the
// math obvious and matches what libsndfile decodes by default.
constexpr uint16_t kChannels = 2;
constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kBitsPerSample = 16;
constexpr uint16_t kBytesPerFrame = (kChannels * kBitsPerSample) / 8;  // 4
constexpr uint32_t kBytesPerSecond = kSampleRate * kBytesPerFrame;

// Logical data size: just above 4 GiB so the uint32 wraparound that C-3
// will fix is exercised, AND large enough to contain Region B (which sits
// at 4 GiB exactly + 1024 bytes of payload). 0x100002000 = 4 GiB + 8 KiB,
// rounded up so it is a multiple of bytes_per_frame and there is breathing
// room past Region B for any future test that wants to read trailing
// samples.
constexpr uint64_t kDataSize = 0x0000000100002000ULL;

// Two non-overlapping 256-sample (1024-byte) payload regions per file.
// Offsets are relative to the start of the data region.
//
// Region A is near the front so a normal sequential reader hits it first.
// Region B sits past the 4 GiB boundary so the round-trip is forced to
// resolve a 64-bit offset, not a 32-bit one. (start of B is at 4 GiB exactly,
// well past the uint32 cliff at 4 GiB - 1.)
constexpr uint64_t kPayloadBytes = 256ULL * kBytesPerFrame;  // 1024
constexpr uint64_t kRegionAOffset = 0x00001000ULL;            // 4096
constexpr uint64_t kRegionBOffset = 0x0000000100000000ULL;    // 4 GiB exactly

static_assert(kRegionAOffset + kPayloadBytes <= kRegionBOffset,
              "regions overlap");
static_assert(kRegionBOffset + kPayloadBytes <= kDataSize,
              "region B falls past logical data_size");

// Endianness helpers. We write LE on disk regardless of host. C++20 gives
// us std::endian, but the byte-by-byte form below is unambiguous and
// dependency-free.
void put_u16le(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

void put_u32le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void put_u64le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

void put_fourcc(uint8_t* p, const char* s) {
    p[0] = static_cast<uint8_t>(s[0]);
    p[1] = static_cast<uint8_t>(s[1]);
    p[2] = static_cast<uint8_t>(s[2]);
    p[3] = static_cast<uint8_t>(s[3]);
}

// Linear congruential generator. Numerical Recipes constants. Deterministic,
// fast, no library state. Adequate for "make every byte distinguishable
// from zero so the byte-identity test is meaningful".
struct Lcg {
    uint64_t state;
    uint8_t next_byte() {
        state = state * 1664525ULL + 1013904223ULL;
        return static_cast<uint8_t>((state >> 16) & 0xFFu);
    }
};

void fill_payload(uint8_t* dst, uint64_t n, uint64_t seed) {
    Lcg lcg{seed};
    for (uint64_t i = 0; i < n; ++i) {
        dst[i] = lcg.next_byte();
    }
}

// Build the fmt chunk body (16 bytes, PCM extension format-tag = 1).
void write_fmt_body(uint8_t* dst) {
    put_u16le(dst + 0, 1);                       // audio_format = PCM
    put_u16le(dst + 2, kChannels);
    put_u32le(dst + 4, kSampleRate);
    put_u32le(dst + 8, kBytesPerSecond);
    put_u16le(dst + 12, kBytesPerFrame);
    put_u16le(dst + 14, kBitsPerSample);
}

// Write `n` bytes at offset `off` to fd. pwrite is atomic w.r.t. the file
// position, so we don't need to lseek first.
bool pwrite_all(int fd, const void* buf, size_t n, off_t off) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        ssize_t wrote = ::pwrite(fd, p, n, off);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (wrote == 0) return false;
        p += wrote;
        off += wrote;
        n -= static_cast<size_t>(wrote);
    }
    return true;
}

// Make `path` a sparse file of logical size `total_size`. The classic
// trick: seek to total_size - 1, write a single zero byte. POSIX guarantees
// the intermediate range reads as zero, and on macOS APFS / Linux ext4 +
// btrfs the file system stores only the populated extents.
bool make_sparse_to(int fd, off_t total_size) {
    if (::ftruncate(fd, total_size) < 0) return false;
    return true;
}

// ---------------------------------------------------------------------
// Variant 1: ds64 immediately after WAVE (the canonical, well-formed case).
// ---------------------------------------------------------------------

constexpr size_t kHeaderSizeFirst = 0x50;  // 80 bytes
constexpr uint64_t kDataOffsetFirst = kHeaderSizeFirst;

bool generate_ds64_first(const std::string& path) {
    const uint64_t total_size = kDataOffsetFirst + kDataSize;

    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "open(%s) failed: %s\n", path.c_str(), std::strerror(errno));
        return false;
    }

    if (!make_sparse_to(fd, static_cast<off_t>(total_size))) {
        std::fprintf(stderr, "ftruncate failed: %s\n", std::strerror(errno));
        ::close(fd);
        return false;
    }

    uint8_t header[kHeaderSizeFirst]{};

    // RF64 + RIFF-size placeholder + WAVE
    put_fourcc(header + 0x00, "RF64");
    put_u32le(header + 0x04, 0xFFFFFFFFu);
    put_fourcc(header + 0x08, "WAVE");

    // ds64 chunk (size 28 = 8+8+8+4)
    put_fourcc(header + 0x0C, "ds64");
    put_u32le(header + 0x10, 28);
    // riff_size = total_size - 8 (everything after the first 8 bytes).
    put_u64le(header + 0x14, total_size - 8);
    put_u64le(header + 0x1C, kDataSize);
    // sample_count = frames
    put_u64le(header + 0x24, kDataSize / kBytesPerFrame);
    // table_length = 0 (no extra chunk-size table entries)
    put_u32le(header + 0x2C, 0);

    // fmt chunk (16-byte body)
    put_fourcc(header + 0x30, "fmt ");
    put_u32le(header + 0x34, 16);
    write_fmt_body(header + 0x38);

    // data chunk header with 0xFFFFFFFF placeholder
    put_fourcc(header + 0x48, "data");
    put_u32le(header + 0x4C, 0xFFFFFFFFu);

    if (!pwrite_all(fd, header, kHeaderSizeFirst, 0)) {
        std::fprintf(stderr, "pwrite header failed: %s\n", std::strerror(errno));
        ::close(fd);
        return false;
    }

    // Payload regions. Seeds chosen distinct per-region per-file so a
    // mistake that cross-wires regions surfaces as a hash mismatch.
    uint8_t payload[kPayloadBytes];
    fill_payload(payload, kPayloadBytes, 0xA17F'C0DEULL);
    if (!pwrite_all(fd, payload, kPayloadBytes,
                    static_cast<off_t>(kDataOffsetFirst + kRegionAOffset))) {
        ::close(fd);
        return false;
    }
    fill_payload(payload, kPayloadBytes, 0xB07F'BEEFULL);
    if (!pwrite_all(fd, payload, kPayloadBytes,
                    static_cast<off_t>(kDataOffsetFirst + kRegionBOffset))) {
        ::close(fd);
        return false;
    }

    ::close(fd);
    return true;
}

// ---------------------------------------------------------------------
// Variant 2: ds64 AFTER the data chunk (M-4 trigger).
//
// This is technically a valid RF64 layout in the sense that the bytes are
// well-formed and a sufficiently smart parser can recover the data size,
// but it violates the EBU recommendation that ds64 appear immediately
// after WAVE. The current parse_wav_header walker advances by chunk_size
// after seeing the data chunk's 0xFFFFFFFF placeholder, which causes it to
// skip 4 GiB ahead and miss the trailing ds64. M-4 is the fix.
// ---------------------------------------------------------------------

constexpr size_t kHeaderSizeAfter = 0x2C;  // 44 bytes before data starts
constexpr uint64_t kDataOffsetAfter = kHeaderSizeAfter;
constexpr size_t kTrailerSize = 8 + 28;    // ds64 header + body

bool generate_ds64_after(const std::string& path) {
    const uint64_t total_size = kDataOffsetAfter + kDataSize + kTrailerSize;

    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "open(%s) failed: %s\n", path.c_str(), std::strerror(errno));
        return false;
    }

    if (!make_sparse_to(fd, static_cast<off_t>(total_size))) {
        ::close(fd);
        return false;
    }

    uint8_t header[kHeaderSizeAfter]{};

    put_fourcc(header + 0x00, "RF64");
    put_u32le(header + 0x04, 0xFFFFFFFFu);
    put_fourcc(header + 0x08, "WAVE");

    // fmt chunk
    put_fourcc(header + 0x0C, "fmt ");
    put_u32le(header + 0x10, 16);
    write_fmt_body(header + 0x14);

    // data chunk header with placeholder
    put_fourcc(header + 0x24, "data");
    put_u32le(header + 0x28, 0xFFFFFFFFu);

    if (!pwrite_all(fd, header, kHeaderSizeAfter, 0)) {
        ::close(fd);
        return false;
    }

    // Trailing ds64 chunk lives just after the data region.
    uint8_t trailer[kTrailerSize]{};
    put_fourcc(trailer + 0, "ds64");
    put_u32le(trailer + 4, 28);
    put_u64le(trailer + 8, total_size - 8);
    put_u64le(trailer + 16, kDataSize);
    put_u64le(trailer + 24, kDataSize / kBytesPerFrame);
    put_u32le(trailer + 32, 0);
    if (!pwrite_all(fd, trailer, kTrailerSize,
                    static_cast<off_t>(kDataOffsetAfter + kDataSize))) {
        ::close(fd);
        return false;
    }

    // Payload regions.
    uint8_t payload[kPayloadBytes];
    fill_payload(payload, kPayloadBytes, 0xC0DE'F00DULL);
    if (!pwrite_all(fd, payload, kPayloadBytes,
                    static_cast<off_t>(kDataOffsetAfter + kRegionAOffset))) {
        ::close(fd);
        return false;
    }
    fill_payload(payload, kPayloadBytes, 0xDEAD'5EEDULL);
    if (!pwrite_all(fd, payload, kPayloadBytes,
                    static_cast<off_t>(kDataOffsetAfter + kRegionBOffset))) {
        ::close(fd);
        return false;
    }

    ::close(fd);
    return true;
}

// ---------------------------------------------------------------------
// Manifest. A simple text format the test reads with std::ifstream. Keys
// are uppercase, KEY=VALUE per line, comments with '#'. No JSON because
// pulling in a JSON library for 50 bytes of metadata is gratuitous.
// ---------------------------------------------------------------------

bool write_manifest(const std::string& dir) {
    const std::string path = dir + "/manifest.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
        "# FIXTURE-RF64 manifest. Generated by gen_rf64_fixture.\n"
        "# All offsets are byte offsets into the file; sizes are in bytes.\n"
        "CHANNELS=%u\n"
        "SAMPLE_RATE=%u\n"
        "BITS_PER_SAMPLE=%u\n"
        "BYTES_PER_FRAME=%u\n"
        "DATA_SIZE=%llu\n"
        "PAYLOAD_BYTES=%llu\n"
        "REGION_A_OFFSET=%llu\n"
        "REGION_B_OFFSET=%llu\n"
        "DS64_FIRST_DATA_OFFSET=%llu\n"
        "DS64_FIRST_REGION_A_SEED=0xA17FC0DE\n"
        "DS64_FIRST_REGION_B_SEED=0xB07FBEEF\n"
        "DS64_AFTER_DATA_OFFSET=%llu\n"
        "DS64_AFTER_REGION_A_SEED=0xC0DEF00D\n"
        "DS64_AFTER_REGION_B_SEED=0xDEAD5EED\n",
        static_cast<unsigned>(kChannels),
        static_cast<unsigned>(kSampleRate),
        static_cast<unsigned>(kBitsPerSample),
        static_cast<unsigned>(kBytesPerFrame),
        static_cast<unsigned long long>(kDataSize),
        static_cast<unsigned long long>(kPayloadBytes),
        static_cast<unsigned long long>(kRegionAOffset),
        static_cast<unsigned long long>(kRegionBOffset),
        static_cast<unsigned long long>(kDataOffsetFirst),
        static_cast<unsigned long long>(kDataOffsetAfter));
    std::fclose(f);
    return true;
}

bool ensure_dir(const std::string& dir) {
    // mkdir -p one level deep is sufficient — the parent is created by
    // CMake's $<TARGET_FILE_DIR> machinery before this binary runs.
    if (::mkdir(dir.c_str(), 0755) == 0) return true;
    if (errno == EEXIST) {
        struct stat st{};
        if (::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    }
    std::fprintf(stderr, "mkdir(%s) failed: %s\n", dir.c_str(), std::strerror(errno));
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const std::string out_dir = argv[1];
    if (!ensure_dir(out_dir)) return 1;

    const std::string ds64_first_path = out_dir + "/rf64_ds64_first.wav";
    const std::string ds64_after_path = out_dir + "/rf64_ds64_after.wav";

    if (!generate_ds64_first(ds64_first_path)) {
        std::fprintf(stderr, "failed to generate %s\n", ds64_first_path.c_str());
        return 1;
    }
    if (!generate_ds64_after(ds64_after_path)) {
        std::fprintf(stderr, "failed to generate %s\n", ds64_after_path.c_str());
        return 1;
    }
    if (!write_manifest(out_dir)) {
        std::fprintf(stderr, "failed to write manifest\n");
        return 1;
    }

    std::fprintf(stdout, "FIXTURE-RF64 written to %s\n", out_dir.c_str());
    return 0;
}
