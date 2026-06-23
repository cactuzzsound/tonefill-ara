#include <catch2/catch_test_macros.hpp>

#include "dsp/GranularSynth.h"
#include "dsp/SeededRng.h"

#include <cmath>
#include <vector>

using namespace tonefill::dsp;

namespace
{
// Normalized autocorrelation of x at a given lag.
double autocorrAt (const std::vector<float>& x, int lag)
{
    double num = 0.0, e0 = 0.0, e1 = 0.0;
    const int n = (int) x.size();
    for (int i = 0; i + lag < n; ++i)
    {
        num += (double) x[(std::size_t) i] * x[(std::size_t) (i + lag)];
        e0  += (double) x[(std::size_t) i] * x[(std::size_t) i];
        e1  += (double) x[(std::size_t) (i + lag)] * x[(std::size_t) (i + lag)];
    }
    const double den = std::sqrt (e0 * e1);
    return den > 0.0 ? num / den : 0.0;
}
} // namespace

TEST_CASE ("Granular fill is anti-looped: long output from short source is not periodic")
{
    const int srcLen = 8000, n = 48000, grainLen = 2400;
    SeededRng noiseRng (1);
    std::vector<float> src ((std::size_t) srcLen);
    for (auto& v : src) v = noiseRng.nextFloat() * 2.0f - 1.0f;

    std::vector<float> out ((std::size_t) n);
    SeededRng rng (99);
    granularResynthesize (out.data(), n, src.data(), srcLen, grainLen, rng);

    double energy = 0.0; for (float v : out) energy += (double) v * v;
    CHECK (energy > 0.0);                       // non-silent

    // A naive loop of the 8000-sample source would show strong correlation at lag 8000.
    // Scrambled grain order must keep it low.
    CHECK (std::fabs (autocorrAt (out, srcLen)) < 0.3);
}

TEST_CASE ("Granular resynthesis is deterministic for equal seeds")
{
    const int srcLen = 8000, n = 24000, grainLen = 2400;
    SeededRng noiseRng (2);
    std::vector<float> src ((std::size_t) srcLen);
    for (auto& v : src) v = noiseRng.nextFloat() * 2.0f - 1.0f;

    std::vector<float> a ((std::size_t) n), b ((std::size_t) n);
    SeededRng r1 (7), r2 (7);
    granularResynthesize (a.data(), n, src.data(), srcLen, grainLen, r1);
    granularResynthesize (b.data(), n, src.data(), srcLen, grainLen, r2);
    CHECK (a == b);
}
