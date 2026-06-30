#pragma once

#ifndef TONEFILL_USE_FVAD
 #define TONEFILL_USE_FVAD 0
#endif

#include <vector>

namespace tonefill::dsp
{
// Per-sample voiced mask (1 = speech) for a mono signal, via libfvad (WebRTC VAD).
// `aggressiveness` 0..3: 0 = most sensitive to speech (flags more), 3 = only confident speech.
// Returns an empty vector when libfvad is not compiled in (caller then uses level-only gating).
std::vector<char> detectVoice (const float* mono, int n, double sampleRate, int aggressiveness);
} // namespace tonefill::dsp
