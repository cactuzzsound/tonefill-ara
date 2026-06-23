#include <catch2/catch_test_macros.hpp>

#include "dsp/NoiseShaper.h"
#include "dsp/Features.h"

#include <cmath>
#include <vector>

using namespace tonefill::dsp;

namespace
{
float bufferLowBandRatio (const std::vector<float>& x, double sr, double cutoff = 200.0)
{
    // One mid-buffer 2048 frame is enough to gauge spectral tilt.
    const int N = 2048;
    FFTWrapper fft (N);
    const auto win = makeWindow (WindowType::Hann, N);
    std::vector<float> fr (N), re (fft.numBins()), im (fft.numBins()), mag (fft.numBins());
    const int start = (int) x.size() / 2 - N / 2;
    for (int k = 0; k < N; ++k) fr[k] = x[(std::size_t) (start + k)] * win[(std::size_t) k];
    fft.forward (fr.data(), re.data(), im.data());
    for (int b = 0; b < fft.numBins(); ++b)
        mag[(std::size_t) b] = std::sqrt (re[b] * re[b] + im[b] * im[b]);
    return lowBandRatio (mag.data(), fft.numBins(), sr / N, cutoff);
}
} // namespace

TEST_CASE ("NoiseShaper output is non-silent and deterministic")
{
    StftConfig cfg;
    NoiseShaper s1 (cfg), s2 (cfg);
    const int bins = s1.numBins();
    std::vector<float> flat ((std::size_t) bins, 1.0f);

    const int n = 24000;
    std::vector<float> a ((std::size_t) n), b ((std::size_t) n);
    SeededRng r1 (123), r2 (123);
    s1.generate (a.data(), n, flat, r1);
    s2.generate (b.data(), n, flat, r2);

    double e = 0.0; for (float v : a) e += (double) v * v;
    CHECK (e > 0.0);                 // non-silent
    CHECK (a == b);                  // deterministic for equal seeds
}

TEST_CASE ("NoiseShaper imposes the target spectral shape")
{
    StftConfig cfg;
    NoiseShaper shaper (cfg);
    const int bins = shaper.numBins();
    const double sr = 48000.0, binHz = sr / cfg.fftSize;

    // Low-pass-ish shape: energy only below ~200 Hz.
    std::vector<float> lowShape ((std::size_t) bins, 0.0f);
    for (int b = 0; b < bins; ++b)
        if ((double) b * binHz < 200.0) lowShape[(std::size_t) b] = 1.0f;

    const int n = 48000;
    std::vector<float> out ((std::size_t) n);
    SeededRng rng (5);
    shaper.generate (out.data(), n, lowShape, rng);

    CHECK (bufferLowBandRatio (out, sr) > 0.7f); // energy concentrated below 200 Hz
}
