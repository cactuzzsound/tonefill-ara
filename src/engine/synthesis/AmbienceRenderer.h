#pragma once

#include "engine/model/AmbienceModel.h"
#include "engine/model/RenderSettings.h"
#include "core/Result.h"

#include <atomic>
#include <vector>

namespace tonefill::engine::synthesis
{
// Mode-orchestrating synthesizer. Sums tonal + noise + grain (+ macro env) layers,
// applies edge conditioning, and trims to the exact target duration.
//
// Thread affinity: RENDER WORKER. Pure-function-like: (model, settings) -> buffer.
// Determinism   : all randomness derives from settings.seed via SeededRng sub-streams,
//                 so identical inputs produce bit-identical output (see render regression).
class AmbienceRenderer
{
public:
    // Owned, deinterleaved result: channels[ch][sample]. Exact target length.
    struct Output
    {
        std::vector<std::vector<float>> channels;
        double sampleRate = 0.0;
    };

    core::Result<Output> render (const model::AmbienceModel& model,
                                 const model::RenderSettings& settings,
                                 std::atomic<bool>& cancelFlag) const;

private:
    // Mode dispatch (design §F). Each delegates to renderCore with the right bed source.
    void renderStatic   (const model::AmbienceModel&, const model::RenderSettings&, Output&) const;
    void renderHybrid   (const model::AmbienceModel&, const model::RenderSettings&, Output&) const;
    void renderComplex  (const model::AmbienceModel&, const model::RenderSettings&, Output&) const;
    void renderAmbience (const model::AmbienceModel&, const model::RenderSettings&, Output&) const;

    // Shared engine: tonal/noise energy split, broadband bed (granular OR LTAS-shaped noise),
    // phase-docked tonal layer, total-level normalization.
    void renderCore (const model::AmbienceModel&, const model::RenderSettings&, Output&,
                     bool useGranular) const;
};
} // namespace tonefill::engine::synthesis
