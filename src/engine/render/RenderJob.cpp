#include "engine/render/RenderJob.h"

namespace tonefill::engine::render
{
RenderJob::RenderJob (model::AmbienceModelPtr model, model::RenderSettings settings)
    : model_ (std::move (model)), settings_ (settings)
{
}

core::Result<RenderResultPtr> RenderJob::run()
{
    using core::Status;

    if (model_ == nullptr)
        return core::Result<RenderResultPtr>::fail (Status::InvalidInput, "No AmbienceModel set");

    progress_.store (0.0f);
    auto result = renderer_.render (*model_, settings_, cancelFlag_);
    progress_.store (1.0f);

    if (! result.ok())
        return core::Result<RenderResultPtr>::fail (result.error().status, result.error().message);

    return core::Result<RenderResultPtr> (
        std::make_shared<const RenderResult> (std::move (result.value())));
}
} // namespace tonefill::engine::render
