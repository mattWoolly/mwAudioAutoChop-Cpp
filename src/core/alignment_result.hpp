#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace mwaac {

struct DriftModel {
    std::vector<double> coefficients;  // Polynomial coefficients
    std::vector<std::pair<int64_t, int64_t>> segment_offsets;
    
    [[nodiscard]] int64_t ref_to_vinyl_sample(
        int64_t ref_sample, int native_sr, int analysis_sr) const;
};

struct TrackOffset {
    int64_t vinyl_start_sample{0};
    double correlation_confidence{0.0};
};

struct AlignmentResult {
    int64_t global_offset{0};
    double correlation_score{0.0};
    std::vector<int64_t> track_boundaries;  // Reference track starts
    std::optional<DriftModel> drift_model;
    int native_sr{0};
    int analysis_sr{22050};
    std::vector<std::string> track_names;
    int64_t music_start_sample{0};
    std::vector<TrackOffset> track_offsets;
};

} // namespace mwaac