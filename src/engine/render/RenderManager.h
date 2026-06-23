#pragma once

#include "engine/model/AmbienceModel.h"
#include "engine/model/RenderSettings.h"
#include "engine/render/RenderJob.h"
#include "engine/render/RenderCache.h"
#include "core/Result.h"

#include <functional>
#include <memory>
#include <mutex>

namespace tonefill::engine::render
{
// Owns the render worker, the render cache, and supersede-on-new-request logic.
//
// Threading: requestRender() is called from the MESSAGE thread. In the Phase-2 scaffold the
// work runs INLINE and the DoneCallback fires synchronously on the calling thread (keeps the
// data flow deterministic and unit-testable). TODO(threading, Phase 5): move job execution
// onto a single render worker and marshal the DoneCallback back to the message thread via
// juce::MessageManager::callAsync. A newer request cancels the stale in-flight job.
//
// Seed ownership: "Regenerate" is NOT handled here. The processor owns the render seed
// (plain state), bumps it, and calls requestRender with the new RenderSettings. This keeps a
// single source of truth for the seed and avoids it desyncing from persisted state.
class RenderManager
{
public:
    using DoneCallback = std::function<void (core::Result<RenderResultPtr>)>;

    RenderManager();
    ~RenderManager();

    // Atomically swap the active model. Invalidates the render cache.
    void setModel (model::AmbienceModelPtr model);

    // Serves from cache when (modelId, settings.hash()) is present; otherwise renders.
    void requestRender (const model::RenderSettings& settings, DoneCallback cb);

    RenderResultPtr getCached (const model::RenderSettings& settings) const;

private:
    void dispatch (const model::RenderSettings& settings, DoneCallback cb);

    model::AmbienceModelPtr      model_;     // shared_ptr<const>, guarded by mutex_
    RenderCache                  cache_;
    std::unique_ptr<RenderJob>   active_;    // current/most-recent job (cancellable)
    mutable std::mutex           mutex_;

    // TODO(threading): back this with a single-thread worker (juce::ThreadPool size 1,
    //   or a dedicated juce::Thread). The scaffold runs dispatch() synchronously so the
    //   data flow compiles and is testable; swap in real async execution in Phase 5.
};
} // namespace tonefill::engine::render
