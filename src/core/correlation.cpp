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

std::vector<float> downsample(std::span<const float> samples, int factor) {
    if (factor <= 1 || samples.empty()) {
        return std::vector<float>(samples.begin(), samples.end());
    }
    
    size_t output_size = samples.size() / factor;
    std::vector<float> result(output_size);
    
    for (size_t i = 0; i < output_size; ++i) {
        float sum = 0.0f;
        size_t start = i * factor;
        for (int j = 0; j < factor && start + j < samples.size(); ++j) {
            sum += samples[start + j];
        }
        result[i] = sum / factor;
    }
    
    return result;
}

CorrelationResult cross_correlate_fast(
    std::span<const float> reference,
    std::span<const float> target,
    int downsample_factor)
{
    if (reference.empty() || target.empty()) {
        return {0, 0.0};
    }
    
    // Stage 1: Coarse search with downsampled audio
    auto ref_ds = downsample(reference, downsample_factor);
    auto tgt_ds = downsample(target, downsample_factor);
    
    auto coarse_result = cross_correlate(ref_ds, tgt_ds);
    
    // Convert coarse lag back to full resolution
    int64_t coarse_lag = coarse_result.lag * downsample_factor;
    
    // Stage 2: Refine around coarse position with full resolution
    // Use a narrow window around the coarse result
    int64_t refine_radius = downsample_factor * 2;  // Search +/- 2 coarse samples
    int64_t refine_start = coarse_lag - refine_radius;
    int64_t refine_end = coarse_lag + refine_radius;
    
    // Only search valid lags (where reference overlaps target)
    int64_t min_valid_lag = 0;
    int64_t max_valid_lag = static_cast<int64_t>(target.size()) - 1;
    
    refine_start = std::max(refine_start, min_valid_lag);
    refine_end = std::min(refine_end, max_valid_lag);
    
    // Normalize reference once
    float ref_mean = std::accumulate(reference.begin(), reference.end(), 0.0f) / reference.size();
    std::vector<float> ref_norm(reference.size());
    std::transform(reference.begin(), reference.end(), ref_norm.begin(),
                  [ref_mean](float s) { return s - ref_mean; });
    
    float ref_energy = 0.0f;
    for (auto v : ref_norm) ref_energy += v * v;
    
    double best_corr = -std::numeric_limits<double>::infinity();
    int64_t best_lag = coarse_lag;
    
    // Fine search
    for (int64_t lag = refine_start; lag <= refine_end; ++lag) {
        double sum = 0.0;
        float tgt_energy = 0.0f;
        
        int64_t tgt_start = lag;
        int64_t overlap_len = std::min(
            static_cast<int64_t>(ref_norm.size()),
            static_cast<int64_t>(target.size()) - tgt_start
        );
        
        if (overlap_len <= 0) continue;
        
        // Compute mean for this target segment
        float tgt_mean = 0.0f;
        for (int64_t i = 0; i < overlap_len; ++i) {
            tgt_mean += target[tgt_start + i];
        }
        tgt_mean /= overlap_len;
        
        for (int64_t i = 0; i < overlap_len; ++i) {
            float t = target[tgt_start + i] - tgt_mean;
            sum += ref_norm[i] * t;
            tgt_energy += t * t;
        }
        
        float norm = std::sqrt(ref_energy * tgt_energy);
        double corr = (norm > 1e-10f) ? (sum / norm) : 0.0;
        
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }
    
    return {best_lag, best_corr};
}

} // namespace mwaac