#include "alignment_result.hpp"
#include <cmath>

namespace mwaac {

int64_t DriftModel::ref_to_vinyl_sample(
    int64_t ref_sample, int native_sr, int analysis_sr) const {
    // If no drift model (empty segment_offsets), just scale
    if (segment_offsets.empty()) {
        return static_cast<int64_t>(ref_sample * native_sr / analysis_sr);
    }
    
    // Get max_pos from last segment_offset
    int64_t max_pos = segment_offsets.back().first;
    if (max_pos == 0) {
        return static_cast<int64_t>(ref_sample * native_sr / analysis_sr);
    }
    
    // Normalize position to 0-1 range for polynomial evaluation
    double t = static_cast<double>(ref_sample) / static_cast<double>(max_pos);
    
    // Evaluate polynomial: offset = c0 + c1*t + c2*t^2 + ...
    double offset = 0.0;
    double t_power = 1.0;
    
    for (size_t i = 0; i < coefficients.size(); ++i) {
        offset += coefficients[i] * t_power;
        t_power *= t;
    }
    
    // Apply offset and scale to native sample rate
    double vinyl_sample = static_cast<double>(ref_sample) + offset;
    return static_cast<int64_t>(vinyl_sample * native_sr / analysis_sr);
}

} // namespace mwaac