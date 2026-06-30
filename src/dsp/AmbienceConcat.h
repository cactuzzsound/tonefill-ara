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
//   variation : 0..1 (user "Variation"), per-fragment level jitter to break audible repetition
void concatenateAmbience (float* out, int n,
                          const float* src, int srcLen,
                          int fragLen, int xfadeLen,
                          SeededRng& rng, int antiRepeat = 8, float variation = 0.0f);

// Grain-cloud ambience: overlap-add MANY real grains (Hann-windowed) taken from random positions
// in `src`, on a regular output grid with high overlap `density` (grains overlapping at once).
// Because the grains are real recorded room tone it sounds like the actual room; because many
// overlap and sum, individual grain boundaries are masked and louder bits are averaged down ->
// a steady, smoothly-blended bed. Output is NOT level-normalised (caller scales to target RMS).
//
//   grainLen : grain length in samples (user "Chunk Size")
//   density  : how many grains overlap at once (>= 1; higher = smoother, hop = grainLen/density)
//   variation: 0..1, reversal probability + position jitter to avoid audible repetition
void grainCloud (float* out, int n,
                 const float* src, int srcLen,
                 int grainLen, int density,
                 SeededRng& rng, int antiRepeat = 12, float variation = 0.4f);
} // namespace tonefill::dsp
