#pragma once

#include <cstdint>
#include <vector>

namespace tonefill::dsp
{
// PaulStretch-style smooth resynthesis (Nasca Octavian Paul's algorithm, original implementation).
// Turns the selected clean material into a CONTINUOUS, natural room-tone bed of length `n`: it
// walks the real (evolving) magnitude spectrum of the source once across the output, randomising
// the phase every frame and overlap-adding a smooth window. Because it uses the source's own
// spectrum it sounds like that room; because of the phase smear it has no grains, no joins and no
// audible repetition. Each output channel uses an independent phase stream -> natural stereo width.
//
//   windowSize : analysis/synthesis window in samples (bigger = smoother / more diffuse).
// Output is NOT level-normalised (the caller scales to the target room-tone RMS).
void paulStretch (std::vector<std::vector<float>>& out, int n,
                  const std::vector<std::vector<float>>& src,
                  int windowSize, std::uint64_t seed);
} // namespace tonefill::dsp
