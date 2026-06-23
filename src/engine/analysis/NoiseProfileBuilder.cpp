#include "engine/analysis/NoiseProfileBuilder.h"

namespace tonefill::engine::analysis
{
model::NoiseProfile
NoiseProfileBuilder::build (const juce::AudioBuffer<float>& residualMono,
                            const juce::AudioBuffer<float>& fullMono,
                            double sampleRate,
                            const Params& params) const
{
    juce::ignoreUnused (residualMono, fullMono, sampleRate, params);

    model::NoiseProfile profile;
    profile.fftSize = params.fftSize;

    // TODO(dsp §E): compute residualLtas, fullLtas, bandEnergies, targetRms.
    return profile;
}
} // namespace tonefill::engine::analysis
