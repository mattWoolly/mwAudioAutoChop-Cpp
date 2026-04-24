#include "modes/reaper_export.hpp"
#include "core/verbose.hpp"
#include <sndfile.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <string>

namespace mwaac {

namespace {

// Natural-sort compare on filenames (kept local so ref_export doesn't
// depend on the reference_mode internals). Splits into text/number runs
// and compares numeric runs as integers so "Track 2.wav" < "Track 10.wav".
bool natural_less_filename(const std::filesystem::path& a,
                           const std::filesystem::path& b) {
    const std::string as = a.filename().string();
    const std::string bs = b.filename().string();

    auto parts = [](const std::string& s) {
        std::vector<std::pair<bool, std::string>> out;
        std::string cur;
        bool is_digit = false;
        for (char c : s) {
            bool d = std::isdigit(static_cast<unsigned char>(c));
            if (!cur.empty() && d != is_digit) { out.push_back({is_digit, cur}); cur.clear(); }
            cur += c;
            is_digit = d;
        }
        if (!cur.empty()) out.push_back({is_digit, cur});
        return out;
    };

    auto ap = parts(as), bp = parts(bs);
    for (size_t i = 0; i < std::min(ap.size(), bp.size()); ++i) {
        if (ap[i].first && bp[i].first) {
            long long x = std::stoll(ap[i].second);
            long long y = std::stoll(bp[i].second);
            if (x != y) return x < y;
        } else if (ap[i].second != bp[i].second) {
            return ap[i].second < bp[i].second;
        }
    }
    return ap.size() < bp.size();
}

bool is_audio_file(const std::filesystem::path& p) {
    static const std::vector<std::string> exts = {
        ".wav", ".aiff", ".aif", ".flac", ".mp3", ".ogg", ".m4a"
    };
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::filesystem::path> list_reference_paths(
    const std::filesystem::path& dir)
{
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::is_directory(dir)) return out;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.is_regular_file() && is_audio_file(e.path())) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end(), natural_less_filename);
    return out;
}

// Duration of an audio file in seconds, via libsndfile. Handles every
// format libsndfile supports (WAV / FLAC / AIFF / etc.), including the
// reference mode's usual WAV variants that AudioFile's strict validator
// rejects.
double audio_duration_seconds(const std::filesystem::path& p) {
    SF_INFO info = {};
    SNDFILE* f = sf_open(p.string().c_str(), SFM_READ, &info);
    if (!f) return 0.0;
    double d = (info.samplerate > 0)
        ? static_cast<double>(info.frames) / info.samplerate
        : 0.0;
    sf_close(f);
    return d;
}

// Deterministic-ish GUID from an index so project files diff cleanly
// across runs while items still have unique IDs.
std::string pseudo_guid(uint64_t seed) {
    static const char hex[] = "0123456789ABCDEF";
    uint64_t s = seed * 0x9E3779B97F4A7C15ull + 0xDEADBEEFCAFEBABEull;
    auto rand_hex = [&](int n) {
        std::string r;
        for (int i = 0; i < n; ++i) {
            s ^= s >> 33; s *= 0xff51afd7ed558ccdull;
            s ^= s >> 33; s *= 0xc4ceb9fe1a85ec53ull;
            r += hex[s & 0xF];
        }
        return r;
    };
    return "{" + rand_hex(8) + "-" + rand_hex(4) + "-" + rand_hex(4) + "-"
         + rand_hex(4) + "-" + rand_hex(12) + "}";
}

// REAPER string escaping for file paths. Strings are wrapped in double
// quotes; if a path contains a double quote, use single quotes instead.
std::string rpp_string(const std::string& s) {
    bool has_dq = s.find('"') != std::string::npos;
    bool has_sq = s.find('\'') != std::string::npos;
    char quote = '"';
    if (has_dq && !has_sq) quote = '\'';
    else if (has_dq && has_sq) quote = '`';  // last resort, usually unused
    return std::string(1, quote) + s + quote;
}

void write_item(
    std::ostream& os,
    const std::string& indent,
    double position_s,
    double length_s,
    double soffs_s,
    const std::filesystem::path& source,
    const std::string& name,
    int item_id)
{
    auto fmt = [](double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(9) << v;
        return ss.str();
    };

    os << indent << "<ITEM\n";
    os << indent << "  POSITION " << fmt(position_s) << "\n";
    os << indent << "  SNAPOFFS 0\n";
    os << indent << "  LENGTH " << fmt(length_s) << "\n";
    os << indent << "  LOOP 0\n";
    os << indent << "  ALLTAKES 0\n";
    os << indent << "  FADEIN 1 0 0 1 0 0 0\n";
    os << indent << "  FADEOUT 1 0 0 1 0 0 0\n";
    os << indent << "  MUTE 0 0\n";
    os << indent << "  SEL 0\n";
    os << indent << "  IGUID " << pseudo_guid(static_cast<uint64_t>(item_id) * 11u) << "\n";
    os << indent << "  IID " << item_id << "\n";
    os << indent << "  NAME " << rpp_string(name) << "\n";
    os << indent << "  VOLPAN 1 0 1 -1\n";
    os << indent << "  SOFFS " << fmt(soffs_s) << "\n";
    os << indent << "  PLAYRATE 1 1 0 -1 0 0.0025\n";
    os << indent << "  CHANMODE 0\n";
    os << indent << "  GUID " << pseudo_guid(static_cast<uint64_t>(item_id) * 17u + 1u) << "\n";
    os << indent << "  <SOURCE WAVE\n";
    os << indent << "    FILE " << rpp_string(source.string()) << "\n";
    os << indent << "  >\n";
    os << indent << ">\n";
}

void write_track_header(
    std::ostream& os,
    const std::string& indent,
    const std::string& name,
    int track_index)
{
    os << indent << "<TRACK " << pseudo_guid(static_cast<uint64_t>(track_index) * 101u) << "\n";
    os << indent << "  NAME " << rpp_string(name) << "\n";
    os << indent << "  PEAKCOL 16576\n";
    os << indent << "  BEAT -1\n";
    os << indent << "  AUTOMODE 0\n";
    os << indent << "  VOLPAN 1 0 -1 -1 1\n";
    os << indent << "  MUTESOLO 0 0 0\n";
    os << indent << "  IPHASE 0\n";
    os << indent << "  PLAYOFFS 0 1\n";
    os << indent << "  ISBUS 0 0\n";
    os << indent << "  BUSCOMP 0 0 0 0 0\n";
    os << indent << "  SHOWINMIX 1 0.6667 0.5 1 0.5 0 0 0\n";
    os << indent << "  FREEMODE 0\n";
    os << indent << "  SEL 0\n";
    os << indent << "  REC 0 0 0 0 0 0 0 0\n";
    os << indent << "  VU 2\n";
    os << indent << "  TRACKHEIGHT 160 0 0 0 0 0 0 0\n";
    os << indent << "  INQ 0 0 0 0.5 100 0 0 100\n";
    os << indent << "  NCHAN 2\n";
    os << indent << "  FX 1\n";
    os << indent << "  TRACKID " << pseudo_guid(static_cast<uint64_t>(track_index) * 103u) << "\n";
    os << indent << "  PERF 0\n";
    os << indent << "  MIDIOUT -1\n";
    os << indent << "  MAINSEND 1 0\n";
}

} // anonymous namespace

bool write_reaper_project(
    const std::filesystem::path& rpp_path,
    const std::filesystem::path& vinyl_path,
    const std::filesystem::path& reference_dir,
    const std::vector<SplitPoint>& chops,
    int native_sample_rate)
{
    auto ref_paths = list_reference_paths(reference_dir);
    if (ref_paths.empty() || chops.empty()) return false;

    // Use absolute paths so the project opens correctly regardless of
    // where the user moves it on disk.
    std::error_code ec;
    auto abs_vinyl = std::filesystem::absolute(vinyl_path, ec);
    if (ec) abs_vinyl = vinyl_path;

    std::vector<std::filesystem::path> abs_refs;
    abs_refs.reserve(ref_paths.size());
    for (const auto& p : ref_paths) {
        std::error_code e;
        auto ap = std::filesystem::absolute(p, e);
        abs_refs.push_back(e ? p : ap);
    }

    // Durations of each reference (read from the file header, at native
    // sample rate). These drive the timeline layout for both tracks.
    std::vector<double> ref_durs;
    ref_durs.reserve(abs_refs.size());
    for (const auto& p : abs_refs) {
        ref_durs.push_back(audio_duration_seconds(p));
    }

    size_t pairs = std::min(abs_refs.size(), chops.size());

    std::ofstream os(rpp_path);
    if (!os) return false;

    os << "<REAPER_PROJECT 0.1 \"7.00/auto-chop\" 0\n";
    os << "  RIPPLE 0\n";
    os << "  GROUPOVERRIDE 0 0 0\n";
    os << "  AUTOXFADE 129\n";
    // SAMPLERATE: rate, project-rate-force, force-from-media.
    // Second field = 1 locks the project to the given rate (otherwise
    // REAPER follows the audio device's rate, which is why renders
    // default to 44.1 kHz unless the device happens to match).
    os << "  SAMPLERATE " << native_sample_rate << " 1 0\n";
    os << "  TEMPO 120 4 4\n";
    os << "  PLAYRATE 1 0 0.25 4\n";
    os << "  ZOOM 4 0 0\n";
    os << "  VZOOMEX 6 0\n";
    os << "  <METRONOME 6 2\n";
    os << "    VOL 0.25 0.125\n";
    os << "  >\n";
    os << "  MASTER_NCH 2 2\n";
    os << "  MASTER_VOLUME 1 0 -1 -1 1\n";
    os << "  MASTERTRACKHEIGHT 0 0\n";
    // Render config: match project rate and write 24-bit PCM WAV.
    os << "  RENDER_FILE \"\"\n";
    os << "  RENDER_PATTERN \"$project-$track\"\n";
    os << "  RENDER_FMT 0 2 0\n";
    os << "  RENDER_1X 0\n";
    os << "  RENDER_RANGE 1 0 0 18 1000\n";
    os << "  RENDER_RESAMPLE 3 0 1\n";
    os << "  RENDER_ADDTOPROJ 0\n";
    os << "  RENDER_STEMS 0\n";
    os << "  RENDER_DITHER 0\n";
    // Force render sample rate to project rate (0 = use project rate).
    os << "  RENDER_SRATE " << native_sample_rate << "\n";

    // Track 1: References
    write_track_header(os, "  ", "References", 1);
    double pos = 0.0;
    for (size_t i = 0; i < pairs; ++i) {
        std::string name = abs_refs[i].stem().string();
        write_item(os, "    ",
                   pos,
                   ref_durs[i],
                   0.0,
                   abs_refs[i],
                   name,
                   static_cast<int>(100 + i));
        pos += ref_durs[i];
    }
    os << "  >\n";

    // Track 2: Vinyl Chops (same timeline layout as the references)
    write_track_header(os, "  ", "Vinyl Chops", 2);
    pos = 0.0;
    for (size_t i = 0; i < pairs; ++i) {
        double chop_start_s  = static_cast<double>(chops[i].start_sample) / native_sample_rate;
        double chop_length_s = static_cast<double>(
            chops[i].end_sample - chops[i].start_sample + 1) / native_sample_rate;
        // Clamp so adjacent chop items never overlap on the timeline; if
        // a chop turned out longer than its ref, cap at ref duration
        // (the user can extend in REAPER — the source data is there).
        if (chop_length_s > ref_durs[i]) chop_length_s = ref_durs[i];
        std::string name = std::string("Chop ") +
            (i < 9 ? "0" : "") + std::to_string(i + 1);
        write_item(os, "    ",
                   pos,
                   chop_length_s,
                   chop_start_s,
                   abs_vinyl,
                   name,
                   static_cast<int>(200 + i));
        pos += ref_durs[i];
    }
    os << "  >\n";

    os << ">\n";
    return true;
}

} // namespace mwaac
