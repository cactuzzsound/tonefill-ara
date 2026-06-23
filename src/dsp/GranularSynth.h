#pragma once

#include "dsp/SeededRng.h"

namespace tonefill::dsp
{
// Concatenative granular resynthesis: fills out[0..n) by overlap-adding Tukey-windowed grains
// drawn from `src`, in a seeded-random order with an anti-repeat constraint. This breaks the
// periodicity that makes a short looped bed sound obviously looped, while preserving the
// source's local texture and spectrum.
//
// Deterministic for a given rng state. Output is roughly source-level (caller rescales).
// If srcLen < 2*grainLen the source is too short to scramble -> falls back to tiling.
void granularResynthesize (float* out, int n,
                           const float* src, int srcLen,
                           int grainLen, SeededRng& rng, int antiRepeat = 8);
} // namespace tonefill::dsp
