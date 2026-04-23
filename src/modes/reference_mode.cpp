#include "modes/reference_mode.hpp"
#include "core/audio_buffer.hpp"
#include "core/audio_file.hpp"
#include "core/correlation.hpp"
#include "core/music_detection.hpp"
#include "core/verbose.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <regex>
#include <iomanip>
#include <sstream>

namespace mwaac {

namespace {

// Natural sort helper - compares numeric parts as integers
bool natural_less(const std::string& a, const std::string& b) {
    auto get_parts = [](const std::string& s) -> std::vector<std::pair<bool, std::string>> {
        std::vector<std::pair<bool, std::string>> parts;
        std::string current;
        bool is_digit = false;
        
        for (char c : s) {
            bool digit = std::isdigit(static_cast<unsigned char>(c));
            if (!current.empty() && digit != is_digit) {
                parts.push_back({is_digit, current});
                current.clear();
            }
            current += c;
            is_digit = digit;
        }
        if (!current.empty()) {
            parts.push_back({is_digit, current});
        }
        return parts;
    };
    
    auto a_parts = get_parts(a);
    auto b_parts = get_parts(b);
    
    for (size_t i = 0; i < std::min(a_parts.size(), b_parts.size()); ++i) {
        if (a_parts[i].first && b_parts[i].first) {
            // Both numeric - compare as numbers
            long long num_a = std::stoll(a_parts[i].second);
            long long num_b = std::stoll(b_parts[i].second);
            if (num_a != num_b) return num_a < num_b;
        } else {
            // At least one is text - compare as strings
            if (a_parts[i].second != b_parts[i].second) {
                return a_parts[i].second < b_parts[i].second;
            }
        }
    }
    return a_parts.size() < b_parts.size();
}

std::vector<std::filesystem::path> natural_sort(
    std::vector<std::filesystem::path> paths) 
{
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return natural_less(a.filename().string(), b.filename().string());
    });
    return paths;
}

// Longest contiguous low-energy run inside [search_start, search_end) of `audio`.
// Returns {run_start_sample, run_end_sample_exclusive} if a run >= min_duration
// samples is found; otherwise nullopt.
struct SilenceRun {
    int64_t start{0};
    int64_t end{0};  // exclusive
    int64_t duration() const { return end - start; }
};

std::optional<SilenceRun> find_longest_silence(
    std::span<const float> audio,
    int sample_rate,
    int64_t search_start,
    int64_t search_end,
    double threshold_db,
    double min_duration_s)
{
    search_start = std::max(int64_t{0}, search_start);
    search_end   = std::min(static_cast<int64_t>(audio.size()), search_end);
    if (search_end - search_start <= 0) return std::nullopt;

    const int64_t frame_size = std::max<int64_t>(1, sample_rate / 10);  // 100 ms
    const int64_t min_duration_samples = static_cast<int64_t>(min_duration_s * sample_rate);
    // Convert dBFS threshold to linear RMS (samples are already normalized to ~[-1, 1])
    const double threshold_linear = std::pow(10.0, threshold_db / 20.0);

    SilenceRun best{0, 0};
    int64_t run_start = -1;

    for (int64_t frame_start = search_start; frame_start < search_end; frame_start += frame_size) {
        int64_t frame_end = std::min(frame_start + frame_size, search_end);
        double sum_sq = 0.0;
        for (int64_t i = frame_start; i < frame_end; ++i) {
            double s = audio[i];
            sum_sq += s * s;
        }
        double rms = std::sqrt(sum_sq / static_cast<double>(frame_end - frame_start));

        bool is_silent = rms < threshold_linear;
        if (is_silent) {
            if (run_start < 0) run_start = frame_start;
            // Track the current run's end; update best when it grows past previous best
            int64_t run_len = frame_end - run_start;
            if (run_len > best.duration()) {
                best.start = run_start;
                best.end   = frame_end;
            }
        } else {
            run_start = -1;
        }
    }

    if (best.duration() >= min_duration_samples) return best;
    return std::nullopt;
}

// Decide the trimmed end (exclusive) for each track given the detected starts,
// reference durations, and the vinyl audio. Uses reference duration to identify
// suspiciously long gaps, then finds the actual dead-air inside them.
struct EndDecision {
    int64_t end_sample_excl{0};  // exclusive end in `audio` coordinates
    std::string reason;          // for verbose logging
    int64_t trimmed_samples{0};  // how much we cut vs. next_start - 1
};

std::vector<EndDecision> compute_track_ends(
    const AudioBuffer& vinyl,
    const std::vector<ReferenceTrack>& tracks,
    const std::vector<std::pair<int64_t, double>>& offsets)
{
    const int sr = vinyl.sample_rate;
    const int64_t SMALL_GAP        = static_cast<int64_t>(5.0 * sr);
    const int64_t TAIL_CAP         = static_cast<int64_t>(2.0 * sr);
    const int64_t FLIP_MIN_SILENCE = static_cast<int64_t>(8.0 * sr);
    const int64_t TAIL_PAD         = static_cast<int64_t>(1.0 * sr);
    const double  SILENCE_DB       = -45.0;
    const double  MIN_RUN_S        = 3.0;

    std::vector<EndDecision> out(offsets.size());

    for (size_t i = 0; i < offsets.size(); ++i) {
        int64_t start     = offsets[i].first;
        int64_t ref_dur   = tracks[i].duration_samples;
        int64_t ref_end   = start + ref_dur;
        int64_t next_start = (i + 1 < offsets.size())
            ? offsets[i + 1].first
            : static_cast<int64_t>(vinyl.samples.size());

        int64_t natural_end_excl = next_start;  // exclusive
        int64_t extra = natural_end_excl - ref_end;

        int64_t chosen_end;  // exclusive
        std::string reason;

        if (extra <= SMALL_GAP) {
            // Normal gap — preserve natural decay up to TAIL_CAP past ref_end
            chosen_end = std::min(natural_end_excl, ref_end + TAIL_CAP);
            reason = "normal gap";
        } else {
            auto silence = find_longest_silence(
                vinyl.samples, sr, ref_end, natural_end_excl, SILENCE_DB, MIN_RUN_S);
            if (silence && silence->duration() >= FLIP_MIN_SILENCE) {
                chosen_end = silence->start + TAIL_PAD;
                // Don't go past natural_end, and never before ref_end
                chosen_end = std::clamp(chosen_end, ref_end, natural_end_excl);
                reason = "flip gap trimmed";
            } else {
                // Long gap without a clear flip-silence block — trust the ref
                chosen_end = std::min(natural_end_excl, ref_end + TAIL_CAP);
                reason = "long gap, no flip signature — using ref duration";
            }
        }

        // For the last track specifically: cap at ref_end + TAIL_CAP rather than
        // letting runout groove / outro silence ride to end-of-file.
        if (i + 1 == offsets.size()) {
            int64_t capped = std::min(
                static_cast<int64_t>(vinyl.samples.size()),
                ref_end + TAIL_CAP);
            if (capped < chosen_end) {
                chosen_end = capped;
                reason = "last track — capped at ref_end + tail";
            }
        }

        out[i].end_sample_excl = chosen_end;
        out[i].reason = reason;
        out[i].trimmed_samples = natural_end_excl - chosen_end;
    }

    return out;
}

// Find the first "music onset" in a signal: the first 10ms frame whose RMS
// rises above `threshold_db` (dBFS, assuming samples roughly in [-1, 1]) and
// stays above for at least `min_sustain_ms`. Returns the absolute sample
// offset of the onset frame's start, or -1 if none found within the search
// window. The threshold must sit above any surface-noise floor in the signal.
int64_t find_music_onset(
    std::span<const float> audio,
    int sample_rate,
    int64_t search_start_sample,
    double max_search_seconds,
    double threshold_db,
    double min_sustain_ms)
{
    const int64_t frame_size = std::max<int64_t>(1, sample_rate / 100);  // 10 ms
    const int64_t min_sustain_frames =
        std::max<int64_t>(1, static_cast<int64_t>(min_sustain_ms / 10.0));
    const double threshold_linear = std::pow(10.0, threshold_db / 20.0);

    int64_t search_start = std::max<int64_t>(0, search_start_sample);
    int64_t search_end = std::min(
        static_cast<int64_t>(audio.size()),
        search_start + static_cast<int64_t>(max_search_seconds * sample_rate));

    int64_t consecutive_above = 0;
    int64_t first_above_frame = -1;

    for (int64_t frame_start = search_start;
         frame_start + frame_size <= search_end;
         frame_start += frame_size)
    {
        double sum_sq = 0.0;
        for (int64_t i = frame_start; i < frame_start + frame_size; ++i) {
            double s = audio[i];
            sum_sq += s * s;
        }
        double rms = std::sqrt(sum_sq / frame_size);

        if (rms > threshold_linear) {
            if (consecutive_above == 0) first_above_frame = frame_start;
            ++consecutive_above;
            if (consecutive_above >= min_sustain_frames) {
                return first_above_frame;
            }
        } else {
            consecutive_above = 0;
            first_above_frame = -1;
        }
    }
    return -1;
}

// Estimate a signal's noise floor as the p-th percentile of 10ms-frame RMS
// values over the provided window. Used to set an adaptive onset threshold
// for vinyl (which has surface noise above absolute silence).
double estimate_noise_floor_db(
    std::span<const float> audio,
    int sample_rate,
    int64_t start_sample,
    int64_t window_samples,
    double percentile = 0.10)
{
    const int64_t frame_size = std::max<int64_t>(1, sample_rate / 100);
    int64_t begin = std::max<int64_t>(0, start_sample);
    int64_t end = std::min(
        static_cast<int64_t>(audio.size()),
        begin + window_samples);

    std::vector<double> frame_rms;
    frame_rms.reserve((end - begin) / frame_size + 1);
    for (int64_t f = begin; f + frame_size <= end; f += frame_size) {
        double sum_sq = 0.0;
        for (int64_t i = f; i < f + frame_size; ++i) {
            double s = audio[i];
            sum_sq += s * s;
        }
        frame_rms.push_back(std::sqrt(sum_sq / frame_size));
    }
    if (frame_rms.empty()) return -120.0;

    size_t k = static_cast<size_t>(percentile * (frame_rms.size() - 1));
    std::nth_element(frame_rms.begin(), frame_rms.begin() + k, frame_rms.end());
    double floor_linear = std::max(frame_rms[k], 1e-9);
    return 20.0 * std::log10(floor_linear);
}

// One snippet's vote for where the track actually starts.
// implied_track_start is the vinyl sample (slice-local) where ref[0]
// would land if we trust this snippet's peak. snippet_conf is the peak
// normalized cross-correlation value (0..1).
struct SnippetVote {
    int64_t implied_track_start{-1};
    double  snippet_conf{0.0};
    int64_t snippet_offset_in_ref{0};  // for logging/debug
};

// Correlate a 5-second (or given length) slice of reference, starting at
// `snippet_offset_in_ref`, against a narrow window of the vinyl at full
// resolution. Returns the track-start implied by the peak, together with
// the correlation confidence. The track start is derived by subtracting
// `snippet_offset_in_ref` from the vinyl position where the snippet was
// located — this works regardless of whether the snippet came from the
// intro, the middle, or the tail of the track.
SnippetVote correlate_snippet(
    std::span<const float> vinyl_processed,
    std::span<const float> ref_processed,
    int sample_rate,
    int64_t expected_track_start,   // slice-local pass-1 estimate
    int64_t snippet_offset_in_ref,  // where in the ref to pull the snippet
    double snippet_seconds,
    double search_radius_s)
{
    SnippetVote vote;
    vote.snippet_offset_in_ref = snippet_offset_in_ref;

    if (snippet_offset_in_ref < 0) return vote;
    int64_t snippet_len = std::min(
        static_cast<int64_t>(snippet_seconds * sample_rate),
        static_cast<int64_t>(ref_processed.size()) - snippet_offset_in_ref);
    if (snippet_len < sample_rate / 2) return vote;  // need ≥0.5 s

    // Where we expect the snippet to land in the vinyl:
    int64_t expected_snippet_pos = expected_track_start + snippet_offset_in_ref;
    int64_t search_radius = static_cast<int64_t>(search_radius_s * sample_rate);
    int64_t window_start = std::max<int64_t>(0, expected_snippet_pos - search_radius);
    int64_t window_end = std::min(
        static_cast<int64_t>(vinyl_processed.size()),
        expected_snippet_pos + search_radius + snippet_len);
    if (window_end - window_start < snippet_len + 1) return vote;

    // Zero-mean the snippet
    double ref_mean = 0.0;
    for (int64_t i = 0; i < snippet_len; ++i) {
        ref_mean += ref_processed[snippet_offset_in_ref + i];
    }
    ref_mean /= snippet_len;

    std::vector<double> ref_norm(snippet_len);
    double ref_energy = 0.0;
    for (int64_t i = 0; i < snippet_len; ++i) {
        ref_norm[i] = ref_processed[snippet_offset_in_ref + i] - ref_mean;
        ref_energy += ref_norm[i] * ref_norm[i];
    }
    if (ref_energy < 1e-10) return vote;

    // Prefix sums for O(1) per-lag target mean/energy
    int64_t window_size = window_end - window_start;
    std::vector<double> prefix_sum(window_size + 1, 0.0);
    std::vector<double> prefix_sum_sq(window_size + 1, 0.0);
    for (int64_t i = 0; i < window_size; ++i) {
        double s = vinyl_processed[window_start + i];
        prefix_sum[i + 1]    = prefix_sum[i]    + s;
        prefix_sum_sq[i + 1] = prefix_sum_sq[i] + s * s;
    }

    int64_t max_lag = window_size - snippet_len;
    double best_corr = -std::numeric_limits<double>::infinity();
    int64_t best_lag = expected_snippet_pos - window_start;  // default

    for (int64_t lag = 0; lag <= max_lag; ++lag) {
        double tgt_sum    = prefix_sum[lag + snippet_len]    - prefix_sum[lag];
        double tgt_sum_sq = prefix_sum_sq[lag + snippet_len] - prefix_sum_sq[lag];
        double tgt_mean   = tgt_sum / snippet_len;
        double tgt_energy = tgt_sum_sq - (tgt_sum * tgt_sum) / snippet_len;
        if (tgt_energy < 1e-10) continue;

        double sum = 0.0;
        const float* tgt_ptr = vinyl_processed.data() + window_start + lag;
        for (int64_t i = 0; i < snippet_len; ++i) {
            double t = tgt_ptr[i] - tgt_mean;
            sum += ref_norm[i] * t;
        }

        double norm = std::sqrt(ref_energy * tgt_energy);
        double corr = sum / norm;
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }

    if (!std::isfinite(best_corr)) best_corr = 0.0;

    // Vinyl position where the snippet landed:
    int64_t snippet_vinyl_pos = window_start + best_lag;
    // Imply the track start (where ref[0] would be):
    vote.implied_track_start = snippet_vinyl_pos - snippet_offset_in_ref;
    vote.snippet_conf = best_corr;
    return vote;
}

// Pass-2 refinement via multi-snippet voting. Runs `correlate_snippet` at
// three offsets in the reference: just past its music onset, 40% through,
// and 80% through. Each snippet independently implies a track-start via
// correlation peak. Votes are combined by a confidence-weighted median —
// robust to a single snippet landing on a spurious match (e.g. when a
// section of the song resembles another part). The `disagreement_s` output
// is the max spread among snippet votes that crossed the confidence floor;
// large values indicate an unreliable track worth flagging.
struct MultiRefineResult {
    int64_t track_start{-1};
    double  top_conf{0.0};         // strongest single-snippet confidence
    double  disagreement_s{0.0};   // max spread among accepted votes (seconds)
    int     accepted_snippets{0};
    std::vector<SnippetVote> votes;
};

MultiRefineResult multi_snippet_refine(
    std::span<const float> vinyl_processed,
    std::span<const float> ref_processed,
    int sample_rate,
    int64_t coarse_track_start,  // slice-local pass-1 estimate (= track start, not music)
    double snippet_seconds = 5.0)
{
    MultiRefineResult out;

    // Snippet A (Vote 1): just past the reference's first music onset.
    // Runs with a tighter ±1.5 s window to match the pass-1 single-snippet
    // behavior — this is the *primary* position estimator.
    // Snippets B, C (Votes 2, 3): from 40% and 80% through the track.
    // Wider ±2.5 s window so they can catch larger drifts, but they're
    // used only as validators or for recovery when Vote 1 is unreliable.
    int64_t onset = find_music_onset(
        ref_processed, sample_rate, 0, 30.0, -40.0, 80.0);
    if (onset < 0) onset = 0;

    int64_t ref_size = static_cast<int64_t>(ref_processed.size());
    int64_t snippet_samples = static_cast<int64_t>(snippet_seconds * sample_rate);

    struct SnippetSpec { int64_t offset; double radius; };
    std::vector<SnippetSpec> specs = {
        { onset,                                1.5 },
        { std::max<int64_t>(onset, (ref_size * 2) / 5), 2.5 },
        { std::max<int64_t>(onset, (ref_size * 4) / 5), 2.5 },
    };
    for (auto& s : specs) {
        if (s.offset + snippet_samples > ref_size) {
            s.offset = std::max<int64_t>(0, ref_size - snippet_samples);
        }
    }

    for (const auto& s : specs) {
        SnippetVote v = correlate_snippet(
            vinyl_processed, ref_processed, sample_rate,
            coarse_track_start, s.offset, snippet_seconds, s.radius);
        out.votes.push_back(v);
    }

    constexpr double VOTE_CONF_MIN = 0.10;
    constexpr double VOTE1_TRUST   = 0.20;  // Vote 1 wins outright above this

    for (auto& v : out.votes) {
        if (v.implied_track_start >= 0 && v.snippet_conf >= VOTE_CONF_MIN) {
            out.accepted_snippets++;
        }
        out.top_conf = std::max(out.top_conf, v.snippet_conf);
    }

    // Compute spread across the valid votes for telemetry
    std::vector<int64_t> valid_positions;
    for (auto& v : out.votes) {
        if (v.implied_track_start >= 0 && v.snippet_conf >= VOTE_CONF_MIN) {
            valid_positions.push_back(v.implied_track_start);
        }
    }
    if (!valid_positions.empty()) {
        int64_t mn = *std::min_element(valid_positions.begin(), valid_positions.end());
        int64_t mx = *std::max_element(valid_positions.begin(), valid_positions.end());
        out.disagreement_s = static_cast<double>(mx - mn) / sample_rate;
    }

    const SnippetVote& vote1 = out.votes[0];
    bool vote1_good = vote1.implied_track_start >= 0
                      && vote1.snippet_conf >= VOTE1_TRUST;

    if (vote1_good) {
        // Vote 1 is the primary position estimator, matching Fix-1 behavior
        out.track_start = vote1.implied_track_start;
        return out;
    }

    // Vote 1 weak — recovery: use Votes 2/3 if they agree closely and both
    // have reasonable confidence. Otherwise fall back to coarse.
    const SnippetVote& v2 = out.votes[1];
    const SnippetVote& v3 = out.votes[2];
    bool v23_ok = v2.implied_track_start >= 0 && v3.implied_track_start >= 0
                  && v2.snippet_conf >= VOTE_CONF_MIN
                  && v3.snippet_conf >= VOTE_CONF_MIN;
    double v23_spread_s = v23_ok
        ? std::abs(static_cast<double>(v2.implied_track_start -
                                       v3.implied_track_start)) / sample_rate
        : 0.0;

    if (v23_ok && v23_spread_s <= 0.3) {
        // Recovery case: Votes 2 and 3 agree tightly; take the higher-conf one
        out.track_start = (v2.snippet_conf >= v3.snippet_conf)
            ? v2.implied_track_start
            : v3.implied_track_start;
    } else {
        out.track_start = coarse_track_start;  // nothing reliable — trust coarse
    }
    return out;
}

// Jump past a long contiguous silence at `start_sample`, but only if the
// silence is at least `min_skip_seconds` long. That threshold matters: a
// 1-second silence is usually a quiet fade-in or inter-track gap that
// should be preserved; only multi-second silences are real flip gaps.
//
// Returns start_sample unchanged when:
//   - the start isn't silent,
//   - the silent run is shorter than min_skip_seconds,
//   - or no music resumes within max_skip_seconds.
// Otherwise returns the sample where music resumes (sustained above
// threshold for min_music_ms).
int64_t skip_leading_silence(
    std::span<const float> vinyl_samples,
    int sample_rate,
    int64_t start_sample,
    double threshold_db = -50.0,
    double min_skip_seconds = 3.0,
    double max_skip_seconds = 180.0,
    double min_music_ms = 200.0)
{
    const int64_t frame_size = std::max<int64_t>(1, sample_rate / 20);  // 50 ms
    const int64_t min_music_frames =
        std::max<int64_t>(1, static_cast<int64_t>(min_music_ms / 50.0));
    const int64_t min_skip_samples =
        static_cast<int64_t>(min_skip_seconds * sample_rate);
    const double threshold_linear = std::pow(10.0, threshold_db / 20.0);

    start_sample = std::max<int64_t>(0, start_sample);
    int64_t end = std::min(
        static_cast<int64_t>(vinyl_samples.size()),
        start_sample + static_cast<int64_t>(max_skip_seconds * sample_rate));

    auto frame_rms = [&](int64_t f) {
        double ss = 0.0;
        int64_t n = std::min(frame_size, end - f);
        if (n <= 0) return 0.0;
        for (int64_t i = 0; i < n; ++i) {
            double s = vinyl_samples[f + i];
            ss += s * s;
        }
        return std::sqrt(ss / n);
    };

    // Is the start itself music? Average a few frames for stability.
    double head_ss = 0.0;
    int64_t head_samples = 0;
    for (int64_t i = 0; i < 4 && start_sample + (i + 1) * frame_size <= end; ++i) {
        head_ss += frame_rms(start_sample + i * frame_size) *
                   frame_rms(start_sample + i * frame_size);
        ++head_samples;
    }
    double head_avg_rms = head_samples > 0
        ? std::sqrt(head_ss / head_samples)
        : 0.0;
    if (head_avg_rms >= threshold_linear) {
        return start_sample;  // already in music
    }

    // Silent. Walk forward and track both the end of this silence and the
    // resumption of sustained music.
    int64_t music_run = 0;
    int64_t music_start = -1;
    for (int64_t f = start_sample; f + frame_size <= end; f += frame_size) {
        double r = frame_rms(f);
        if (r > threshold_linear) {
            if (music_run == 0) music_start = f;
            ++music_run;
            if (music_run >= min_music_frames) {
                int64_t silence_length = music_start - start_sample;
                if (silence_length >= min_skip_samples) {
                    return music_start;  // real flip-gap-sized silence
                }
                return start_sample;  // short silence — likely fade-in, keep
            }
        } else {
            music_run = 0;
            music_start = -1;
        }
    }
    return start_sample;  // no music found in window — give up
}

// Check if path is audio file
bool is_audio_file(const std::filesystem::path& p) {
    static const std::vector<std::string> exts = {
        ".wav", ".aiff", ".aif", ".flac", ".mp3", ".ogg", ".m4a"
    };
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

} // anonymous namespace

Expected<std::vector<ReferenceTrack>, ReferenceError> load_reference_tracks(
    const std::filesystem::path& reference_dir,
    int sample_rate)
{
    if (!std::filesystem::is_directory(reference_dir)) {
        return ReferenceError::ReferenceLoadFailed;
    }
    
    std::vector<std::filesystem::path> audio_files;
    for (const auto& entry : std::filesystem::directory_iterator(reference_dir)) {
        if (entry.is_regular_file() && is_audio_file(entry.path())) {
            audio_files.push_back(entry.path());
        }
    }
    
    if (audio_files.empty()) {
        return ReferenceError::NoTracksFound;
    }
    
    audio_files = natural_sort(audio_files);
    
    std::vector<ReferenceTrack> tracks;
    for (const auto& path : audio_files) {
        auto result = load_audio_mono(path, sample_rate);
        if (!result.ok()) {
            continue;  // Skip failed loads
        }
        
        ReferenceTrack track;
        track.path = path;
        track.audio = std::move(result.value());
        track.duration_samples = track.audio.samples.size();
        tracks.push_back(std::move(track));
    }
    
    if (tracks.empty()) {
        return ReferenceError::ReferenceLoadFailed;
    }
    
    return tracks;
}

std::vector<std::pair<int64_t, double>> align_per_track(
    const AudioBuffer& vinyl,
    const std::vector<ReferenceTrack>& tracks,
    int64_t music_start_sample)
{
    // Minimum correlation to trust a found position. Below this (or if the
    // correlation returns non-finite), we fall back to expected_position
    // so one bad match doesn't cascade into every subsequent track.
    constexpr double MIN_CONFIDENCE = 0.05;

    std::vector<std::pair<int64_t, double>> offsets;
    int64_t expected_position = music_start_sample;

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks[i];

        if (g_verbose) {
            verbose("  Aligning track " + std::to_string(i + 1) + "/" +
                   std::to_string(tracks.size()) + ": " + track.path.filename().string());
        }

        auto ref_processed = preprocess_for_correlation(track.audio.samples, track.audio.sample_rate);

        // If the expected position sits in a long silent stretch (flip gap),
        // jump past the silence to where music actually resumes. This keeps
        // the correlation window focused on real content and not on dead air.
        int64_t pre_skip_expected = expected_position;
        expected_position = skip_leading_silence(
            vinyl.samples, vinyl.sample_rate, expected_position);
        if (g_verbose && expected_position != pre_skip_expected) {
            double skipped = static_cast<double>(expected_position - pre_skip_expected)
                             / vinyl.sample_rate;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "    Flip-gap skip: " << skipped << "s of silence before this track";
            verbose(oss.str());
        }

        int64_t margin_samples = 10 * vinyl.sample_rate;
        int64_t window_size = track.duration_samples + margin_samples;

        int64_t vinyl_start_idx = std::max(int64_t{0}, expected_position - margin_samples / 2);
        int64_t vinyl_end_idx = std::min(
            static_cast<int64_t>(vinyl.samples.size()),
            expected_position + window_size
        );

        int64_t chosen_pos;
        double confidence = 0.0;

        if (vinyl_start_idx >= static_cast<int64_t>(vinyl.samples.size()) ||
            vinyl_end_idx - vinyl_start_idx < track.duration_samples) {
            // No room left (or not enough) for a real search — use expected position
            chosen_pos = expected_position;
        } else {
            std::vector<float> vinyl_slice(
                vinyl.samples.begin() + vinyl_start_idx,
                vinyl.samples.begin() + vinyl_end_idx
            );
            auto vinyl_processed = preprocess_for_correlation(vinyl_slice, vinyl.sample_rate);

            // Downsample factor 100 gives ~1ms accuracy at 44.1kHz
            auto result = cross_correlate_fast(ref_processed, vinyl_processed, 100);
            confidence = result.peak_value;

            if (std::isfinite(confidence) && confidence >= MIN_CONFIDENCE) {
                int64_t coarse_pos = vinyl_start_idx + result.lag;

                // Pass 2: multi-snippet voting.
                // Correlate three snippets from different points in the
                // reference (post-onset, 40%, 80%) and combine votes by
                // confidence-weighted median. Robust against a single
                // snippet landing on a spurious match, and catches cases
                // where the start snippet is non-distinctive.
                MultiRefineResult mr = multi_snippet_refine(
                    vinyl_processed, ref_processed,
                    vinyl.sample_rate, result.lag,   // slice-local coarse track start
                    /*snippet_seconds=*/5.0);

                if (g_verbose) {
                    for (size_t s = 0; s < mr.votes.size(); ++s) {
                        const auto& v = mr.votes[s];
                        double offset_s = static_cast<double>(v.snippet_offset_in_ref) /
                                          vinyl.sample_rate;
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(3);
                        oss << "    Vote " << (s + 1) << " @ref+" << offset_s << "s:";
                        if (v.implied_track_start < 0) {
                            oss << " (snippet too short / out of bounds)";
                        } else {
                            double impl_delta = static_cast<double>(
                                (vinyl_start_idx + v.implied_track_start) - coarse_pos) /
                                vinyl.sample_rate;
                            oss << " implies " << impl_delta << "s shift, conf "
                                << v.snippet_conf;
                        }
                        verbose(oss.str());
                    }
                }

                constexpr double ACCEPT_CONF_MIN = 0.10;
                bool accept = mr.accepted_snippets > 0 && mr.top_conf >= ACCEPT_CONF_MIN;

                if (accept) {
                    int64_t refined_pos = vinyl_start_idx + mr.track_start;
                    int64_t delta = refined_pos - coarse_pos;
                    if (g_verbose) {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(3);
                        oss << "    Multi-refine: " << mr.accepted_snippets
                            << "/3 snippets, shift "
                            << (static_cast<double>(delta) / vinyl.sample_rate) << "s, "
                            << "top_conf " << mr.top_conf << ", "
                            << "spread " << mr.disagreement_s << "s";
                        if (mr.disagreement_s > 0.3) oss << " [WEAK]";
                        verbose(oss.str());
                    }
                    chosen_pos = refined_pos;
                    confidence = std::max(confidence, mr.top_conf);
                } else {
                    if (g_verbose) {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(3);
                        oss << "    Multi-refine: rejected (top_conf "
                            << mr.top_conf << " < " << ACCEPT_CONF_MIN
                            << ") — keeping coarse";
                        verbose(oss.str());
                    }
                    chosen_pos = coarse_pos;
                }
            } else {
                chosen_pos = expected_position;
                if (g_verbose) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(3) << confidence;
                    verbose("    Low confidence (" + oss.str() + ") — falling back to expected position");
                }
            }
        }

        chosen_pos = std::clamp(chosen_pos,
                                int64_t{0},
                                static_cast<int64_t>(vinyl.samples.size()) - 1);

        offsets.push_back({chosen_pos, confidence});
        expected_position = chosen_pos + track.duration_samples;
    }

    return offsets;
}

Expected<AnalysisResult, ReferenceError> analyze_reference_mode(
    const std::filesystem::path& vinyl_path,
    const std::filesystem::path& reference_path,
    int analysis_sr)
{
    VerboseTimer timer("reference mode analysis");
    
    // Load vinyl
    verbose("Loading vinyl...");
    auto vinyl_result = load_audio_mono(vinyl_path, analysis_sr);
    if (!vinyl_result.ok()) {
        verbose("ERROR: Failed to load vinyl");
        return ReferenceError::VinylLoadFailed;
    }
    auto vinyl = std::move(vinyl_result.value());
    
    if (g_verbose) {
        verbose("  Vinyl loaded: " + std::to_string(vinyl.samples.size()) + 
               " samples at " + std::to_string(vinyl.sample_rate) + " Hz");
    }
    
    // Detect music start
    verbose("Detecting music start...");
    int64_t music_start = detect_music_start(vinyl.samples, analysis_sr);
    
    if (g_verbose) {
        double music_start_sec = static_cast<double>(music_start) / analysis_sr;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << music_start_sec;
        verbose("  Music starts at sample " + std::to_string(music_start) + 
               " (" + oss.str() + "s)");
    }
    
    // Load reference tracks
    verbose("Loading reference tracks...");
    auto tracks_result = load_reference_tracks(reference_path, analysis_sr);
    if (!tracks_result) {
        verbose("ERROR: Failed to load reference tracks");
        return tracks_result.error();
    }
    auto tracks = std::move(tracks_result.value());
    
    if (g_verbose) {
        verbose("  Loaded " + std::to_string(tracks.size()) + " reference track(s)");
        for (size_t i = 0; i < tracks.size(); ++i) {
            verbose("    Track " + std::to_string(i + 1) + ": " + 
                    tracks[i].path.filename().string() + 
                    " (" + std::to_string(tracks[i].duration_samples) + " samples)");
        }
    }
    
    // Align each track
    verbose("Aligning tracks to vinyl...");
    auto offsets = align_per_track(vinyl, tracks, music_start);
    
    if (g_verbose) {
        verbose_section("Per-Track Alignment Results");
        for (size_t i = 0; i < offsets.size(); ++i) {
            double offset_sec = static_cast<double>(offsets[i].first) / analysis_sr;
            int min = static_cast<int>(offset_sec) / 60;
            int sec = static_cast<int>(offset_sec) % 60;
            std::ostringstream conf_oss;
            conf_oss << std::fixed << std::setprecision(3) << offsets[i].second;
            std::ostringstream pos_oss;
            pos_oss << std::fixed << std::setprecision(2) << offset_sec;
            verbose("  Track " + std::to_string(i + 1) + " (" + tracks[i].path.filename().string() + "):");
            verbose("    Position: " + std::to_string(offsets[i].first) + " samples" +
                    " (" + pos_oss.str() + "s = " + std::to_string(min) + ":" + std::to_string(sec) + ")");
            verbose("    Confidence: " + conf_oss.str());
        }
    }
    
    // Decide trimmed end (exclusive) for each track in analysis_sr coordinates.
    // Handles flip-gap dead space and excessive inter-track silence.
    verbose("Trimming dead space between tracks...");
    auto end_decisions = compute_track_ends(vinyl, tracks, offsets);

    if (g_verbose) {
        verbose_section("Track End Decisions");
        for (size_t i = 0; i < end_decisions.size(); ++i) {
            double trimmed_s = static_cast<double>(end_decisions[i].trimmed_samples) / analysis_sr;
            double end_s     = static_cast<double>(end_decisions[i].end_sample_excl) / analysis_sr;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2);
            oss << "  Track " << (i + 1) << ": end @ " << end_s << "s"
                << " (" << end_decisions[i].reason
                << "; trimmed " << trimmed_s << "s)";
            verbose(oss.str());
        }
    }

    // Build split points
    std::vector<SplitPoint> split_points;

    // Get native sample rate from the actual vinyl file
    int native_sr = analysis_sr;  // Fallback
    auto audio_file = AudioFile::open(vinyl_path);
    if (audio_file) {
        native_sr = audio_file.value().info().sample_rate;
        if (g_verbose) {
            verbose("  Native sample rate: " + std::to_string(native_sr) + " Hz");
        }
    }

    for (size_t i = 0; i < offsets.size(); ++i) {
        SplitPoint sp;
        sp.start_sample = offsets[i].first * native_sr / analysis_sr;
        // Convert exclusive end in analysis_sr to inclusive end in native_sr
        sp.end_sample = (end_decisions[i].end_sample_excl * native_sr / analysis_sr) - 1;

        sp.confidence = offsets[i].second;
        sp.source = "reference";
        sp.evidence["track_index"] = static_cast<double>(i);
        sp.evidence["track_name"] = tracks[i].path.filename().string();
        sp.evidence["end_reason"]  = end_decisions[i].reason;
        sp.evidence["trimmed_samples_analysis_sr"] =
            static_cast<double>(end_decisions[i].trimmed_samples);

        split_points.push_back(sp);
    }
    
    // Build result
    AnalysisResult result;
    result.split_points = std::move(split_points);
    result.mode = "reference";
    result.metadata["music_start_sample"] = static_cast<double>(music_start);
    result.metadata["analysis_sr"] = static_cast<double>(analysis_sr);
    result.metadata["num_tracks"] = static_cast<double>(tracks.size());
    
    if (g_verbose) {
        verbose_section("Analysis Parameters");
        verbose("  Analysis sample rate: " + std::to_string(analysis_sr) + " Hz");
        verbose("  Music start sample: " + std::to_string(music_start));
        verbose("  Number of tracks: " + std::to_string(tracks.size()));
        verbose("  Output sample rate: " + std::to_string(native_sr) + " Hz");
    }
    
    return result;
}

} // namespace mwaac