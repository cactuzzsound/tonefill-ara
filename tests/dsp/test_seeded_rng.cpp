#include <catch2/catch_test_macros.hpp>
#include "dsp/SeededRng.h"

using tonefill::dsp::SeededRng;

// Determinism is a hard contract (design §N): identical seeds must reproduce identical
// sequences, and sub-streams must diverge from the master and from each other.
TEST_CASE ("SeededRng is reproducible for equal seeds")
{
    SeededRng a (12345), b (12345);
    for (int i = 0; i < 1000; ++i)
        REQUIRE (a.nextUInt64() == b.nextUInt64());
}

TEST_CASE ("SeededRng sub-streams are independent")
{
    SeededRng master (999);
    auto s0 = master.deriveSubStream (0);
    auto s1 = master.deriveSubStream (1);

    bool diverged = false;
    for (int i = 0; i < 64; ++i)
        if (s0.nextUInt64() != s1.nextUInt64()) { diverged = true; break; }

    REQUIRE (diverged);
}

TEST_CASE ("SeededRng nextFloat stays in [0,1)")
{
    SeededRng rng (7);
    for (int i = 0; i < 10000; ++i)
    {
        const float f = rng.nextFloat();
        REQUIRE (f >= 0.0f);
        REQUIRE (f <  1.0f);
    }
}
