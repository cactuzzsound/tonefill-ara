#pragma once

#include "engine/model/NoiseProfile.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::engine::analysis
{
// Estimates the diffuse stationary bed: residual LTAS (shapes synth noise), full LTAS
// (energy/output matching), per-band energies, target RMS (design §E).
//
// Thread affinity: ANALYSIS WORKER.
class NoiseProfileBuilder
{
public:
    struct Params
    {
        int   fftSize             = 2048;
        int   hopSize             = 512;
        float smoothingOctaveFrac = 1.0f / 6.0f; // fractional-octave LTAS smoothing
    };

    // `residualMono` is the tonal-removed clean ambience for one channel.
    // `fullMono`     is the same material WITH tonal content, for fullLtas / energy targets.
    model::NoiseProfile build (const juce::AudioBuffer<float>& residualMono,
                               const juce::AudioBuffer<float>& fullMono,
                               double sampleRate,
                               const Params& params) const;

private:
    // TODO(dsp §E): magnitude LTAS (median/mean over accepted frames) + octave smoothing.
    // TODO(dsp §E): per-band energy integration + target RMS estimation.
};
} // namespace tonefill::engine::analysis
