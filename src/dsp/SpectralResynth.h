#pragma once

#include "dsp/NoiseModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace tonefill::dsp
{
// Random-phase spectral re-synthesis of a constant room-tone bed from a NoiseSpectrum.
//
// For every output frame each bin gets a Rayleigh-distributed magnitude with mean = model
// magnitude (the natural statistics of band-limited noise) and a uniformly random phase; an
// inverse FFT + Hann overlap-add (75%) turns that into continuous time-domain noise shaped to the
// room's spectrum. The result is perfectly stationary, has NO concatenation joins or loud bursts,
// and is as long as requested. Each channel uses an independent random stream -> decorrelated,
// natural stereo width. Finally normalised so the channel RMS == targetRms.
void synthesizeBed (juce::AudioBuffer<float>& out, int numSamples,
                    const NoiseSpectrum& model, std::uint64_t seed, float targetRms);
} // namespace tonefill::dsp
