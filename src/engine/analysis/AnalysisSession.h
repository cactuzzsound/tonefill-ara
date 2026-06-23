#pragma once

#include "engine/analysis/AnalysisContext.h"
#include "engine/analysis/AmbienceModelBuilder.h"
#include "engine/model/AmbienceModel.h"
#include "core/Result.h"
#include "core/DiagnosticsLogger.h"

#include <atomic>

namespace tonefill::engine::analysis
{
// Orchestrates the analysis pipeline into an immutable AmbienceModel.
//
// Thread affinity: runs on the ANALYSIS WORKER thread ONLY. Never call from the audio
// thread. Cancellation is cooperative (cancelFlag), checked at frame-loop granularity.
class AnalysisSession
{
public:
    using ModelResult = core::Result<model::AmbienceModelPtr>;

    explicit AnalysisSession (core::DiagnosticsLogger& diagnostics);

    ModelResult run (const AnalysisContext& ctx, std::atomic<bool>& cancelFlag);

private:
    core::DiagnosticsLogger& diagnostics_;
    AmbienceModelBuilder     builder_;
};
} // namespace tonefill::engine::analysis
