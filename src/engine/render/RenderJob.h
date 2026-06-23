#pragma once

#include "engine/model/AmbienceModel.h"
#include "engine/model/RenderSettings.h"
#include "engine/render/RenderCache.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "core/Result.h"

#include <atomic>

namespace tonefill::engine::render
{
// One cancellable synthesis job: (model, settings) -> rendered fill.
// Thread affinity: executed on the RENDER WORKER. cancel() may be called from any thread.
//
// A RenderJob is single-use. RenderManager constructs one per request and supersedes
// (cancels) any stale in-flight job.
class RenderJob
{
public:
    RenderJob (model::AmbienceModelPtr model, model::RenderSettings settings);

    core::Result<RenderResultPtr> run();          // blocking; call on the worker
    void  cancel() noexcept { cancelFlag_.store (true); }
    bool  isCancelled() const noexcept { return cancelFlag_.load(); }
    float progress() const noexcept { return progress_.load(); }

    const model::RenderSettings& settings() const noexcept { return settings_; }
    core::ModelId modelId() const noexcept { return model_ ? model_->modelId : core::kInvalidModelId; }

private:
    model::AmbienceModelPtr  model_;
    model::RenderSettings    settings_;
    synthesis::AmbienceRenderer renderer_;

    std::atomic<bool>  cancelFlag_ { false };
    std::atomic<float> progress_   { 0.0f };
};
} // namespace tonefill::engine::render
