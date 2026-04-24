// FIXTURE-MALFORMED generator.
//
// Builds the 14+ small, byte-precise malformed audio files documented in
// README.md and listed in manifest.txt. Output filenames must match the
// manifest exactly; the test in tests/test_audio_file.cpp walks the
// manifest at run time and looks for each blob in this generator's
// output directory.
//
// Usage: malformed_fixture_gen <output-directory>
//
// The generator is deterministic (no clocks, no PRNGs) so identical
// bytes are produced on every CI run. Total output is well under 2 KiB.
//
// Style notes:
//   - We deliberately avoid linking against the project's audio_file
//     library. The blobs are malformed by definition; producing them
//     through the project's own writer would beg the question.
//   - Every blob is built up in a std::vector<std::uint8_t>, then flushed
//     atomically via std::ofstream. This keeps each emitter readable
//     and lets us hash / size-check blobs at the end of main().
//   - All multi-byte sizes are written as little-endian for RIFF/RF64
//     and big-endian for FORM/AIFF, exactly as the on-disk formats
//     require. Helpers below do the byte assembly so the per-blob
//     emitter functions stay declarative.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void push_bytes(Bytes& out, std::string_view tag) {
    for (char c : tag) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
}

void push_le_u16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void push_le_u32(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void push_be_u16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void push_be_u32(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void pad_to(Bytes& out, std::size_t target_size, std::uint8_t fill = 0u) {
    while (out.size() < target_size) {
        out.push_back(fill);
    }
}

bool write_blob(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "malformed-fixture: cannot open %s for writing\n",
                     path.string().c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!f.good()) {
        std::fprintf(stderr, "malformed-fixture: write failed for %s\n",
                     path.string().c_str());
        return false;
    }
    return true;
}

// Push a fake IEEE 80-bit extended float value of 0.0 (sign 0, exp 0,
// mantissa 0). Good enough for COMM where we don't actually need a
// valid sample-rate; the parser the fixture targets discards the
// 80-bit sample rate today.
void push_aiff_float80_zero(Bytes& out) {
    for (int i = 0; i < 10; ++i) {
        out.push_back(0u);
    }
}

// Build a syntactically-correct AIFF COMM chunk body.
//   channels (be u16) | num_frames (be u32) | bits_per_sample (be u16)
//   | sample_rate (10-byte float80) = 18 bytes
void push_aiff_comm_body(Bytes& out,
                         std::uint16_t channels,
                         std::uint32_t num_frames,
                         std::uint16_t bits_per_sample) {
    push_be_u16(out, channels);
    push_be_u32(out, num_frames);
    push_be_u16(out, bits_per_sample);
    push_aiff_float80_zero(out);
}

// ============================================================================
//  WAV / RF64 blobs
// ============================================================================

Bytes blob_tiny_8() {
    // 8 bytes of "RIFF" + 4 random bytes — below the 12-byte preamble.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 0u);  // RIFF size, irrelevant
    return b;
}

Bytes blob_tiny_43() {
    // RIFF/WAVE preamble + 31 zero bytes = 43 bytes.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 35u);  // claimed RIFF size; doesn't matter
    push_bytes(b, "WAVE");
    pad_to(b, 43u);
    return b;
}

Bytes blob_riff_no_chunks() {
    // 44 bytes: RIFF/WAVE preamble + 32 bytes of zeros that the chunk
    // walker will read as 4-byte tag + 4-byte size = 0; the walker loops
    // forward by 8 bytes each iteration and exits without finding fmt
    // or data.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 36u);
    push_bytes(b, "WAVE");
    pad_to(b, 44u);
    return b;
}

Bytes blob_fmt_size_max() {
    // RIFF/WAVE + fmt chunk with size 0xFFFFFFFF (the RF64 placeholder
    // sentinel). On a regular RIFF file this is nonsense; the chunk
    // walker must simply terminate.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 56u);
    push_bytes(b, "WAVE");
    push_bytes(b, "fmt ");
    push_le_u32(b, 0xFFFFFFFFu);
    pad_to(b, 64u);
    return b;
}

Bytes blob_fmt_size_overflows_file() {
    // fmt chunk_size = 0x00010000 — 64 KiB — much larger than the
    // 64-byte file. Chunk walker should advance past EOF and exit.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 56u);
    push_bytes(b, "WAVE");
    push_bytes(b, "fmt ");
    push_le_u32(b, 0x00010000u);
    pad_to(b, 64u);
    return b;
}

Bytes blob_data_before_fmt() {
    // 80-byte file. Layout:
    //   RIFF/WAVE preamble (12)
    //   data chunk (header 8 + 0 body bytes)
    //   fmt  chunk header with chunk_size = 0 (sub-minimum, < 16,
    //        so found_fmt stays false)
    // data chunk has size 0 here so the walker progresses cleanly.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 72u);
    push_bytes(b, "WAVE");
    push_bytes(b, "data");
    push_le_u32(b, 0u);
    push_bytes(b, "fmt ");
    push_le_u32(b, 0u);  // sub-minimum: triggers no-found_fmt path
    pad_to(b, 80u);
    return b;
}

Bytes blob_data_size_overflows_file() {
    // RIFF/WAVE + valid fmt + data chunk claiming 0x10000000 bytes.
    // Currently parses successfully (M-2 unguarded). Test marks this
    // case [!shouldfail] until M-2 lands a bytes-per-frame check.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 56u);
    push_bytes(b, "WAVE");

    // fmt  chunk: 16-byte body, audio_format = 1 (PCM), 2ch 48kHz 16-bit.
    push_bytes(b, "fmt ");
    push_le_u32(b, 16u);
    push_le_u16(b, 1u);          // audio_format PCM
    push_le_u16(b, 2u);          // channels
    push_le_u32(b, 48000u);      // sample_rate
    push_le_u32(b, 192000u);     // byte_rate
    push_le_u16(b, 4u);          // block_align
    push_le_u16(b, 16u);         // bits_per_sample

    // data chunk header with absurd size.
    push_bytes(b, "data");
    push_le_u32(b, 0x10000000u);

    pad_to(b, 64u);
    return b;
}

Bytes blob_rf64_no_ds64() {
    // RF64/WAVE preamble; no ds64 anywhere. Followed by junk that
    // doesn't match fmt/data. Parser should drop out with InvalidFormat
    // (no fmt, no data).
    Bytes b;
    push_bytes(b, "RF64");
    push_le_u32(b, 0xFFFFFFFFu);  // RIFF size placeholder (RF64 convention)
    push_bytes(b, "WAVE");
    pad_to(b, 64u);
    return b;
}

Bytes blob_rf64_ds64_truncated() {
    // RF64/WAVE + ds64 chunk_size = 12 (< 24). Parser ignores the
    // truncated ds64 (no rf64_data_size set). With no fmt or data
    // chunk following, the parser returns InvalidFormat.
    Bytes b;
    push_bytes(b, "RF64");
    push_le_u32(b, 0xFFFFFFFFu);
    push_bytes(b, "WAVE");
    push_bytes(b, "ds64");
    push_le_u32(b, 12u);            // sub-minimum
    // 12 bytes of partial ds64 body
    for (int i = 0; i < 12; ++i) {
        b.push_back(0u);
    }
    pad_to(b, 56u);
    return b;
}

Bytes blob_wav_fmt_audio_format_zero() {
    // Valid RIFF/WAVE/fmt structure but audio_format = 0. Parser
    // returns UnsupportedFormat.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 56u);
    push_bytes(b, "WAVE");
    push_bytes(b, "fmt ");
    push_le_u32(b, 16u);
    push_le_u16(b, 0u);       // audio_format = 0
    push_le_u16(b, 2u);       // channels
    push_le_u32(b, 48000u);   // sample_rate
    push_le_u32(b, 192000u);
    push_le_u16(b, 4u);
    push_le_u16(b, 16u);
    pad_to(b, 64u);
    return b;
}

Bytes blob_wav_fmt_adpcm() {
    // Microsoft ADPCM (audio_format = 0x0002). UnsupportedFormat.
    Bytes b;
    push_bytes(b, "RIFF");
    push_le_u32(b, 56u);
    push_bytes(b, "WAVE");
    push_bytes(b, "fmt ");
    push_le_u32(b, 16u);
    push_le_u16(b, 0x0002u);  // Microsoft ADPCM
    push_le_u16(b, 2u);
    push_le_u32(b, 48000u);
    push_le_u32(b, 192000u);
    push_le_u16(b, 4u);
    push_le_u16(b, 16u);
    pad_to(b, 64u);
    return b;
}

// ============================================================================
//  AIFF blobs
// ============================================================================

Bytes blob_aiff_no_comm_no_ssnd() {
    // FORM/AIFF envelope with the rest zero-padded. parse_aiff_header's
    // 54-byte minimum check passes, but neither COMM nor SSND is found.
    Bytes b;
    push_bytes(b, "FORM");
    push_be_u32(b, 46u);        // claimed FORM size (irrelevant for parser)
    push_bytes(b, "AIFF");
    pad_to(b, 54u);
    return b;
}

Bytes blob_aiff_ssnd_size_lt_8() {
    // SSND chunk_size = 4 (< 8). Parser's `chunk_size >= 8` guard
    // prevents the underflow; data_size stays 0 and `found_ssnd`
    // becomes true. We deliberately omit COMM, so the parser reports
    // InvalidFormat for the missing COMM. (This blob's job is to prove
    // the underflow guard holds; the *outcome* is InvalidFormat.)
    Bytes b;
    push_bytes(b, "FORM");
    push_be_u32(b, 72u);
    push_bytes(b, "AIFF");
    push_bytes(b, "SSND");
    push_be_u32(b, 4u);
    pad_to(b, 80u);
    return b;
}

Bytes blob_aiff_ssnd_offset_nonzero() {
    // Valid FORM/AIFF + COMM + SSND with a non-zero offset field. The
    // parser today ignores the offset; once M-5 lands the parser will
    // correctly skip the offset bytes and either reject the file (if
    // the offset places the data outside the file) or parse it
    // correctly. Until then, the test tags this case [!shouldfail].
    Bytes b;
    push_bytes(b, "FORM");
    push_be_u32(b, 88u);
    push_bytes(b, "AIFF");

    // COMM chunk: header 8 + 18-byte body = 26 bytes.
    push_bytes(b, "COMM");
    push_be_u32(b, 18u);
    push_aiff_comm_body(b, /*channels=*/2u, /*num_frames=*/0u,
                        /*bits_per_sample=*/16u);

    // SSND chunk header.
    push_bytes(b, "SSND");
    push_be_u32(b, 12u);            // chunk_size; body = 8 hdr + 4 data
    push_be_u32(b, 0x10u);          // OFFSET = 16 — non-zero, the M-5 case
    push_be_u32(b, 0u);             // block_size
    // 4 bytes of payload sample data
    for (int i = 0; i < 4; ++i) {
        b.push_back(0u);
    }
    pad_to(b, 96u);
    return b;
}

Bytes blob_aiff_comm_size_lt_18() {
    // COMM chunk_size = 12 (< 18). `found_comm` stays false; no SSND.
    // Outcome: InvalidFormat.
    Bytes b;
    push_bytes(b, "FORM");
    push_be_u32(b, 72u);
    push_bytes(b, "AIFF");
    push_bytes(b, "COMM");
    push_be_u32(b, 12u);
    // 12 bytes of partial COMM body
    for (int i = 0; i < 12; ++i) {
        b.push_back(0u);
    }
    pad_to(b, 80u);
    return b;
}

// ============================================================================
//  Driver
// ============================================================================

struct Entry {
    const char* name;
    Bytes (*make)();
};

const std::array<Entry, 15> kEntries = {{
    {"tiny_8.wav",                    &blob_tiny_8},
    {"tiny_43.wav",                   &blob_tiny_43},
    {"riff_no_chunks.wav",            &blob_riff_no_chunks},
    {"fmt_size_max.wav",              &blob_fmt_size_max},
    {"fmt_size_overflows_file.wav",   &blob_fmt_size_overflows_file},
    {"data_before_fmt.wav",           &blob_data_before_fmt},
    {"data_size_overflows_file.wav",  &blob_data_size_overflows_file},
    {"rf64_no_ds64.wav",              &blob_rf64_no_ds64},
    {"rf64_ds64_truncated.wav",       &blob_rf64_ds64_truncated},
    {"aiff_no_comm_no_ssnd.aiff",     &blob_aiff_no_comm_no_ssnd},
    {"aiff_ssnd_size_lt_8.aiff",      &blob_aiff_ssnd_size_lt_8},
    {"aiff_ssnd_offset_nonzero.aiff", &blob_aiff_ssnd_offset_nonzero},
    {"aiff_comm_size_lt_18.aiff",     &blob_aiff_comm_size_lt_18},
    {"wav_fmt_audio_format_zero.wav", &blob_wav_fmt_audio_format_zero},
    {"wav_fmt_adpcm.wav",             &blob_wav_fmt_adpcm},
}};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-directory>\n",
                     argc > 0 ? argv[0] : "malformed_fixture_gen");
        return 2;
    }

    std::filesystem::path out_dir(argv[1]);
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        std::fprintf(stderr, "malformed-fixture: cannot mkdir %s: %s\n",
                     out_dir.string().c_str(), ec.message().c_str());
        return 1;
    }

    bool ok = true;
    for (const auto& entry : kEntries) {
        Bytes blob = entry.make();
        if (blob.size() > 256u) {
            std::fprintf(stderr,
                         "malformed-fixture: %s exceeds 256-byte budget (%zu)\n",
                         entry.name, blob.size());
            ok = false;
            continue;
        }
        const std::filesystem::path path = out_dir / entry.name;
        if (!write_blob(path, blob)) {
            ok = false;
        }
    }

    return ok ? 0 : 1;
}
