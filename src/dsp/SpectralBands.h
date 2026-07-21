#pragma once

#include <vector>

namespace tonefill::dsp
{
// Split a mono buffer into complementary Linkwitz-Riley (LR4) bands at the given crossover edges,
// low to high. Each split is a pair of cascaded 2nd-order Butterworth sections, so the low and high
// halves of a split sum in phase to an allpass (flat magnitude) -- which is why the re-synthesized
// bands recombine by plain summation without a magnitude dip at the crossovers.
//
// Returns edges.size() + 1 bands, each `n` samples long. `edges` must be ascending and below Nyquist.
std::vector<std::vector<float>> splitBandsLR (const float* x, int n, double sampleRate,
                                              const std::vector<double>& edges);
} // namespace tonefill::dsp
