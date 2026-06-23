#include <catch2/catch_test_macros.hpp>

#include "dsp/AmbienceConcat.h"
#include "dsp/SeededRng.h"

#include <cmath>
#include <vector>

using namespace tonefill::dsp;

namespace
{
double rms (const std::vector<float>& x)
{
    double e = 0.0; for (float v : x) e += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (e / x.size());
}
} // namespace

TEST_CASE ("Concatenative ambience: real-level, deterministic, longer-than-source")
{
    const int srcLen = 48000; // 1 s of source ambience
    SeededRng nz (1);
    std::vector<float> src ((std::size_t) srcLen);
    for (auto& v : src) v = 0.1f * (nz.nextFloat() * 2.0f - 1.0f);

    const int n = 48000 * 5; // 5 s fill from 1 s source
    const int fragLen = 24000, xfade = 6000;

    std::vector<float> a ((std::size_t) n), b ((std::size_t) n);
    SeededRng r1 (7), r2 (7);
    concatenateAmbience (a.data(), n, src.data(), srcLen, fragLen, xfade, r1);
    concatenateAmbience (b.data(), n, src.data(), srcLen, fragLen, xfade, r2);

    CHECK (rms (a) > 0.0);                          // non-silent
    CHECK (a == b);                                 // deterministic
    // Output is REAL audio -> its level matches the source (within the crossfade's small dip).
    CHECK (std::abs (rms (a) - rms (src)) < 0.03);
}

TEST_CASE ("Concatenative ambience handles a source shorter than one fragment")
{
    const int srcLen = 4000;
    std::vector<float> src ((std::size_t) srcLen, 0.2f);
    const int n = 20000;
    std::vector<float> out ((std::size_t) n);
    SeededRng rng (3);
    concatenateAmbience (out.data(), n, src.data(), srcLen, 12000, 3000, rng);
    CHECK (rms (out) > 0.0); // falls back to looping the short source, still produces audio
}
