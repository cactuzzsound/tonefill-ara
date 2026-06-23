#pragma once

#include "core/Ids.h"
#include "engine/model/TonalLayer.h"
#include "engine/model/NoiseProfile.h"
#include "engine/model/GrainCorpus.h"
#include "engine/model/BoundaryConditionProfile.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tonefill::engine::model
{
// The complete learned description of the room.
//
// Ownership : produced by AmbienceModelBuilder; held via shared_ptr<const AmbienceModel>.
// Mutability: IMMUTABLE after construction. Shared across render + audio threads by const
//             shared_ptr and swapped atomically. Never mutate in place.
// Lifetime  : survives regenerate and all render-only parameter changes; invalidated only
//             by target / learn-region / analysis-parameter changes. Serialized into the
//             ARA archive (version-checked on restore).
struct AmbienceModel
{
    static constexpr std::uint32_t kCurrentVersion = 1;

    std::uint32_t   modelVersion       = kCurrentVersion;
    core::ModelId   modelId            = core::kInvalidModelId; // cache key
    int             numChannels        = 0;
    double          analysisSampleRate = 0.0;

    // Pre/post seam profiles for one channel (unambiguous vs a flat side+channel vector).
    struct ChannelBoundaries
    {
        BoundaryConditionProfile pre;   // end of the audio before the gap
        BoundaryConditionProfile post;  // start of the audio after the gap
    };

    // Per-channel layers (each size == numChannels).
    std::vector<TonalLayer>       tonalPerChannel;
    std::vector<NoiseProfile>     noisePerChannel;
    std::vector<GrainCorpus>      grainsPerChannel;
    std::vector<ChannelBoundaries> boundariesPerChannel;

    // Selected clean real ambience per channel (for the Ambience concatenative mode).
    std::vector<std::vector<float>> cleanAudioPerChannel;

    // Diagnostics surfaced to the UI (drive warning states; see DiagnosticsLogger).
    float learnMaterialSeconds   = 0.0f;
    bool  speechContaminationHigh = false;
    bool  loopRiskHigh            = false;
};

using AmbienceModelPtr = std::shared_ptr<const AmbienceModel>;
} // namespace tonefill::engine::model
