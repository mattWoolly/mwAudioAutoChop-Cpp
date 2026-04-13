#include "correlation.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace mwaac {

CorrelationResult cross_correlate(
    std::span<const float> reference,
    std::span<const float> target)
{
    if (reference.empty() || target.empty()) {
        return {0, 0.0};
    }
    
    // Normalize signals (zero mean)
    auto normalize = [](std::span<const float> sig) {
        float mean = std::accumulate(sig.begin(), sig.end(), 0.0f) / sig.size();
        std::vector<float> normalized(sig.size());
        std::transform(sig.begin(), sig.end(), normalized.begin(),
                      [mean](float s) { return s - mean; });
        return normalized;
    };
    
    auto ref_norm = normalize(reference);
    auto tgt_norm = normalize(target);
    
    // Compute normalization factors
    float ref_energy = 0.0f, tgt_energy = 0.0f;
    for (auto v : ref_norm) ref_energy += v * v;
    for (auto v : tgt_norm) tgt_energy += v * v;
    
    float norm_factor = std::sqrt(ref_energy * tgt_energy);
    if (norm_factor < 1e-10f) {
        return {0, 0.0};
    }
    
    // Cross-correlation (naive O(n*m) - TODO: FFT optimization)
    // We compute correlation for lags from -(ref.size-1) to (target.size-1)
    int64_t min_lag = -static_cast<int64_t>(ref_norm.size() - 1);
    int64_t max_lag = static_cast<int64_t>(tgt_norm.size() - 1);
    
    double best_corr = -std::numeric_limits<double>::infinity();
    int64_t best_lag = 0;
    
    for (int64_t lag = min_lag; lag <= max_lag; ++lag) {
        double sum = 0.0;
        
        // Compute overlap
        int64_t ref_start = std::max(int64_t{0}, -lag);
        int64_t tgt_start = std::max(int64_t{0}, lag);
        int64_t overlap_len = std::min(
            static_cast<int64_t>(ref_norm.size()) - ref_start,
            static_cast<int64_t>(tgt_norm.size()) - tgt_start
        );
        
        for (int64_t i = 0; i < overlap_len; ++i) {
            sum += ref_norm[ref_start + i] * tgt_norm[tgt_start + i];
        }
        
        if (sum > best_corr) {
            best_corr = sum;
            best_lag = lag;
        }
    }
    
    return {best_lag, best_corr / norm_factor};
}

void apply_highpass(std::vector<float>& samples, int sample_rate, float cutoff_hz) {
    // Simple first-order IIR high-pass filter
    // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    // alpha = RC / (RC + dt), RC = 1/(2*pi*fc)
    
    float rc = 1.0f / (2.0f * M_PI * cutoff_hz);
    float dt = 1.0f / sample_rate;
    float alpha = rc / (rc + dt);
    
    float prev_x = 0.0f;
    float prev_y = 0.0f;
    
    for (auto& sample : samples) {
        float x = sample;
        float y = alpha * (prev_y + x - prev_x);
        prev_x = x;
        prev_y = y;
        sample = y;
    }
}

void normalize_rms(std::vector<float>& samples) {
    if (samples.empty()) return;
    
    float sum_sq = 0.0f;
    for (float s : samples) sum_sq += s * s;
    float rms = std::sqrt(sum_sq / samples.size());
    
    if (rms > 1e-10f) {
        for (float& s : samples) s /= rms;
    }
}

std::vector<float> preprocess_for_correlation(
    std::span<const float> samples,
    int sample_rate)
{
    std::vector<float> processed(samples.begin(), samples.end());
    apply_highpass(processed, sample_rate, 80.0f);
    normalize_rms(processed);
    return processed;
}

} // namespace mwaac