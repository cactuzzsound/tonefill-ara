#pragma once

#include "engine/model/GrainCorpus.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::engine::analysis
{
// Cuts overlapping windowed grains from the residual and computes per-grain features
// for similarity-based concatenation (design §E). Enforces a memory budget by decimating.
//
// Thread affinity: ANALYSIS WORKER.
class GrainCorpusBuilder
{
public:
    struct Params
    {
        float grainSizeMs       = 120.0f;
        float hopFraction       = 0.5f;     // extraction stride as fraction of grain size
        int   minUsableGrains   = 20;       // below this -> loopRiskHigh
        std::size_t maxBytes    = 64u * 1024u * 1024u; // per-channel corpus memory cap
    };

    // `residualMono` is tonal-removed (and optionally transient-removed) clean ambience.
    model::GrainCorpus build (const juce::AudioBuffer<float>& residualMono,
                              double sampleRate,
                              const Params& params) const;

private:
    // TODO(dsp §E): windowed grain extraction (Hann/Tukey) at hopFraction stride.
    // TODO(dsp §E): per-grain features (rms, centroid, flatness, band energies).
    // TODO(dsp §E): optional brute-force kNN adjacency for concatenation cost.
    // TODO(dsp §E): decimate to maxBytes; set usableCount.
};
} // namespace tonefill::engine::analysis
