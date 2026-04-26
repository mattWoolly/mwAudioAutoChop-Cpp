// Synthetic vinyl-rip fixture for FIXTURE-REF.
//
// Produces a deterministic three-track vinyl rip whose per-track audio has a
// distinctive RMS envelope (per BACKLOG.md FIXTURE-REF). The reference-mode
// pipeline aligns by RMS-envelope cross-correlation; constant-amplitude tones
// produce a flat envelope that correlates equally well at every offset, hence
// the previous tones-in-noise fixture was insufficient.
//
// Output layout (under <out_dir>):
//   vinyl.wav            — concatenated rip (lead-in noise, T1, gap, T2,
//                          gap, T3), mono PCM_16 at kFixtureSampleRate.
//   refs/01_track1.wav   — reference for T1 (same audio, no lead-in/gap)
//   refs/02_track2.wav
//   refs/03_track3.wav
//   manifest.txt         — flat KEY=VALUE ground truth (tracks indexed 1..3)
//
// The fixture is fully deterministic: a fixed-seed std::mt19937 drives the
// noise generator and there is no wall-clock or filesystem state in the
// output. The same source compiled on the same toolchain produces a
// byte-identical vinyl.wav and references.

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Constants. Every magic number is named here.
// ---------------------------------------------------------------------------

// Sample rate of the generated WAV files. 44100 Hz is the canonical "CD rate"
// vinyl rip; analysis will downsample to 22050 internally.
constexpr int kFixtureSampleRate = 44100;

// Per-track length in seconds. Must be >=10 s so reference_mode's
// envelope_refine_start step engages (see reference_mode.cpp:953 — it gates
// envelope refinement on `duration_samples >= 10 * sample_rate`).
constexpr double kTrackSeconds = 12.0;

// Inter-track gap, in seconds. Must be >=3 s per FIXTURE-REF spec.
constexpr double kGapSeconds = 3.5;

// Lead-in noise before the first track, in seconds. >=1.5 s per spec.
constexpr double kLeadInSeconds = 1.5;

// Inter-track / lead-in noise amplitude (linear). -60 dBFS — well below the
// per-track peak (0 dBFS) so the gap is unambiguous on the envelope.
constexpr float kGapNoiseAmplitude = 1.0e-3f;

// Per-track peak amplitude (linear). 0.85 leaves headroom against PCM clip.
constexpr float kTrackPeakAmplitude = 0.85f;

// Pink-noise filter order (Voss-McCartney). Eight octaves of accumulators
// gives a 1/f spectrum down to roughly fs/256 ~= 172 Hz — distinctive
// enough for spectral matching while remaining computationally trivial.
constexpr int kPinkOctaves = 8;

// Deterministic master seed. Different sub-seeds derived from this give each
// track its own noise realisation; reusing the same master ensures the
// fixture is byte-stable across builds.
constexpr uint32_t kMasterSeed = 0x4D57'4143u;  // "MWAC"

// PCM_16 quantum (1 / 32768). Used only in comments / sanity asserts.
constexpr float kPcm16Quantum = 1.0f / 32768.0f;
static_assert(kPcm16Quantum > 0.0f);

// ---------------------------------------------------------------------------
// Pink-noise generator (Voss-McCartney). Cheap, deterministic, and produces
// audio with non-flat spectrum so spectral content of each track differs
// from the inter-track white noise.
// ---------------------------------------------------------------------------

class PinkNoise {
public:
    explicit PinkNoise(uint32_t seed)
        : rng_(seed), dist_(-1.0f, 1.0f), accumulators_{}, sum_(0.0f)
    {
        for (int i = 0; i < kPinkOctaves; ++i) {
            accumulators_[static_cast<size_t>(i)] = dist_(rng_);
            sum_ += accumulators_[static_cast<size_t>(i)];
        }
    }

    [[nodiscard]] float next() {
        // Voss-McCartney: each octave updates at half the rate of the
        // previous. Counter trick: index of the lowest set bit picks the
        // octave whose accumulator we refresh this sample.
        ++sample_index_;
        int octave = lowest_set_bit(sample_index_);
        if (octave < kPinkOctaves) {
            sum_ -= accumulators_[static_cast<size_t>(octave)];
            accumulators_[static_cast<size_t>(octave)] = dist_(rng_);
            sum_ += accumulators_[static_cast<size_t>(octave)];
        }
        // Normalise: sum of N uniform[-1,1] is bounded by N; divide.
        return sum_ / static_cast<float>(kPinkOctaves);
    }

private:
    static int lowest_set_bit(uint64_t x) {
        // Returns kPinkOctaves when x is zero or has no low bits in range,
        // which is fine — we then skip the octave update.
        for (int i = 0; i < kPinkOctaves; ++i) {
            if (((x >> i) & 1u) == 1u) return i;
        }
        return kPinkOctaves;
    }

    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_;
    std::array<float, kPinkOctaves> accumulators_;
    float sum_;
    uint64_t sample_index_{0};
};

// White noise, used for inter-track gap and lead-in surface noise.
class WhiteNoise {
public:
    explicit WhiteNoise(uint32_t seed) : rng_(seed), dist_(-1.0f, 1.0f) {}

    [[nodiscard]] float next() { return dist_(rng_); }

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_;
};

// ---------------------------------------------------------------------------
// Per-track AM (amplitude modulation) curves. Each track has a unique
// envelope shape so RMS-envelope cross-correlation has an unambiguous peak.
// All curves return a linear gain in [0, 1].
// ---------------------------------------------------------------------------

// Track 1: fade-in (1.5 s) -> hold (8.5 s) -> fade-out (2.0 s).
// Cosine fades for smooth onset; produces a clean trapezoid envelope.
[[nodiscard]] float track1_envelope(double t_seconds) {
    constexpr double kFadeIn = 1.5;
    constexpr double kFadeOut = 2.0;
    const double t_end = kTrackSeconds;
    if (t_seconds < 0.0 || t_seconds > t_end) return 0.0f;
    if (t_seconds < kFadeIn) {
        // Cosine fade-in from 0 to 1
        const double phase = t_seconds / kFadeIn;  // 0 .. 1
        return static_cast<float>(0.5 - 0.5 * std::cos(M_PI * phase));
    }
    if (t_seconds > t_end - kFadeOut) {
        const double phase = (t_end - t_seconds) / kFadeOut;  // 1 .. 0
        return static_cast<float>(0.5 - 0.5 * std::cos(M_PI * phase));
    }
    return 1.0f;
}

// Track 2: instant onset, 4 s sustain, 0.3 s drop to -18 dB, 0.3 s recovery,
// then sustain to end. The drop is the distinctive feature.
[[nodiscard]] float track2_envelope(double t_seconds) {
    constexpr double kSustainBeforeDrop = 4.0;
    constexpr double kDropDuration = 0.3;
    constexpr double kRecoveryDuration = 0.3;
    // -18 dB linear gain
    constexpr double kDropLevel = 0.125892541;  // pow(10, -18/20)

    if (t_seconds < 0.0 || t_seconds > kTrackSeconds) return 0.0f;
    if (t_seconds < kSustainBeforeDrop) return 1.0f;
    const double after_drop_start = t_seconds - kSustainBeforeDrop;
    if (after_drop_start < kDropDuration) {
        // Linear ramp from 1 down to kDropLevel
        const double phase = after_drop_start / kDropDuration;
        return static_cast<float>(1.0 - phase * (1.0 - kDropLevel));
    }
    const double after_drop_end = after_drop_start - kDropDuration;
    if (after_drop_end < kRecoveryDuration) {
        const double phase = after_drop_end / kRecoveryDuration;
        return static_cast<float>(kDropLevel + phase * (1.0 - kDropLevel));
    }
    return 1.0f;
}

// Track 3: 1 Hz amplitude oscillation between 0 dB (peak 1.0) and -12 dB
// (~0.251). A periodic envelope distinguishable from the trapezoid and the
// drop curves above.
[[nodiscard]] float track3_envelope(double t_seconds) {
    constexpr double kOscFreq = 1.0;            // Hz
    constexpr double kHighGain = 1.0;
    constexpr double kLowGainDb = -12.0;        // dB
    const double low_gain = std::pow(10.0, kLowGainDb / 20.0);

    if (t_seconds < 0.0 || t_seconds > kTrackSeconds) return 0.0f;
    // (1 + cos)/2 oscillates between 1 and 0; remap to [low_gain, kHighGain].
    const double cos_part = 0.5 + 0.5 * std::cos(2.0 * M_PI * kOscFreq * t_seconds);
    return static_cast<float>(low_gain + (kHighGain - low_gain) * cos_part);
}

using EnvelopeFn = float (*)(double);

// ---------------------------------------------------------------------------
// Track audio synthesis: filtered pink noise * per-track envelope.
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<float>
generate_track(EnvelopeFn envelope, uint32_t noise_seed) {
    const int64_t n_samples =
        static_cast<int64_t>(kTrackSeconds * kFixtureSampleRate);
    std::vector<float> out(static_cast<size_t>(n_samples));

    PinkNoise noise(noise_seed);
    const double dt = 1.0 / kFixtureSampleRate;

    for (int64_t i = 0; i < n_samples; ++i) {
        const double t = static_cast<double>(i) * dt;
        const float gain = envelope(t);
        const float carrier = noise.next();
        out[static_cast<size_t>(i)] = kTrackPeakAmplitude * gain * carrier;
    }
    return out;
}

[[nodiscard]] std::vector<float>
generate_gap_noise(double seconds, uint32_t noise_seed) {
    const int64_t n_samples =
        static_cast<int64_t>(seconds * kFixtureSampleRate);
    std::vector<float> out(static_cast<size_t>(n_samples));
    WhiteNoise noise(noise_seed);
    for (int64_t i = 0; i < n_samples; ++i) {
        out[static_cast<size_t>(i)] = kGapNoiseAmplitude * noise.next();
    }
    return out;
}

// ---------------------------------------------------------------------------
// WAV writer (mono PCM_16) via libsndfile.
// ---------------------------------------------------------------------------

[[nodiscard]] bool
write_wav_mono_pcm16(const fs::path& path, std::span<const float> samples) {
    SF_INFO info{};
    info.samplerate = kFixtureSampleRate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    info.frames = static_cast<sf_count_t>(samples.size());

    SNDFILE* sf = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (sf == nullptr) return false;

    const sf_count_t want = static_cast<sf_count_t>(samples.size());
    const sf_count_t wrote = sf_write_float(sf, samples.data(), want);
    sf_close(sf);
    return wrote == want;
}

// ---------------------------------------------------------------------------
// Manifest writer. Flat KEY=VALUE pairs, one per line; trivial to parse from
// the test side without pulling in a JSON dependency.
// ---------------------------------------------------------------------------

struct TrackTruth {
    std::string name;
    int64_t start_sample;     // first sample of track in vinyl.wav
    int64_t end_sample;       // last sample (inclusive)
};

[[nodiscard]] bool
write_manifest(const fs::path& path,
               int sample_rate,
               std::span<const TrackTruth> tracks)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "fixture=ref_v1\n";
    out << "sample_rate=" << sample_rate << "\n";
    out << "num_tracks=" << tracks.size() << "\n";
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& tr = tracks[i];
        out << "track" << (i + 1) << "_name=" << tr.name << "\n";
        out << "track" << (i + 1) << "_start_sample=" << tr.start_sample << "\n";
        out << "track" << (i + 1) << "_end_sample=" << tr.end_sample << "\n";
    }
    out.flush();
    return out.good();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr,
                     "usage: %s <output_dir>\n"
                     "  Writes vinyl.wav, refs/, and manifest.txt under "
                     "<output_dir>.\n",
                     argv[0]);
        return 2;
    }

    const fs::path out_dir = argv[1];
    const fs::path refs_dir = out_dir / "refs";
    std::error_code ec;
    fs::create_directories(refs_dir, ec);
    if (ec) {
        std::fprintf(stderr, "ERROR: create_directories(%s): %s\n",
                     refs_dir.string().c_str(), ec.message().c_str());
        return 1;
    }

    // Per-track noise seeds derived from the master seed by simple offsets.
    // Different seeds give each track an independent noise realisation; the
    // master is fixed so the whole fixture is byte-stable.
    const std::array<uint32_t, 3> track_seeds = {
        kMasterSeed + 1u, kMasterSeed + 2u, kMasterSeed + 3u
    };
    const std::array<EnvelopeFn, 3> envelopes = {
        &track1_envelope, &track2_envelope, &track3_envelope
    };
    const std::array<std::string, 3> ref_names = {
        "01_track1.wav", "02_track2.wav", "03_track3.wav"
    };

    // 1) Generate per-track audio buffers.
    std::array<std::vector<float>, 3> track_audio;
    for (size_t i = 0; i < 3; ++i) {
        track_audio[i] = generate_track(envelopes[i], track_seeds[i]);
    }

    // 2) Write the per-track reference files.
    for (size_t i = 0; i < 3; ++i) {
        const fs::path p = refs_dir / ref_names[i];
        if (!write_wav_mono_pcm16(p, track_audio[i])) {
            std::fprintf(stderr, "ERROR: write %s\n", p.string().c_str());
            return 1;
        }
    }

    // 3) Build the concatenated vinyl rip and capture ground-truth boundaries.
    std::vector<float> vinyl;
    std::array<TrackTruth, 3> truth{};

    // Lead-in noise (its own seed so it's deterministic but distinct).
    {
        auto leadin = generate_gap_noise(kLeadInSeconds, kMasterSeed + 100u);
        vinyl.insert(vinyl.end(), leadin.begin(), leadin.end());
    }

    for (size_t i = 0; i < 3; ++i) {
        truth[i].name = ref_names[i];
        truth[i].start_sample = static_cast<int64_t>(vinyl.size());
        vinyl.insert(vinyl.end(),
                     track_audio[i].begin(),
                     track_audio[i].end());
        truth[i].end_sample = static_cast<int64_t>(vinyl.size()) - 1;

        // Append a gap after every track except the last.
        if (i + 1 < 3) {
            auto gap = generate_gap_noise(
                kGapSeconds, kMasterSeed + 200u + static_cast<uint32_t>(i));
            vinyl.insert(vinyl.end(), gap.begin(), gap.end());
        }
    }

    // 4) Write vinyl.wav.
    const fs::path vinyl_path = out_dir / "vinyl.wav";
    if (!write_wav_mono_pcm16(vinyl_path, vinyl)) {
        std::fprintf(stderr, "ERROR: write %s\n", vinyl_path.string().c_str());
        return 1;
    }

    // 5) Write the manifest.
    const fs::path manifest_path = out_dir / "manifest.txt";
    if (!write_manifest(manifest_path, kFixtureSampleRate, truth)) {
        std::fprintf(stderr, "ERROR: write %s\n",
                     manifest_path.string().c_str());
        return 1;
    }

    std::printf("ref_v1 fixture written to %s\n", out_dir.string().c_str());
    return 0;
}
