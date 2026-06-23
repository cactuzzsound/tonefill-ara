#include <catch2/catch_test_macros.hpp>

#include "dsp/TonalDetect.h"
#include "golden/fixtures.h"

#include <cmath>

using namespace tonefill::dsp;

TEST_CASE ("Detects a single sine with correct frequency and amplitude")
{
    const double sr = 48000.0;
    auto tone = tonefill::test::sine (1, 48000, sr, 1000.0); // amplitude 0.25
    auto p = detectPartials (tone.getReadPointer (0), tone.getNumSamples(), sr);

    REQUIRE (! p.empty());
    CHECK (std::fabs (p[0].freqHz - 1000.0f) < 3.0f);
    CHECK (std::fabs (p[0].amplitude - 0.25f) < 0.03f);
}

TEST_CASE ("Resolves closely-spaced mains-hum harmonics")
{
    const double sr = 48000.0;
    auto h = tonefill::test::hum (1, 48000, sr, 50.0); // 50/100/150/200 @ 0.2/0.1/0.05/0.025
    auto p = detectPartials (h.getReadPointer (0), h.getNumSamples(), sr);

    auto found = [&] (double f)
    {
        for (const auto& x : p) if (std::fabs ((double) x.freqHz - f) < 4.0) return x.amplitude;
        return 0.0f;
    };

    CHECK (found (50.0)  > 0.15f);   // strongest
    CHECK (found (100.0) > 0.07f);
    CHECK (found (150.0) > 0.03f);
    CHECK (found (50.0)  > found (100.0)); // amplitude ordering preserved
}
