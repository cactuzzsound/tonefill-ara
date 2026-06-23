#pragma once

#include "plugin/ara/CapabilityProfile.h"
#include "engine/analysis/AnalysisContext.h"
#include "core/AudioBufferView.h"
#include "core/Result.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::plugin::ara
{
// The ONLY place that touches ARA objects. Translates host concepts into host-agnostic
// engine inputs, keeping the engine free of any ARA/host dependency.
//
// IMPORTANT ARA model constraints (corrected from the first draft):
//  * The plugin can only read the AudioSource(s) that ITS OWN playback region references —
//    not arbitrary timeline audio. Room tone before/after a cut WITHIN THE SAME CLIP is
//    reachable; ambience on another track/region generally is not. If the flanking audio is
//    not in an accessible source, acquireContext fails -> Manual Capture fallback.
//  * Sample access on a source may be DISABLED by the host; reads must check and defer.
//  * Output is delivered by an ARAPlaybackRenderer the host PULLS from (see PluginProcessor),
//    NOT by pushing samples to the host. There is no "commit to host" call. "Baking" happens
//    when the user bounces in the host, during which the playback renderer returns the fill.
//    The only push-style path we own is the optional WAV export.
//
// Threading: capability detection runs on the ARA/host thread; source reads happen on the
// ANALYSIS WORKER. NEVER read sources on the audio thread.
//
// NOTE: concrete ARA SDK / JUCE-ARA calls are intentionally stubbed. The method shapes are
// stable; the bodies are filled in once the ARA document controller is wired (design §I).
class ARAIntegrationFacade
{
public:
    // Describes the region to fill, in the source's own sample domain.
    struct TargetSpan
    {
        long long startSample = 0;
        long long lengthSamples = 0;
        double    sampleRate = 0.0;
        int       numChannels = 0;
    };

    // Detect what the host supports. Called after the ARA document controller is bound.
    CapabilityProfile detectCapabilities() const;

    // Read clean ambience + boundary buffers around the target span (within the same
    // AudioSource) and resample to the analysis rate. Returns owned buffers the engine can
    // analyze. Fails with HostUnsupported/InsufficientMaterial when sources are unreadable.
    core::Result<engine::analysis::AnalysisContext>
    acquireContext (const TargetSpan& target, float learnWindowSeconds) const;

    // Optional, always-available export safety net (juce::AudioFormatWriter). This is NOT the
    // primary delivery path under ARA (the playback renderer is) — it lets the editor place
    // the fill manually when ARA render/bounce is awkward or unavailable.
    core::Status exportToWav (const core::AudioBufferView& fill, const juce::File& destination);

private:
    // TODO(ARA): hold a non-owning handle to the ARA document controller / region context.
    // TODO(ARA): detectCapabilities() -> reader/sample-access availability, region timeline,
    //            and whether an ARAPlaybackRenderer path is usable for bounce.
    // TODO(ARA): acquireContext() -> create juce::ARAAudioSourceReader(s) for the region's
    //            source, check isSampleAccessEnabled, read learn ranges + boundary buffers,
    //            resample to AnalysisContext::analysisSampleRate, compute sourceContentHash.
    // NOTE(ARA): fill delivery lives in the ARAPlaybackRenderer (PluginProcessor), not here.
};
} // namespace tonefill::plugin::ara
