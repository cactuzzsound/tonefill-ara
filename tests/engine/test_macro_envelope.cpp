#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/MacroEnvelope.h"
#include "dsp/SeededRng.h"

#include <algorithm>
#include <vector>

using namespace tonefill::dsp;
using Catch::Matchers::WithinAbs;

// The macro envelope itself, isolated from the bed: it must (a) leave the endpoints at unity
// (so seam levels are preserved), (b) introduce real slow level variation, (c) be deterministic.
TEST_CASE ("Macro envelope: anchored endpoints, real variation, deterministic")
{
    const double sr = 48000.0;
    const int n = (int) (sr * 4.0); // 4 s -> several breaths
    const float depth = 0.15f;

    std::vector<float> a ((std::size_t) n, 1.0f), b ((std::size_t) n, 1.0f);
    SeededRng r1 (42), r2 (42);
    applyMacroEnvelope (a.data(), n, depth, sr, r1);
    applyMacroEnvelope (b.data(), n, depth, sr, r2);

    // (a) endpoints anchored to unity -> boundary level untouched.
    CHECK_THAT (a.front(), WithinAbs (1.0f, 1e-4f));
    CHECK_THAT (a.back(),  WithinAbs (1.0f, 1e-4f));

    // (b) real variation, bounded by depth.
    float mn = a[0], mx = a[0];
    for (float v : a) { mn = std::min (mn, v); mx = std::max (mx, v); }
    CHECK ((mx - mn) > 0.05f);                 // it actually moves
    CHECK (mn > 1.0f - depth - 1e-3f);         // stays within +/- depth
    CHECK (mx < 1.0f + depth + 1e-3f);

    // (c) deterministic.
    CHECK (a == b);
}

TEST_CASE ("Macro envelope is a no-op at zero depth")
{
    const double sr = 48000.0;
    const int n = 10000;
    std::vector<float> a ((std::size_t) n, 0.7f);
    SeededRng rng (1);
    applyMacroEnvelope (a.data(), n, 0.0f, sr, rng);
    for (float v : a) CHECK_THAT (v, WithinAbs (0.7f, 1e-6f));
}
