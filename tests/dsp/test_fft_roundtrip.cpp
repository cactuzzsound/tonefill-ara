#include <catch2/catch_test_macros.hpp>

#include "dsp/FFTWrapper.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace tonefill::dsp;

// forward -> inverse must reconstruct the input (validates the JUCE FFT wiring AND the inverse
// normalization assumption in FFTWrapper). If this fails by a constant factor, flip
// kInverseNormalized in FFTWrapper.h.
TEST_CASE ("Real FFT round-trip reconstructs the signal")
{
    const int N = 1024;
    FFTWrapper fft (N);
    REQUIRE (fft.numBins() == N / 2 + 1);

    std::vector<float> in ((std::size_t) N), out ((std::size_t) N);
    std::vector<float> re ((std::size_t) fft.numBins()), im ((std::size_t) fft.numBins());

    const double w = 2.0 * 3.141592653589793 * 5.0 / N; // 5 cycles across the frame
    for (int n = 0; n < N; ++n)
        in[(std::size_t) n] = 0.3f * (float) std::sin (w * n)
                            + 0.1f * (float) std::sin (3.0 * w * n);

    fft.forward (in.data(), re.data(), im.data());
    fft.inverse (re.data(), im.data(), out.data());

    double maxErr = 0.0;
    for (int n = 0; n < N; ++n)
        maxErr = std::max (maxErr, std::fabs ((double) out[(std::size_t) n] - in[(std::size_t) n]));
    CHECK (maxErr < 1e-4);
}

TEST_CASE ("A pure bin shows energy at the expected frequency index")
{
    const int N = 1024;
    FFTWrapper fft (N);
    std::vector<float> in ((std::size_t) N);
    std::vector<float> re ((std::size_t) fft.numBins()), im ((std::size_t) fft.numBins());

    const int bin = 8;
    const double w = 2.0 * 3.141592653589793 * bin / N;
    for (int n = 0; n < N; ++n) in[(std::size_t) n] = (float) std::cos (w * n);

    fft.forward (in.data(), re.data(), im.data());

    int argmax = 0; float best = -1.0f;
    for (int k = 0; k < fft.numBins(); ++k)
    {
        const float mag = std::sqrt (re[(std::size_t) k] * re[(std::size_t) k]
                                   + im[(std::size_t) k] * im[(std::size_t) k]);
        if (mag > best) { best = mag; argmax = k; }
    }
    CHECK (argmax == bin);
}
