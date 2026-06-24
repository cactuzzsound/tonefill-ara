#include <catch2/catch_test_macros.hpp>

#include "core/DiagnosticsLogger.h"
#include "dsp/SeededRng.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/model/AmbienceModel.h"

#include <atomic>
#include <cmath>

using namespace tonefill::engine;

// The model must learn the room-tone LEVEL from the quiet gaps, not the loud dialogue. We build
// a signal alternating loud bursts (sine, amp 0.4) with quiet ambience (amp 0.02), then check
// the learned targetRms sits near the quiet floor, not the overall (loud-dominated) RMS.
TEST_CASE ("Clean-ambience selection learns from the quiet gaps, not the loud parts")
{
    const double sr = 48000.0;
    const int seg = 24000; // 0.5 s segments
    const int numSeg = 12; // 6 s total
    juce::AudioBuffer<float> buf (1, seg * numSeg);
    auto* d = buf.getWritePointer (0);

    tonefill::dsp::SeededRng rng (5);
    for (int s = 0; s < numSeg; ++s)
    {
        const bool loud = (s % 2) == 0;            // loud broadband bursts vs quiet broadband floor
        const float amp = loud ? 0.4f : 0.02f;
        for (int i = 0; i < seg; ++i)
            d[s * seg + i] = amp * (rng.nextFloat() * 2.0f - 1.0f);
    }

    const float overallRms = buf.getRMSLevel (0, 0, buf.getNumSamples()); // ~0.2 (loud-dominated)

    analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (buf);
    ctx.rightContext.makeCopyOf (buf);
    ctx.analysisSampleRate = sr;
    ctx.numChannels = 1;
    ctx.leftEnabled = true;
    ctx.sourceContentHash = 1;

    tonefill::core::DiagnosticsLogger diag;
    analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };
    auto res = session.run (ctx, cancel);
    REQUIRE (res.ok());

    const float learnedRms = res.value()->noisePerChannel[0].targetRms;
    INFO ("overallRms=" << overallRms << " learnedRms=" << learnedRms);

    CHECK (overallRms > 0.15f);        // overall is loud-dominated
    CHECK (learnedRms < 0.10f);        // learned level is near the quiet ambience, not the dialogue
}
