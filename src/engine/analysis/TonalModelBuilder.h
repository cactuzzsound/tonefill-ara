#pragma once

#include "engine/model/TonalLayer.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::engine::analysis
{
// Builds the tonal skeleton (hum / HVAC / whine) via sinusoidal peak picking + partial
// tracking over the clean learn material (design §E). Also exposes the tonal-removed
// residual so the noise/grain builders work on the right signal.
//
// Thread affinity: ANALYSIS WORKER.
class TonalModelBuilder
{
public:
    struct Params
    {
        int   fftSize           = 2048;
        int   hopSize           = 512;
        float peakThresholdDb   = 9.0f;   // above the median-filtered noise floor
        float minPersistence    = 0.60f;  // partial must appear in >= 60% of frames
        float tonalSensitivity  = 0.5f;   // maps to threshold/persistence at runtime
    };

    struct Output
    {
        model::TonalLayer        layer;
        juce::AudioBuffer<float> residual; // input minus resynthesized partials (single channel)
    };

    // `cleanMono` is one channel of selected clean ambience material.
    Output build (const juce::AudioBuffer<float>& cleanMono,
                  double sampleRate,
                  const Params& params) const;

private:
    // TODO(dsp §E): spectral peak picking + parabolic interpolation (freq/amp/phase).
    // TODO(dsp §E): peak linking into partials (birth/death, freq continuity ~3%).
    // TODO(dsp §E): persistence/stability filtering -> keep steady tones only.
    // TODO(dsp §E): synthesize partials and subtract to produce the residual.
};
} // namespace tonefill::engine::analysis
