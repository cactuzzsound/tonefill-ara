#pragma once

#ifndef TONEFILL_ARA_AVAILABLE
 #define TONEFILL_ARA_AVAILABLE 0
#endif

#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/SessionState.h"

#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace tonefill::plugin::ara
{
// Debug trace to ~/tonefill_ara.log (temporary, for bringing up the ARA path).
void araLog (const juce::String& msg);

// Small lock interface (mirrors the JUCE ARA demo): the document controller hands the renderer
// a try-read-lock so editing on the model thread can safely block audio-thread rendering.
struct ProcessingLockInterface
{
    virtual ~ProcessingLockInterface() = default;
    virtual juce::ScopedTryReadLock getProcessingLock() = 0;
};

// ARA playback renderer that REPLACES the region with synthesized room tone.
//
// Threading: analysis + fill render happen ONCE on a background thread (heavy; reads the ARA
// source off the audio thread). processBlock (audio thread) copies the precomputed fill, looped
// across the region. Until the fill is ready it outputs silence (no RT-thread source reading).
class ToneFillPlaybackRenderer : public juce::ARAPlaybackRenderer
{
public:
    ToneFillPlaybackRenderer (ARA::PlugIn::DocumentController* dc, ProcessingLockInterface& lock);
    ~ToneFillPlaybackRenderer() override;

    void prepareToPlay (double sampleRate, int maximumSamplesPerBlock, int numChannels,
                        juce::AudioProcessor::ProcessingPrecision,
                        AlwaysNonRealtime alwaysNonRealtime) override;

    void releaseResources() override;

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

    // Wired by the owning PluginProcessor in didBindToARA(). Only a fallback: prepareToPlay
    // replaces it with the state the document controller shares for our audio source, because in
    // most hosts the editor lives on a DIFFERENT instance than this renderer.
    void setSessionState (std::shared_ptr<plugin::SessionState> s) { state_ = std::move (s); }

private:
    struct FillData
    {
        std::vector<std::vector<float>> channels; // [ch][sample]
        long long length = 0;
    };

    class FillWorker; // background analyze+render thread (defined in .cpp)

    ProcessingLockInterface& lockInterface;
    ARA::PlugIn::DocumentController* documentController_ = nullptr; // to reach the shared state
    std::shared_ptr<plugin::SessionState> state_; // UI bridge, shared per audio source

    double sampleRate = 48000.0;
    int    numChannels = 0;
    int    maximumSamplesPerBlock = 0;

    // Re-published by the worker on every re-render. The audio thread copies it under a brief
    // try-lock; if it can't (worker swapping), it reuses nothing for that block (rare).
    std::shared_ptr<const FillData> fill;
    juce::SpinLock                  fillLock;
    std::atomic<bool>               fillReady { false };
    std::atomic<bool>               analysisStarted { false };
    std::unique_ptr<FillWorker>     worker;

    // Enhance-only live HF de-hiss: per-channel low-pass applied on the audio thread. Coeffs are
    // rebuilt only when the freq/Q knobs move (cheap; not per sample).
    std::vector<juce::IIRFilter> hissFilters_;
    float hissLastFreq_ = -1.0f, hissLastQ_ = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneFillPlaybackRenderer)
};

// ARA editor renderer: replaces the region with the SAME synthesized fill during audition (e.g.
// Nuendo/Cubase playing from the Sample Editor). Without this, JUCE's default editor renderer lets
// the source pass through unaltered, so those hosts play the original clip instead of the room tone.
// It does no analysis of its own -- it reads the loop the playback renderer's worker published into
// the shared per-source SessionState.
class ToneFillEditorRenderer : public juce::ARAEditorRenderer
{
public:
    ToneFillEditorRenderer (ARA::PlugIn::DocumentController* dc, ProcessingLockInterface& lock);

    void prepareToPlay (double sampleRate, int maximumSamplesPerBlock, int numChannels,
                        juce::AudioProcessor::ProcessingPrecision,
                        AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override {}

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAEditorRenderer::processBlock;

private:
    ProcessingLockInterface& lockInterface;
    ARA::PlugIn::DocumentController* documentController_ = nullptr;
    std::shared_ptr<plugin::SessionState> state_; // shared per audio source (same loop as playback)
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneFillEditorRenderer)
};
} // namespace tonefill::plugin::ara

#endif // TONEFILL_ARA_AVAILABLE
