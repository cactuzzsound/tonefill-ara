#include "dsp/NoiseShaper.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
NoiseShaper::NoiseShaper (StftConfig cfg)
    : cfg_ (cfg),
      fft_ (cfg.fftSize),
      window_ (makeWindow (cfg.window, cfg.fftSize, cfg.tukeyAlpha)),
      frame_ ((std::size_t) cfg.fftSize, 0.0f),
      re_ ((std::size_t) (cfg.fftSize / 2 + 1), 0.0f),
      im_ ((std::size_t) (cfg.fftSize / 2 + 1), 0.0f),
      tmp_ ((std::size_t) cfg.fftSize, 0.0f)
{
}

void NoiseShaper::generate (float* out, int n, const std::vector<float>& shape, SeededRng& rng)
{
    const int N = cfg_.fftSize, H = cfg_.hop, bins = fft_.numBins();
    if (n <= 0) return;

    // White-noise source padded by one frame on BOTH sides, so every output sample in [0,n)
    // is covered by a full set of overlapping windows (COLA). `pad` maps logical index j to
    // physical src_[j + pad]. Without the leading pad, the first/last samples sit under the
    // window's zero-valued tails -> winSum -> 0 -> divide-by-near-zero edge spikes.
    const int pad = N;
    src_.assign ((std::size_t) (n + 2 * N), 0.0f);
    for (auto& s : src_) s = rng.nextFloat() * 2.0f - 1.0f;

    std::fill (out, out + n, 0.0f);
    winSum_.assign ((std::size_t) n, 0.0f);

    // Mean-normalize the shape so overall gain stays ~unity (caller rescales to RMS).
    double mean = 0.0;
    for (float v : shape) mean += v;
    const float meanInv = (mean > 0.0) ? (float) ((double) shape.size() / mean) : 1.0f;

    for (int start = -N; start < n; start += H)
    {
        for (int k = 0; k < N; ++k)
        {
            const int phys = start + k + pad;
            const float src = (phys >= 0 && phys < (int) src_.size()) ? src_[(std::size_t) phys] : 0.0f;
            frame_[(std::size_t) k] = src * window_[(std::size_t) k];
        }

        fft_.forward (frame_.data(), re_.data(), im_.data());

        for (int b = 0; b < bins; ++b)
        {
            const float reB = re_[(std::size_t) b], imB = im_[(std::size_t) b];
            const float mag = std::sqrt (reB * reB + imB * imB);
            const float ph  = std::atan2 (imB, reB);
            const float sh  = (b < (int) shape.size() ? shape[(std::size_t) b] : 0.0f) * meanInv;
            const float nm  = mag * sh;
            re_[(std::size_t) b] = nm * std::cos (ph);
            im_[(std::size_t) b] = nm * std::sin (ph);
        }

        fft_.inverse (re_.data(), im_.data(), tmp_.data());

        for (int k = 0; k < N; ++k)
        {
            const int idx = start + k;
            if (idx >= 0 && idx < n)
            {
                const float w = window_[(std::size_t) k];
                out[idx]                    += tmp_[(std::size_t) k] * w;
                winSum_[(std::size_t) idx]  += w * w;
            }
        }
    }

    for (int i = 0; i < n; ++i)
        if (winSum_[(std::size_t) i] > 1e-8f)
            out[i] /= winSum_[(std::size_t) i];
}
} // namespace tonefill::dsp
