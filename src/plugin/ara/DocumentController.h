#pragma once

#include "plugin/ara/CapabilityProfile.h"

// When ARA is wired, include JUCE's ARA document-controller specialization header here,
// e.g. <juce_audio_processors/juce_audio_processors.h> with the ARA module enabled, and
// derive from juce::ARADocumentControllerSpecialisation. Kept abstract for Phase 0 so the
// engine + plugin compile without the ARA SDK present.

namespace tonefill::plugin::ara
{
// ARA document controller specialization (scaffold).
//
// Thread affinity: ARA / host callback threads. Heavy work (analysis/synthesis) MUST be
// marshalled to the engine workers; never block an ARA callback on long DSP.
//
// Responsibilities (design §H/§I):
//   - lifecycle hooks for ARA documents / audio sources / playback regions
//   - archive store/restore of the AmbienceModel (version-checked)
//   - expose the detected CapabilityProfile to the processor/UI
//
// Companion ARA classes still to add when the SDK is wired (omitted from the first draft):
//   - PluginProcessor must also inherit juce::AudioProcessorARAExtension.
//   - an ARAPlaybackRenderer subclass supplies the synthesized fill for the region's sample
//     range when the host pulls audio (playback/bounce). This is the real delivery path.
//   - optionally an ARAEditorRenderer/ARAEditorView for audition while editing (V1.1+).
class DocumentController /* : public juce::ARADocumentControllerSpecialisation */
{
public:
    DocumentController() = default;
    virtual ~DocumentController() = default;

    const CapabilityProfile& capabilities() const noexcept { return capabilities_; }

    // TODO(ARA): override doCreateAudioSource / doCreatePlaybackRegion / analysis hooks.
    // TODO(ARA): storeArchive() / restoreArchive() <-> AmbienceModel serialization (§H).
    // TODO(ARA): populate capabilities_ during binding via ARAIntegrationFacade.

protected:
    CapabilityProfile capabilities_;
};
} // namespace tonefill::plugin::ara
