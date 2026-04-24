#pragma once

#include "core/split_point.hpp"
#include <filesystem>
#include <vector>

namespace mwaac {

// Write a REAPER project (.rpp) file that lets the user A/B-compare the
// chopped vinyl against the reference tracks and drag item edges to
// fine-tune any chop that isn't quite right.
//
// The project contains two tracks, both sharing the same timeline:
//   - "References"  — each reference file as a sequential item.
//   - "Vinyl Chops" — slices of the ORIGINAL vinyl file (SOFFS + LENGTH).
//     No intermediate audio is written; the items reference the vinyl
//     directly, so the user can extend an item's left/right edge in
//     REAPER and the underlying audio is still available.
//
// Both tracks place their Nth item at the cumulative reference duration,
// so the user can click up/down between the two tracks at any point in
// the timeline to compare the same musical moment.
//
// Returns true on success.
bool write_reaper_project(
    const std::filesystem::path& rpp_path,
    const std::filesystem::path& vinyl_path,
    const std::filesystem::path& reference_dir,
    const std::vector<SplitPoint>& chops,
    int native_sample_rate);

} // namespace mwaac
