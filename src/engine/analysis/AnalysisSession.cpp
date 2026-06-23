#include "engine/analysis/AnalysisSession.h"

namespace tonefill::engine::analysis
{
AnalysisSession::AnalysisSession (core::DiagnosticsLogger& diagnostics)
    : diagnostics_ (diagnostics)
{
}

AnalysisSession::ModelResult
AnalysisSession::run (const AnalysisContext& ctx, std::atomic<bool>& cancelFlag)
{
    using core::Status;

    if (ctx.numChannels <= 0)
        return ModelResult::fail (Status::InvalidInput, "AnalysisContext has no channels");

    auto result = builder_.assemble (ctx, cancelFlag);

    if (! result.ok())
    {
        // Surface failure as a user-facing warning where it makes sense.
        if (result.error().status == Status::InsufficientMaterial)
            diagnostics_.warn ("INSUFFICIENT_MATERIAL", result.error().message);
        return result;
    }

    const auto& model = result.value();
    if (model->speechContaminationHigh)
        diagnostics_.warn ("SPEECH_CONTAMINATION", "Speech/transients dominate the learn window.");
    if (model->loopRiskHigh)
        diagnostics_.warn ("LOOP_RISK", "Target longer than available unique texture.");

    return result;
}
} // namespace tonefill::engine::analysis
