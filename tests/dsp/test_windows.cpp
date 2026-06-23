#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Windows.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace tonefill::dsp;
using Catch::Matchers::WithinAbs;

// COLA: overlap-adding the periodic Hann at hop = N/2 and N/4 must sum to a constant in the
// steady-state (interior) region. This is the property STFT resynthesis relies on.
static double colaInteriorMaxDeviation (const std::vector<float>& w, int hop)
{
    const int N = (int) w.size();
    const int span = 4 * N;
    std::vector<double> acc ((std::size_t) span, 0.0);
    for (int start = 0; start + N <= span; start += hop)
        for (int n = 0; n < N; ++n)
            acc[(std::size_t) (start + n)] += w[(std::size_t) n];

    // Inspect the interior, away from the ramp-up/down edges.
    const int lo = N, hi = span - N;
    double mean = 0.0; int c = 0;
    for (int i = lo; i < hi; ++i) { mean += acc[(std::size_t) i]; ++c; }
    mean /= (double) c;
    double maxDev = 0.0;
    for (int i = lo; i < hi; ++i)
        maxDev = std::max (maxDev, std::fabs (acc[(std::size_t) i] - mean));
    return maxDev / mean; // relative
}

TEST_CASE ("Periodic Hann satisfies COLA at 50% and 75% overlap")
{
    const auto w = makeWindow (WindowType::Hann, 1024);
    CHECK (colaInteriorMaxDeviation (w, 512) < 1e-4); // 50%
    CHECK (colaInteriorMaxDeviation (w, 256) < 1e-4); // 75%
}

TEST_CASE ("Tukey is flat in the middle and tapers to ~0 at the edges")
{
    const auto w = makeWindow (WindowType::Tukey, 1024, 0.5);
    CHECK_THAT (w.front(), WithinAbs (0.0f, 1e-5f));
    CHECK_THAT (w[512],    WithinAbs (1.0f, 1e-5f)); // centre is in the flat region
}
