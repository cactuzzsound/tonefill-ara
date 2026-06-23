#include "engine/analysis/GrainCorpusBuilder.h"

namespace tonefill::engine::analysis
{
model::GrainCorpus
GrainCorpusBuilder::build (const juce::AudioBuffer<float>& residualMono,
                           double sampleRate,
                           const Params& params) const
{
    juce::ignoreUnused (residualMono);

    model::GrainCorpus corpus;
    corpus.grainSizeSamples = static_cast<int> (params.grainSizeMs * 0.001 * sampleRate);
    corpus.hopSamples       = static_cast<int> (corpus.grainSizeSamples * params.hopFraction);

    // TODO(dsp §E): extract grains + features; set usableCount.
    return corpus;
}
} // namespace tonefill::engine::analysis
