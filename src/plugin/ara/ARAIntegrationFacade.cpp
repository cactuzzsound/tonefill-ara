#include "plugin/ara/ARAIntegrationFacade.h"

#include <juce_audio_formats/juce_audio_formats.h>

// TONEFILL_ARA_AVAILABLE is injected per-target by tonefill_configure_ara() (CMake). When the
// ARA SDK is absent the macro is 0, so every ARA-SDK-specific body below compiles out and the
// facade reports "no ARA" — the plugin still builds and links as a plain insert.
#ifndef TONEFILL_ARA_AVAILABLE
 #define TONEFILL_ARA_AVAILABLE 0
#endif

namespace tonefill::plugin::ara
{
CapabilityProfile ARAIntegrationFacade::detectCapabilities() const
{
#if TONEFILL_ARA_AVAILABLE
    // TODO(TF-101): query the bound ARA document controller -> reader/sample-access
    // availability, region timeline, and whether an ARAPlaybackRenderer bounce path exists.
    // Until TF-101 lands, still report "no ARA" so the UX falls back rather than lying.
    return CapabilityProfile {};
#else
    // No ARA SDK in this build: definitively no ARA support.
    return CapabilityProfile {};
#endif
}

core::Result<engine::analysis::AnalysisContext>
ARAIntegrationFacade::acquireContext (const TargetSpan& target, float learnWindowSeconds) const
{
    juce::ignoreUnused (target, learnWindowSeconds);

#if TONEFILL_ARA_AVAILABLE
    // TODO(TF-102/103): create juce::ARAAudioSourceReader(s) for the region's own AudioSource,
    // check isSampleAccessEnabled, read the L/R learn windows + boundary buffers, resample to
    // AnalysisContext::analysisSampleRate, compute sourceContentHash, return the context.
    return core::Result<engine::analysis::AnalysisContext>::fail (
        core::Status::HostUnsupported,
        "ARA source reading not yet implemented (TF-102).");
#else
    // Built without the ARA SDK: source reading is impossible; caller uses Manual Capture.
    return core::Result<engine::analysis::AnalysisContext>::fail (
        core::Status::HostUnsupported,
        "Plugin built without ARA support; use Manual Capture.");
#endif
}

core::Status ARAIntegrationFacade::exportToWav (const core::AudioBufferView& fill,
                                                const juce::File& destination)
{
    if (! fill.isValid())
        return core::Status::InvalidInput;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());
    if (stream == nullptr)
        return core::Status::Internal;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(),
                             fill.sampleRate,
                             static_cast<unsigned int> (fill.numChannels),
                             24,
                             {},
                             0));
    if (writer == nullptr)
        return core::Status::Internal;

    stream.release(); // writer now owns the stream
    writer->writeFromFloatArrays (fill.channels, fill.numChannels, (int) fill.numSamples);
    return core::Status::Ok;
}
} // namespace tonefill::plugin::ara
