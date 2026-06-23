#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Crossfade.h"
#include "dsp/SeededRng.h"

#include <cmath>
#include <vector>

using namespace tonefill::dsp;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Equal-power crossfade: endpoints and constant power")
{
    const int n = 256;
    std::vector<float> a ((std::size_t) n, 1.0f), b ((std::size_t) n, 1.0f), out ((std::size_t) n);
    equalPowerCrossfade (a.data(), b.data(), out.data(), n);

    CHECK_THAT (out.front(), WithinAbs (1.0f, 1e-5f)); // starts on a
    CHECK_THAT (out.back(),  WithinAbs (1.0f, 1e-5f)); // ends on b
    // Two equal unit-DC signals: equal-power blend gives cos+sin in [1, sqrt(2)] — never dips.
    float mn = 2.0f;
    for (float v : out) mn = std::min (mn, v);
    CHECK (mn > 0.99f);
}

TEST_CASE ("Crossfade smooths a level discontinuity")
{
    // The discontinuity crossfade actually fixes: a step in level/value (e.g. real neighbour
    // ending high, fill starting low). Two opposite DC levels make it unambiguous.
    const int K = 256;
    std::vector<float> a ((std::size_t) K, 1.0f), b ((std::size_t) K, -1.0f), xf ((std::size_t) K);

    const float hardStep = std::fabs (b[0] - a[(std::size_t) (K - 1)]); // == 2.0

    equalPowerCrossfade (a.data(), b.data(), xf.data(), K);

    float xfStep = 0.0f;
    for (int i = 1; i < K; ++i)
        xfStep = std::max (xfStep, std::fabs (xf[(std::size_t) i] - xf[(std::size_t) (i - 1)]));

    INFO ("hardStep=" << hardStep << " xfStep=" << xfStep);
    CHECK (xfStep < hardStep * 0.05f); // gradual ramp instead of a cliff
}
