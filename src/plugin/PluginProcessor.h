#pragma once

#include "plugin/ParameterState.h"
#include "plugin/SessionState.h"
#include "plugin/ara/ARAIntegrationFacade.h"
#include "engine/render/RenderManager.h"
#include "engine/model/AmbienceModel.h"
#include "core/DiagnosticsLogger.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

// Injected per-target by tonefill_configure_ara() (CMake). 0 when the ARA SDK is absent.
#ifndef TONEFILL_ARA_AVAILABLE
 #define TONEFILL_ARA_AVAILABLE 0
#endif

namespace tonefill::plugin
{
// JUCE entry point + host glue. Declared as an ARA effect via CMake (IS_ARA_EFFECT).
//
// ARA wiring (TODO when the ARA SDK is configured): this processor must also inherit
// juce::AudioProcessorARAExtension, the DocumentController must derive from
// juce::ARADocumentControllerSpecialisation, and a juce::ARAPlaybackRenderer subclass must
// supply the synthesized fill for the playback region's sample range. In ARA, the host
// PULLS audio through the playback renderer; the plugin never pushes samples to the host.
//
// Threading: processBlock() runs on the AUDIO thread. When ARA-hosted, playback is served
// by the ARA playback renderer reading the precomputed fill; when loaded as a plain insert
// (no ARA), processBlock passes through. Either way it does a lock-free read of an immutable
// fill buffer and never allocates.
//
// TF-005 insertion point: when TONEFILL_ARA_AVAILABLE, add the ARA extension base, e.g.
//     class PluginProcessor : public juce::AudioProcessor,
//                             public juce::AudioProcessorARAExtension { ... };
// The exact base/header is JUCE-version-specific and needs the SDK to compile, so it is left
// as a documented seam rather than activated blind. The non-ARA declaration below compiles in
// both build modes today.
class PluginProcessor : public juce::AudioProcessor
#if TONEFILL_ARA_AVAILABLE
                      , public juce::AudioProcessorARAExtension
#endif
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ToneFill"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;   // ParameterState only
    void setStateInformation (const void*, int) override;

    // Accessors for the editor.
    ParameterState&            parameters()  noexcept { return params_; }
    core::DiagnosticsLogger&   diagnostics() noexcept { return diagnostics_; }
    engine::render::RenderManager& renderManager() noexcept { return renderManager_; }
    ara::ARAIntegrationFacade& araFacade()   noexcept { return araFacade_; }

    // UI <-> worker bridge, shared with the other instances working on the same ARA audio source
    // (see ara/DocumentControllerImpl.h) and scoped to it, so separate clips stay independent.
    SessionState&                 sessionState()    noexcept { return *statePtr_.load (std::memory_order_acquire); }
    std::shared_ptr<SessionState> sessionStatePtr() noexcept { return sharedState_ != nullptr ? sharedState_ : ownState_; }

#if TONEFILL_ARA_AVAILABLE
    // Called by JUCE once this instance is bound to ARA.
    void didBindToARA() noexcept override;

    // Point this instance at the SessionState shared for our audio source. Cheap no-op once
    // resolved; the editor retries on its timer because a host may attach the region after
    // binding. Message thread only.
    bool tryResolveSharedState();
#endif

    // TODO(ARA): expose the ARA document controller factory via JUCE ARA support. With
    // IS_ARA_EFFECT, JUCE generates the binding; we provide the specialization in ara/.

private:
    using PreviewBuffer = std::vector<std::vector<float>>;

    core::DiagnosticsLogger        diagnostics_;
    ParameterState                 params_;          // APVTS source of truth (message thread)
    ara::ARAIntegrationFacade      araFacade_;        // host boundary
    engine::render::RenderManager  renderManager_;    // owns render worker + cache

    // shared_ptr (not a plain member) so the ARA renderer/worker can co-own it and we never
    // get a use-after-free from base/member destruction-order surprises.
    //
    // ownState_ is what we use until the shared one is found, and outside ARA entirely. It is
    // kept alive for the processor's lifetime even after sharedState_ takes over, so statePtr_
    // can never dangle across the swap.
    std::shared_ptr<SessionState>  ownState_ { std::make_shared<SessionState>() };
    std::shared_ptr<SessionState>  sharedState_;                    // from the document controller
    std::atomic<SessionState*>     statePtr_ { ownState_.get() };   // what sessionState() returns

    // Non-ARA LEARN / GENERATE path (Pro Tools AudioSuite, or any plain insert). Learn mode:
    // capture + analyze the selection into learnedModel_ (output = passthrough). Generate mode:
    // synthesize room tone from learnedModel_ over the selection (no need to re-read it), so the
    // render starts at sample 0 (no head silence) and responds to the knobs.
    void analyzeLearn();                            // learnInput_ -> learnedModel_
    void buildGenFill();                            // learnedModel_ + params -> genFill_
    std::uint64_t genParamHash() const;             // render-param fingerprint (rebuild trigger)

    juce::AudioBuffer<float>        learnInput_;     // accumulated material to learn from (capped)
    int                            learnInputLen_  = 0;
    bool                           learnDone_      = false;
    engine::model::AmbienceModelPtr learnedModel_;
    std::vector<std::vector<float>> genFill_;        // loopable room-tone fill from the model
    long long                      genFillLen_     = 0;
    long long                      genPos_         = 0;
    std::uint64_t                  genHash_        = 0;
    double                         currentSampleRate_ = 48000.0;
    int                            hostBlockSize_  = 0; // to detect the final partial block

    // Owns the current preview fill (message thread). The buffer is published to the audio
    // thread via a raw atomic pointer so the audio thread never touches a shared_ptr control
    // block (std::shared_ptr atomic ops are NOT guaranteed lock-free and must not run on the
    // audio thread). currentPreview_ stays alive as the owner; previewPtr_ points into it and
    // the old buffer is reclaimed on the message thread after a swap.
    // TODO(rt): replace with a proper SPSC handoff (e.g. juce::AbstractFifo or a small
    // retire-list) once preview playback is implemented.
    std::shared_ptr<const PreviewBuffer> currentPreview_;          // message-thread owner
    std::atomic<const PreviewBuffer*>    previewPtr_ { nullptr };  // audio-thread reads this
    std::atomic<long long>               previewReadPos_ { 0 };
    std::atomic<bool>                    previewPlaying_ { false };

    // Deterministic render seed. Plain serialized state (not an APVTS parameter): owned here,
    // bumped by the Regenerate action, persisted in get/setStateInformation alongside APVTS.
    std::uint64_t renderSeed_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace tonefill::plugin
