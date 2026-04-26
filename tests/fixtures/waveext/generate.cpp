// tests/fixtures/waveext/generate.cpp
//
// FIXTURE-WAVEEXT generator — produces 24-bit WAVE_FORMAT_EXTENSIBLE
// (audio_format = 0xFFFE) WAV files plus a manifest. Bytes are assembled
// by hand because libsndfile emits the legacy format-tag 0x0001 for
// channel counts <= 2 and the parser we are stress-testing keys off the
// 0xFFFE tag.
//
// Wire format (post-RIFF/WAVE):
//
//   "fmt "  uint32 size=40
//     uint16 wFormatTag       = 0xFFFE   (WAVE_FORMAT_EXTENSIBLE)
//     uint16 nChannels
//     uint32 nSamplesPerSec
//     uint32 nAvgBytesPerSec  = SR * block_align
//     uint16 nBlockAlign      = channels * bytes_per_sample
//     uint16 wBitsPerSample
//     uint16 cbSize           = 22
//     uint16 wValidBitsPerSample
//     uint32 dwChannelMask
//     byte[16] SubFormat GUID
//   "data"  uint32 size
//     <samples>
//
// SubFormat GUIDs (little-endian Data1, Data2, Data3 + raw Data4):
//   PCM:        00000001-0000-0010-8000-00aa00389b71
//   IEEE float: 00000003-0000-0010-8000-00aa00389b71
//   "all-zero": 00000000-0000-0000-0000-000000000000  (used for the
//               "unknown subformat" negative fixture)
//
// Sample data is a deterministic 24-bit signed sawtooth: each successive
// sample (across all channels) increments by a per-fixture stride and
// wraps within the 24-bit signed range. This gives us byte-identity
// targets for the lossless round-trip test without an RNG dependency
// across libstdc++ versions.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int kSampleRate = 48000;
constexpr int kFrames     = 48000;  // 1 second
constexpr int kBitsPerSample = 24;

// Microsoft SubFormat GUIDs (16 bytes, exact wire order).
constexpr std::array<std::uint8_t, 16> kGuidPcm = {
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
};

constexpr std::array<std::uint8_t, 16> kGuidUnknown = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
constexpr std::uint32_t kChannelMaskStereo = 0x00000003u;
// FL | FR | FC | BL | BR  (5.0)
constexpr std::uint32_t kChannelMask5ch    = 0x00000033u | 0x00000004u;

// Append little-endian integers to a byte buffer.
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8)  & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}
void put_bytes(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> b) {
    out.insert(out.end(), b.begin(), b.end());
}
void put_str(std::vector<std::uint8_t>& out, const char (&s)[5]) {
    // 4-char FourCC + trailing NUL not written.
    out.push_back(static_cast<std::uint8_t>(s[0]));
    out.push_back(static_cast<std::uint8_t>(s[1]));
    out.push_back(static_cast<std::uint8_t>(s[2]));
    out.push_back(static_cast<std::uint8_t>(s[3]));
}

// 24-bit signed sawtooth sample data, deterministic per (channels, stride).
std::vector<std::uint8_t> make_pcm24_sawtooth(int channels, int frames, std::int32_t stride) {
    const std::int32_t max24 = (1 << 23) - 1;        //  8388607
    const std::int32_t min24 = -(1 << 23);           // -8388608
    const std::int32_t range = max24 - min24 + 1;    // 16777216

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(channels) * static_cast<std::size_t>(frames) * 3u);

    std::int64_t acc = 0;  // signed accumulator before folding into 24-bit range
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            // Per-channel offset so multichannel files don't have identical streams.
            std::int64_t v = acc + static_cast<std::int64_t>(c) * 65537;
            // Fold to [min24, max24].
            std::int64_t r = ((v - min24) % range + range) % range + min24;
            // Convert to unsigned 32-bit before shifting so we get
            // well-defined right-shift behaviour and clean conversions
            // to uint8_t under -Wsign-conversion.
            auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(r));
            // 24-bit little-endian (low, mid, high).
            bytes.push_back(static_cast<std::uint8_t>(u         & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((u >> 8)  & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFFu));
            acc += stride;
        }
    }
    return bytes;
}

struct WaveExtSpec {
    std::string filename;
    int channels;
    std::uint32_t channel_mask;
    std::array<std::uint8_t, 16> subformat_guid;
    std::int32_t sawtooth_stride;
};

struct ManifestEntry {
    std::string filename;
    std::uint64_t data_offset;
    std::uint64_t data_size;
    int channels;
    int bits_per_sample;
};

ManifestEntry write_waveext_file(const fs::path& dir, const WaveExtSpec& spec) {
    const int bytes_per_sample = kBitsPerSample / 8;          // 3
    const int block_align      = spec.channels * bytes_per_sample;
    const std::uint32_t avg_bytes_per_sec =
        static_cast<std::uint32_t>(kSampleRate) * static_cast<std::uint32_t>(block_align);

    auto samples = make_pcm24_sawtooth(spec.channels, kFrames, spec.sawtooth_stride);
    const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size());

    std::vector<std::uint8_t> buf;
    buf.reserve(12 + 8 + 40 + 8 + samples.size());

    // RIFF header. RIFF size patched after we know total length.
    put_str(buf, "RIFF");
    put_u32(buf, 0);  // placeholder; fixed up below
    put_str(buf, "WAVE");

    // fmt chunk (40-byte WAVE_FORMAT_EXTENSIBLE body).
    put_str(buf, "fmt ");
    put_u32(buf, 40);
    put_u16(buf, 0xFFFEu);                                          // wFormatTag
    put_u16(buf, static_cast<std::uint16_t>(spec.channels));
    put_u32(buf, static_cast<std::uint32_t>(kSampleRate));
    put_u32(buf, avg_bytes_per_sec);
    put_u16(buf, static_cast<std::uint16_t>(block_align));
    put_u16(buf, static_cast<std::uint16_t>(kBitsPerSample));
    put_u16(buf, 22);                                                // cbSize
    put_u16(buf, static_cast<std::uint16_t>(kBitsPerSample));        // wValidBitsPerSample
    put_u32(buf, spec.channel_mask);
    put_bytes(buf, std::span<const std::uint8_t>(spec.subformat_guid));

    // data chunk.
    const std::size_t data_chunk_header_offset = buf.size();
    put_str(buf, "data");
    put_u32(buf, data_size);
    const std::size_t data_offset = buf.size();
    put_bytes(buf, std::span<const std::uint8_t>(samples));

    // Patch RIFF size: total size minus 8 (the "RIFF" + size fields).
    const std::uint32_t riff_size = static_cast<std::uint32_t>(buf.size() - 8u);
    buf[4] = static_cast<std::uint8_t>(riff_size         & 0xFFu);
    buf[5] = static_cast<std::uint8_t>((riff_size >> 8)  & 0xFFu);
    buf[6] = static_cast<std::uint8_t>((riff_size >> 16) & 0xFFu);
    buf[7] = static_cast<std::uint8_t>((riff_size >> 24) & 0xFFu);

    fs::path out_path = dir / spec.filename;
    std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::fprintf(stderr, "gen_waveext_fixture: cannot open %s for writing\n",
                     out_path.string().c_str());
        std::exit(1);
    }
    ofs.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    if (!ofs) {
        std::fprintf(stderr, "gen_waveext_fixture: write failed for %s\n",
                     out_path.string().c_str());
        std::exit(1);
    }

    // Silence unused-variable on data_chunk_header_offset in NDEBUG-only builds.
    (void)data_chunk_header_offset;

    return ManifestEntry{
        spec.filename,
        static_cast<std::uint64_t>(data_offset),
        static_cast<std::uint64_t>(data_size),
        spec.channels,
        kBitsPerSample,
    };
}

void write_manifest(const fs::path& dir, std::span<const ManifestEntry> entries) {
    fs::path path = dir / "manifest.txt";
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) {
        std::fprintf(stderr, "gen_waveext_fixture: cannot write manifest\n");
        std::exit(1);
    }
    ofs << "# FIXTURE-WAVEEXT manifest\n";
    ofs << "# format: filename data_offset data_size channels bits_per_sample\n";
    for (const auto& e : entries) {
        ofs << e.filename << ' '
            << e.data_offset << ' '
            << e.data_size << ' '
            << e.channels << ' '
            << e.bits_per_sample << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gen_waveext_fixture <output-dir>\n");
        return 2;
    }
    fs::path out_dir = argv[1];
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        std::fprintf(stderr, "gen_waveext_fixture: cannot create %s: %s\n",
                     out_dir.string().c_str(), ec.message().c_str());
        return 1;
    }

    const std::array<WaveExtSpec, 3> specs = {{
        {"pcm_24bit_stereo.wav", 2, kChannelMaskStereo, kGuidPcm, 12345},
        {"pcm_24bit_5ch.wav",    5, kChannelMask5ch,    kGuidPcm, 7919},
        {"pcm_extensible_unsupported_subformat.wav", 2, kChannelMaskStereo, kGuidUnknown, 1},
    }};

    std::vector<ManifestEntry> manifest;
    manifest.reserve(specs.size());
    for (const auto& s : specs) {
        manifest.push_back(write_waveext_file(out_dir, s));
    }
    write_manifest(out_dir, manifest);
    return 0;
}
