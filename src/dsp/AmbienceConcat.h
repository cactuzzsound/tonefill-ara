#pragma once

#include "dsp/SeededRng.h"

namespace tonefill::dsp
{
// Concatenative real-ambience fill: builds out[0..n) by placing real fragments taken from
// `src` (clean recorded ambience) end-to-end, equal-power crossfaded over their overlap, in a
// seeded-random non-repeating order. Unlike granular this keeps long real chunks (natural
// texture/events) and avoids both the obvious loop and synthesis artifacts.
//
//   fragLen   : fragment length in samples (user "Fragment")
//   xfadeLen  : crossfade/overlap in samples (user "Blend"), 0 < xfadeLen < fragLen
//   antiRepeat: how many recent fragment starts to avoid reusing
void concatenateAmbience (float* out, int n,
                          const float* src, int srcLen,
                          int fragLen, int xfadeLen,
                          SeededRng& rng, int antiRepeat = 8);
} // namespace tonefill::dsp
