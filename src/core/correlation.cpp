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
    if (reference.empty() || target.empty() || reference.size() > target.size()) {
        return {0, 0.0};
    }

    // Stage 1: Coarse search on downsampled signals.
    // Restricted to valid lags [0, tgt_ds.size - ref_ds.size] — the reference
    // must fit fully inside the target slice. Callers (reference mode) already
    // position the target slice around the expected position, so searching
    // negative lags (reference hanging off the front) just invites spurious
    // matches against low-frequency content.
    auto ref_ds = downsample(reference, downsample_factor);
    auto tgt_ds = downsample(target, downsample_factor);

    if (ref_ds.empty() || tgt_ds.empty() || ref_ds.size() > tgt_ds.size()) {
        return {0, 0.0};
    }

    double ref_ds_mean = std::accumulate(ref_ds.begin(), ref_ds.end(), 0.0) / ref_ds.size();
    std::vector<float> ref_ds_norm(ref_ds.size());
    std::transform(ref_ds.begin(), ref_ds.end(), ref_ds_norm.begin(),
                   [ref_ds_mean](float s) { return s - static_cast<float>(ref_ds_mean); });

    double ref_ds_energy = 0.0;
    for (float v : ref_ds_norm) ref_ds_energy += static_cast<double>(v) * v;

    int64_t ds_max_lag = static_cast<int64_t>(tgt_ds.size()) - static_cast<int64_t>(ref_ds_norm.size());

    double best_coarse_corr = 0.0;
    int64_t best_coarse_lag = 0;

    for (int64_t lag = 0; lag <= ds_max_lag; ++lag) {
        double tgt_mean = 0.0;
        for (size_t i = 0; i < ref_ds_norm.size(); ++i) {
            tgt_mean += tgt_ds[lag + i];
        }
        tgt_mean /= ref_ds_norm.size();

        double sum = 0.0;
        double tgt_energy = 0.0;
        for (size_t i = 0; i < ref_ds_norm.size(); ++i) {
            double t = static_cast<double>(tgt_ds[lag + i]) - tgt_mean;
            sum += static_cast<double>(ref_ds_norm[i]) * t;
            tgt_energy += t * t;
        }

        double norm = std::sqrt(ref_ds_energy * tgt_energy);
        double corr = (norm > 1e-10) ? (sum / norm) : 0.0;

        if (corr > best_coarse_corr) {
            best_coarse_corr = corr;
            best_coarse_lag = lag;
        }
    }

    int64_t coarse_lag = best_coarse_lag * downsample_factor;

    // Stage 2: Refine around coarse position at full resolution.
    int64_t max_valid_lag = static_cast<int64_t>(target.size()) - static_cast<int64_t>(reference.size());
    int64_t refine_radius = downsample_factor * 2;
    int64_t refine_start = std::max(int64_t{0}, coarse_lag - refine_radius);
    int64_t refine_end = std::min(max_valid_lag, coarse_lag + refine_radius);

    double ref_mean = std::accumulate(reference.begin(), reference.end(), 0.0) / reference.size();
    std::vector<float> ref_norm(reference.size());
    std::transform(reference.begin(), reference.end(), ref_norm.begin(),
                   [ref_mean](float s) { return s - static_cast<float>(ref_mean); });

    double ref_energy = 0.0;
    for (float v : ref_norm) ref_energy += static_cast<double>(v) * v;

    // Fall back to coarse result if refine can't improve on it.
    double best_corr = best_coarse_corr;
    int64_t best_lag = std::clamp(coarse_lag, int64_t{0}, std::max(int64_t{0}, max_valid_lag));

    for (int64_t lag = refine_start; lag <= refine_end; ++lag) {
        double tgt_mean = 0.0;
        for (size_t i = 0; i < ref_norm.size(); ++i) {
            tgt_mean += target[lag + i];
        }
        tgt_mean /= ref_norm.size();

        double sum = 0.0;
        double tgt_energy = 0.0;
        for (size_t i = 0; i < ref_norm.size(); ++i) {
            double t = static_cast<double>(target[lag + i]) - tgt_mean;
            sum += static_cast<double>(ref_norm[i]) * t;
            tgt_energy += t * t;
        }

        double norm = std::sqrt(ref_energy * tgt_energy);
        double corr = (norm > 1e-10) ? (sum / norm) : 0.0;

        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }

    return {best_lag, best_corr};
}

} // namespace mwaac