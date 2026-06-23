#include "engine/analysis/TonalModelBuilder.h"

namespace tonefill::engine::analysis
{
TonalModelBuilder::Output
TonalModelBuilder::build (const juce::AudioBuffer<float>& cleanMono,
                          double sampleRate,
                          const Params& params) const
{
    juce::ignoreUnused (sampleRate, params);

    Output out;

    // TODO(dsp §E): replace this passthrough with sinusoidal modelling.
    // Phase-1 scaffold: empty tonal layer, residual == input (no tonal removed yet).
    out.residual.makeCopyOf (cleanMono);
    return out;
}
} // namespace tonefill::engine::analysis
