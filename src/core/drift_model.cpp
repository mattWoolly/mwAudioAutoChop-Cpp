#include "alignment_result.hpp"
#include "modes/reference_mode.hpp"
#include <cmath>

namespace mwaac {

int64_t DriftModel::ref_to_vinyl_sample(
    int64_t ref_sample, int native_sr, int analysis_sr) const {
    // DRIFT-MODEL-RATE-TRUNCATION (Tier 5): the previous implementation of
    // each return below performed `ref_sample * native_sr / analysis_sr` as
    // integer division (lines 10, 16) or float→int truncation (line 33),
    // either of which can place a vinyl-sample index up to ~9 native-rate
    // samples below the mathematically correct value at 192 kHz native /
    // 44.1 kHz analysis. The cure routes the two integer-arithmetic sites
    // through the C-4 helper (mwaac::analysis_to_native_sample) and uses
    // std::llround at the float→int site so the analysis-rate ↔ native-rate
    // invariant established by C-4 in reference_mode.cpp also holds here.

    // If no drift model (empty segment_offsets), just scale
    if (segment_offsets.empty()) {
        return analysis_to_native_sample(ref_sample, native_sr, analysis_sr);
    }

    // Get max_pos from last segment_offset
    int64_t max_pos = segment_offsets.back().first;
    if (max_pos == 0) {
        return analysis_to_native_sample(ref_sample, native_sr, analysis_sr);
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

    // Apply offset and scale to native sample rate.
    //
    // Cure choice (DRIFT-MODEL-RATE-TRUNCATION line 33): use std::llround on
    // the double product rather than pre-rounding `vinyl_sample` to int64_t
    // and routing through the integer helper. Rationale: (a) the polynomial
    // evaluation above already produces a double, so a single std::llround
    // on the final product is the natural one-step round-to-nearest with
    // tie-away-from-zero, giving a worst-case error of 0.5 native-rate
    // samples; (b) the alternative (pre-round to int64_t, then call
    // analysis_to_native_sample) is two rounding steps, which composes
    // looser error bounds without buying any property C-4's invariant
    // requires. std::llround returns long long; the static_cast<int64_t> is
    // defensive across platforms where long long != int64_t.
    double vinyl_sample = static_cast<double>(ref_sample) + offset;
    return static_cast<int64_t>(
        std::llround(vinyl_sample * static_cast<double>(native_sr) /
                     static_cast<double>(analysis_sr)));
}

} // namespace mwaac
