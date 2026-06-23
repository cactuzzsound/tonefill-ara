#pragma once

#include "engine/analysis/AnalysisContext.h"
#include "engine/analysis/CandidateFrameSelector.h"
#include "engine/analysis/TonalModelBuilder.h"
#include "engine/analysis/NoiseProfileBuilder.h"
#include "engine/analysis/GrainCorpusBuilder.h"
#include "engine/model/AmbienceModel.h"
#include "core/Result.h"

#include <atomic>

namespace tonefill::engine::analysis
{
// Assembles a complete, immutable AmbienceModel from the individual builders.
// Owns no persistent state; constructed per analysis run by AnalysisSession.
//
// Thread affinity: ANALYSIS WORKER. Cooperative cancellation via cancelFlag.
class AmbienceModelBuilder
{
public:
    core::Result<model::AmbienceModelPtr>
    assemble (const AnalysisContext& ctx, std::atomic<bool>& cancelFlag) const;

private:
    CandidateFrameSelector frameSelector_;
    TonalModelBuilder      tonalBuilder_;
    NoiseProfileBuilder    noiseBuilder_;
    GrainCorpusBuilder     grainBuilder_;

    // TODO(§E): BoundaryMatcher once seam capture is implemented.
};
} // namespace tonefill::engine::analysis
