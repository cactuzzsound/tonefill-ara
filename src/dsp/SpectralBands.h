#pragma once

#include <vector>

namespace tonefill::dsp
{
// Split a mono buffer into POWER-COMPLEMENTARY bands at the given crossover edges, low to high.
//
// Each split is a single 2nd-order Butterworth low/high pair, whose SQUARED magnitudes sum to 1
// (|LP|^2 + |HP|^2 = 1). That is the right property here because the bands are re-synthesized
// independently (random, incoherent grain clouds) and summed: incoherent signals add in POWER, so
// power-complementary crossovers reconstruct a flat magnitude spectrum. (Linkwitz-Riley crossovers
// are amplitude-complementary instead, which would leave a -3 dB dip at every crossover once the
// bands are decorrelated.)
//
// Returns edges.size() + 1 bands, each `n` samples long. `edges` must be ascending and below Nyquist.
std::vector<std::vector<float>> splitBands (const float* x, int n, double sampleRate,
                                            const std::vector<double>& edges);
} // namespace tonefill::dsp
