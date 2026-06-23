#include "engine/render/RenderManager.h"

namespace tonefill::engine::render
{
RenderManager::RenderManager()  = default;
RenderManager::~RenderManager() = default;

void RenderManager::setModel (model::AmbienceModelPtr model)
{
    std::lock_guard<std::mutex> lock (mutex_);
    model_ = std::move (model);
    if (active_)
        active_->cancel();
    cache_.invalidate();
}

void RenderManager::requestRender (const model::RenderSettings& settings, DoneCallback cb)
{
    if (auto cached = getCached (settings))
    {
        if (cb) cb (core::Result<RenderResultPtr> (cached));
        return;
    }
    dispatch (settings, std::move (cb));
}

RenderResultPtr RenderManager::getCached (const model::RenderSettings& settings) const
{
    std::lock_guard<std::mutex> lock (mutex_);
    if (model_ == nullptr)
        return nullptr;
    return cache_.get (model_->modelId, settings.hash());
}

void RenderManager::dispatch (const model::RenderSettings& settings, DoneCallback cb)
{
    model::AmbienceModelPtr model;
    {
        std::lock_guard<std::mutex> lock (mutex_);
        if (active_)
            active_->cancel();           // supersede the stale job
        model = model_;
    }

    if (model == nullptr)
    {
        if (cb)
            cb (core::Result<RenderResultPtr>::fail (core::Status::InvalidInput, "No model analyzed yet"));
        return;
    }

    auto job = std::make_unique<RenderJob> (model, settings);

    // TODO(threading): enqueue job->run() on the render worker instead of running inline.
    // Inline execution keeps the scaffold deterministic and unit-testable for now.
    auto result = job->run();

    if (result.ok())
        cache_.put (model->modelId, settings.hash(), result.value());

    {
        std::lock_guard<std::mutex> lock (mutex_);
        active_ = std::move (job);
    }

    if (cb)
        cb (std::move (result));
}
} // namespace tonefill::engine::render
