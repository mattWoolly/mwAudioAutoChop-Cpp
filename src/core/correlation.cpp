#include "correlation.hpp"

// pocketfft is a vendored third-party header (BSD-3, Martin Reinecke). It
// emits an int->long-double conversion warning under our -Wimplicit-int-
// float-conversion flag. Suppress at the include site rather than editing
// the vendored file (Mi-18 scope rule).
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-conversion"
#endif
#include "pocketfft_hdronly.h"
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <complex>
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
        float mean = std::accumulate(sig.begin(), sig.end(), 0.0f) / static_cast<float>(sig.size());
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
            sum += static_cast<double>(ref_norm[static_cast<std::size_t>(ref_start + i)])
                 * static_cast<double>(tgt_norm[static_cast<std::size_t>(tgt_start + i)]);
        }
        
        if (sum > best_corr) {
            best_corr = sum;
            best_lag = lag;
        }
    }
    
    return {best_lag, best_corr / static_cast<double>(norm_factor)};
}

void apply_highpass(std::vector<float>& samples, int sample_rate, float cutoff_hz) {
    // Simple first-order IIR high-pass filter
    // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
    // alpha = RC / (RC + dt), RC = 1/(2*pi*fc)
    
    float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff_hz);
    float dt = 1.0f / static_cast<float>(sample_rate);
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
    float rms = std::sqrt(sum_sq / static_cast<float>(samples.size()));
    
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
    
    size_t output_size = samples.size() / static_cast<std::size_t>(factor);
    std::vector<float> result(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        float sum = 0.0f;
        size_t start = i * static_cast<std::size_t>(factor);
        for (int j = 0; j < factor && start + static_cast<std::size_t>(j) < samples.size(); ++j) {
            sum += samples[start + static_cast<std::size_t>(j)];
        }
        result[i] = sum / static_cast<float>(factor);
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

    double ref_ds_mean = std::accumulate(ref_ds.begin(), ref_ds.end(), 0.0) / static_cast<double>(ref_ds.size());
    std::vector<float> ref_ds_norm(ref_ds.size());
    std::transform(ref_ds.begin(), ref_ds.end(), ref_ds_norm.begin(),
                   [ref_ds_mean](float s) { return s - static_cast<float>(ref_ds_mean); });

    double ref_ds_energy = 0.0;
    for (float v : ref_ds_norm) ref_ds_energy += static_cast<double>(v) * static_cast<double>(v);

    int64_t ds_max_lag = static_cast<int64_t>(tgt_ds.size()) - static_cast<int64_t>(ref_ds_norm.size());

    double best_coarse_corr = 0.0;
    int64_t best_coarse_lag = 0;

    for (int64_t lag = 0; lag <= ds_max_lag; ++lag) {
        double tgt_mean = 0.0;
        for (size_t i = 0; i < ref_ds_norm.size(); ++i) {
            tgt_mean += static_cast<double>(tgt_ds[static_cast<std::size_t>(lag) + i]);
        }
        tgt_mean /= static_cast<double>(ref_ds_norm.size());

        double sum = 0.0;
        double tgt_energy = 0.0;
        for (size_t i = 0; i < ref_ds_norm.size(); ++i) {
            double t = static_cast<double>(tgt_ds[static_cast<std::size_t>(lag) + i]) - tgt_mean;
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

    int64_t coarse_lag = best_coarse_lag * static_cast<int64_t>(downsample_factor);

    // Stage 2: Refine around coarse position at full resolution.
    int64_t max_valid_lag = static_cast<int64_t>(target.size()) - static_cast<int64_t>(reference.size());
    int64_t refine_radius = static_cast<int64_t>(downsample_factor) * 2;
    int64_t refine_start = std::max(int64_t{0}, coarse_lag - refine_radius);
    int64_t refine_end = std::min(max_valid_lag, coarse_lag + refine_radius);

    double ref_mean = std::accumulate(reference.begin(), reference.end(), 0.0) / static_cast<double>(reference.size());
    std::vector<float> ref_norm(reference.size());
    std::transform(reference.begin(), reference.end(), ref_norm.begin(),
                   [ref_mean](float s) { return s - static_cast<float>(ref_mean); });

    double ref_energy = 0.0;
    for (float v : ref_norm) ref_energy += static_cast<double>(v) * static_cast<double>(v);

    // Fall back to coarse result if refine can't improve on it.
    double best_corr = best_coarse_corr;
    int64_t best_lag = std::clamp(coarse_lag, int64_t{0}, std::max(int64_t{0}, max_valid_lag));

    for (int64_t lag = refine_start; lag <= refine_end; ++lag) {
        double tgt_mean = 0.0;
        for (size_t i = 0; i < ref_norm.size(); ++i) {
            tgt_mean += static_cast<double>(target[static_cast<std::size_t>(lag) + i]);
        }
        tgt_mean /= static_cast<double>(ref_norm.size());

        double sum = 0.0;
        double tgt_energy = 0.0;
        for (size_t i = 0; i < ref_norm.size(); ++i) {
            double t = static_cast<double>(target[static_cast<std::size_t>(lag) + i]) - tgt_mean;
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

CorrelationResult cross_correlate_fft(
    std::span<const float> reference,
    std::span<const float> target)
{
    const size_t N = reference.size();
    const size_t M = target.size();
    if (N == 0 || M == 0 || N > M) return {0, 0.0};

    // Zero-mean the reference. After this, raw_corr[lag] = sum_i ref_c[i] *
    // target[lag+i] is already the centered cross product on the ref side.
    // (The constant tgt_slice_mean * sum(ref_c) = 0 drops out.) We still
    // need to center the target per-lag for the normalization denominator.
    double ref_mean = 0.0;
    for (float v : reference) ref_mean += static_cast<double>(v);
    ref_mean /= static_cast<double>(N);

    std::vector<double> ref_c(N);
    double ref_energy = 0.0;
    for (size_t i = 0; i < N; ++i) {
        double v = static_cast<double>(reference[i]) - ref_mean;
        ref_c[i] = v;
        ref_energy += v * v;
    }
    if (ref_energy < 1e-15) return {0, 0.0};

    std::vector<double> tgt_d(M);
    for (size_t i = 0; i < M; ++i) tgt_d[i] = static_cast<double>(target[i]);

    // FFT length: at least N+M-1 so linear (non-circular) correlation fits.
    // pocketfft handles arbitrary sizes efficiently, but prefers smooth
    // numbers. good_size rounds up to the next efficient FFT length.
    const size_t L_min = N + M - 1;
    const size_t L = pocketfft::detail::util::good_size_real(L_min);

    // Zero-pad both signals to length L.
    std::vector<double> ref_padded(L, 0.0);
    std::vector<double> tgt_padded(L, 0.0);
    std::copy(ref_c.begin(), ref_c.end(), ref_padded.begin());
    std::copy(tgt_d.begin(), tgt_d.end(), tgt_padded.begin());

    // Real-to-complex forward FFT. Output length is L/2 + 1.
    const size_t K = L / 2 + 1;
    std::vector<std::complex<double>> Ref_f(K), Tgt_f(K);
    pocketfft::shape_t shape = {L};
    pocketfft::stride_t stride_real = {sizeof(double)};
    pocketfft::stride_t stride_cplx = {sizeof(std::complex<double>)};
    pocketfft::shape_t axes = {0};

    pocketfft::r2c(shape, stride_real, stride_cplx, axes,
                   pocketfft::FORWARD,
                   ref_padded.data(), Ref_f.data(), 1.0);
    pocketfft::r2c(shape, stride_real, stride_cplx, axes,
                   pocketfft::FORWARD,
                   tgt_padded.data(), Tgt_f.data(), 1.0);

    // Cross-correlation in frequency domain: R_xy = IFFT(conj(X) * Y)
    // This yields R[k] = sum_i x[i] * y[i + k].
    std::vector<std::complex<double>> Prod(K);
    for (size_t i = 0; i < K; ++i) Prod[i] = std::conj(Ref_f[i]) * Tgt_f[i];

    // Inverse FFT back to time domain.
    std::vector<double> corr(L);
    pocketfft::c2r(shape, stride_cplx, stride_real, axes,
                   pocketfft::BACKWARD,
                   Prod.data(), corr.data(), 1.0 / static_cast<double>(L));

    // Precompute target prefix sums for per-lag slice mean/energy.
    std::vector<double> psum(M + 1, 0.0), psum_sq(M + 1, 0.0);
    for (size_t i = 0; i < M; ++i) {
        double v = tgt_d[i];
        psum[i + 1]    = psum[i]    + v;
        psum_sq[i + 1] = psum_sq[i] + v * v;
    }

    // Walk valid lags [0, M - N] and find the normalized peak.
    const size_t max_lag = M - N;
    double best_corr = 0.0;
    int64_t best_lag = 0;

    for (size_t lag = 0; lag <= max_lag; ++lag) {
        double tgt_sum    = psum[lag + N]    - psum[lag];
        double tgt_sum_sq = psum_sq[lag + N] - psum_sq[lag];
        // Centered tgt energy = sum (tgt_i - mean)^2 = sum_sq - N*mean^2.
        double tgt_energy = tgt_sum_sq - (tgt_sum * tgt_sum) / static_cast<double>(N);
        if (tgt_energy < 1e-15) continue;

        // raw_corr is the centered ref X uncentered target cross product.
        // Since ref_c is zero-mean, the tgt_mean term drops out:
        //   sum ref_c[i] * (tgt[lag+i] - tgt_mean)
        //     = sum ref_c[i] * tgt[lag+i] - tgt_mean * sum ref_c[i]
        //     = corr[lag] - 0.
        double centered = corr[lag];
        double denom = std::sqrt(ref_energy * tgt_energy);
        double ncc = centered / denom;
        if (ncc > best_corr) {
            best_corr = ncc;
            best_lag = static_cast<int64_t>(lag);
        }
    }

    return {best_lag, best_corr};
}

} // namespace mwaac