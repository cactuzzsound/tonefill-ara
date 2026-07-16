#pragma once

#ifndef TONEFILL_ARA_AVAILABLE
 #define TONEFILL_ARA_AVAILABLE 0
#endif

#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/SessionState.h"
#include "plugin/ara/PlaybackRenderer.h"

#include <map>
#include <memory>
#include <mutex>

namespace tonefill::plugin::ara
{
// Document controller specialisation. Owns the editing lock the playback renderer uses, creates
// our ToneFillPlaybackRenderer, and hands out the SessionState shared by every plug-in instance
// working on a given audio source.
//
// Why the state lives here rather than in PluginProcessor: hosts split the ARA roles across
// SEPARATE plug-in instances. Reaper gives the instance you see both the editor and the
// playbackRenderer role, so a per-instance SessionState happened to work there. Nuendo/Cubase put
// the editor on one instance and the renderer on another, which left the editor writing knob
// values no worker ever read, and reading an export fill no worker ever wrote. The document
// controller is the only object those instances share.
//
// Keyed by audio source rather than one state per document: a document holds many clips (Reaper
// makes an instance per item) and those must stay independent of each other.
class ToneFillDocumentController : public juce::ARADocumentControllerSpecialisation,
                                   private ProcessingLockInterface
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    // Get-or-create the state for a source. Callable from any thread.
    std::shared_ptr<plugin::SessionState> stateForSource (const juce::ARAAudioSource* source);

    // The state for the document's only audio source, or nullptr if it has none or several.
    // Lets an instance that holds no region of its own (how some hosts bind an editor-only
    // instance) still find the state for the single clip being edited.
    std::shared_ptr<plugin::SessionState> stateForSoleSource();

protected:
    void willBeginEditing (juce::ARADocument*) override { processBlockLock.enterWrite(); }
    void didEndEditing    (juce::ARADocument*) override { processBlockLock.exitWrite(); }

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    // Persist each clip's parameters into the ARA archive, keyed by the audio source's persistent
    // ID, so per-clip settings survive project save/reload. (The AmbienceModel itself is still
    // recomputed on load, not archived.)
    bool doStoreObjectsToStream (juce::ARAOutputStream& output,
                                 const juce::ARAStoreObjectsFilter* filter) override;
    bool doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                     const juce::ARARestoreObjectsFilter* filter) override;

private:
    juce::ScopedTryReadLock getProcessingLock() override { return juce::ScopedTryReadLock { processBlockLock }; }

    juce::ReadWriteLock processBlockLock;

    std::mutex statesLock_;
    std::map<const juce::ARAAudioSource*, std::shared_ptr<plugin::SessionState>> states_;
};

// Our document controller behind an ARA one, or nullptr.
ToneFillDocumentController* specialisedDocumentController (ARA::PlugIn::DocumentController* dc);

} // namespace tonefill::plugin::ara

#endif // TONEFILL_ARA_AVAILABLE
