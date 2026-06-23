#include "engine/analysis/CandidateFrameSelector.h"

namespace tonefill::engine::analysis
{
std::vector<CandidateFrameSelector::Segment>
CandidateFrameSelector::select (const juce::AudioBuffer<float>& context,
                                double sampleRate,
                                const Params& params) const
{
    juce::ignoreUnused (context, sampleRate, params);

    // TODO(dsp §E): implement STFT framing + feature extraction + scoring + segment grouping.
    // For Phase 1 scaffolding this returns no segments; downstream builders must treat an
    // empty result as "insufficient material" and surface the corresponding warning.
    return {};
}
} // namespace tonefill::engine::analysis
