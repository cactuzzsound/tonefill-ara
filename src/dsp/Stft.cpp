#include "dsp/Stft.h"

#include <cmath>

namespace tonefill::dsp
{
Stft::Stft (StftConfig cfg)
    : cfg_ (cfg),
      fft_ (cfg.fftSize),
      window_ (makeWindow (cfg.window, cfg.fftSize, cfg.tukeyAlpha)),
      rawFrame_ ((std::size_t) cfg.fftSize, 0.0f),
      winFrame_ ((std::size_t) cfg.fftSize, 0.0f),
      re_ ((std::size_t) (cfg.fftSize / 2 + 1), 0.0f),
      im_ ((std::size_t) (cfg.fftSize / 2 + 1), 0.0f)
{
}

void Stft::analyze (const float* src, int numSamples, int frameStart,
                    std::vector<float>& magOut, std::vector<float>& phaseOut)
{
    const int N = cfg_.fftSize;
    const int bins = fft_.numBins();
    if ((int) magOut.size()   != bins) magOut.assign   ((std::size_t) bins, 0.0f);
    if ((int) phaseOut.size() != bins) phaseOut.assign ((std::size_t) bins, 0.0f);

    // Gather frame with zero-padding outside [0, numSamples).
    for (int n = 0; n < N; ++n)
    {
        const int idx = frameStart + n;
        const float s = (idx >= 0 && idx < numSamples) ? src[idx] : 0.0f;
        rawFrame_[(std::size_t) n] = s;
        winFrame_[(std::size_t) n] = s * window_[(std::size_t) n];
    }

    fft_.forward (winFrame_.data(), re_.data(), im_.data());

    for (int k = 0; k < bins; ++k)
    {
        const float re = re_[(std::size_t) k];
        const float im = im_[(std::size_t) k];
        magOut[(std::size_t) k]   = std::sqrt (re * re + im * im);
        phaseOut[(std::size_t) k] = std::atan2 (im, re);
    }
}
} // namespace tonefill::dsp
