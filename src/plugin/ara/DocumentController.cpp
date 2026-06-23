#include "plugin/ara/DocumentController.h"

#ifndef TONEFILL_ARA_AVAILABLE
 #define TONEFILL_ARA_AVAILABLE 0
#endif

// Real JUCE<->ARA binding (TF-005). CMake excludes this TU when the SDK is absent.
#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/ara/PlaybackRenderer.h"

namespace tonefill::plugin::ara
{
// Document controller specialisation. Owns the editing lock the playback renderer uses, and
// creates our ToneFillPlaybackRenderer (which replaces the region with synthesized room tone).
class ToneFillDocumentController : public juce::ARADocumentControllerSpecialisation,
                                   private ProcessingLockInterface
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

protected:
    void willBeginEditing (juce::ARADocument*) override { processBlockLock.enterWrite(); }
    void didEndEditing    (juce::ARADocument*) override { processBlockLock.exitWrite(); }

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override
    {
        araLog ("DocumentController::doCreatePlaybackRenderer");
        return new ToneFillPlaybackRenderer (getDocumentController(), *this);
    }

    // Archive hooks are pure-virtual. No-op for now. TODO(TF-801): persist AmbienceModel.
    bool doStoreObjectsToStream (juce::ARAOutputStream& output,
                                 const juce::ARAStoreObjectsFilter* filter) override
    {
        juce::ignoreUnused (output, filter);
        return true;
    }

    bool doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                     const juce::ARARestoreObjectsFilter* filter) override
    {
        juce::ignoreUnused (input, filter);
        return true;
    }

private:
    juce::ScopedTryReadLock getProcessingLock() override { return juce::ScopedTryReadLock { processBlockLock }; }

    juce::ReadWriteLock processBlockLock;
};
} // namespace tonefill::plugin::ara

// JUCE's plugin client (IS_ARA_EFFECT) calls this global factory to advertise ARA support.
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<
        tonefill::plugin::ara::ToneFillDocumentController>();
}

#endif // TONEFILL_ARA_AVAILABLE
