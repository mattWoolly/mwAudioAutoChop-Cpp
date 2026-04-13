#pragma once
#include "split_point.hpp"
#include "audio_info.hpp"
#include <vector>
#include <optional>
#include <map>

namespace mwaac {

struct AlignmentOffset {
    int64_t offset_samples{0};
    double confidence{0.0};
};

struct AnalysisResult {
    std::vector<SplitPoint> split_points;
    AudioInfo audio_info;
    std::string mode;  // "reference" or "blind"
    std::vector<AlignmentOffset> alignment_offsets;
    bool drift_correction_applied{false};
    
    // Additional metadata
    std::map<std::string, EvidenceValue> metadata;
};

} // namespace mwaac