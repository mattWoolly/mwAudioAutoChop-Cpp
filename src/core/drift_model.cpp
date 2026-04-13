#include "alignment_result.hpp"
#include <cmath>

namespace mwaac {

int64_t DriftModel::ref_to_vinyl_sample(
    int64_t ref_sample, int native_sr, int analysis_sr) const {
    // If no coefficients, assume linear 1:1 mapping
    if (coefficients.empty()) {
        return ref_sample;
    }
    
    // Evaluate polynomial: y = c[0] + c[1]*x + c[2]*x^2 + ...
    // We're mapping reference sample to vinyl sample position
    double x = static_cast<double>(ref_sample);
    double y = 0.0;
    double x_power = 1.0;
    
    for (size_t i = 0; i < coefficients.size(); ++i) {
        y += coefficients[i] * x_power;
        x_power *= x;
    }
    
    // Apply sample rate conversion: reference_sr / native_sr
    // The coefficients are in analysis_sr space, so we need to convert
    double sr_ratio = static_cast<double>(analysis_sr) / static_cast<double>(native_sr);
    return static_cast<int64_t>(y * sr_ratio);
}

} // namespace mwaac