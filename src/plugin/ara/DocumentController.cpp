#include "plugin/ara/DocumentController.h"
#include "plugin/ara/DocumentControllerImpl.h"

// Real JUCE<->ARA binding (TF-005). CMake excludes this TU when the SDK is absent.
#if TONEFILL_ARA_AVAILABLE

namespace tonefill::plugin::ara
{
std::shared_ptr<plugin::SessionState>
ToneFillDocumentController::stateForSource (const juce::ARAAudioSource* source)
{
    if (source == nullptr)
        return nullptr;

    const std::lock_guard<std::mutex> l (statesLock_);
    auto& slot = states_[source];
    if (slot == nullptr)
        slot = std::make_shared<plugin::SessionState>();
    return slot;
}

std::shared_ptr<plugin::SessionState> ToneFillDocumentController::stateForSoleSource()
{
    auto* document = getDocument();
    if (document == nullptr)
        return nullptr;

    const auto& sources = document->getAudioSources();
    if (sources.size() != 1)
        return nullptr;

    return stateForSource (sources.front());
}

juce::ARAPlaybackRenderer* ToneFillDocumentController::doCreatePlaybackRenderer() noexcept
{
    araLog ("DocumentController::doCreatePlaybackRenderer");
    return new ToneFillPlaybackRenderer (getDocumentController(), *this);
}

bool ToneFillDocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output,
                                                         const juce::ARAStoreObjectsFilter* filter)
{
    juce::ignoreUnused (output, filter);
    return true;
}

bool ToneFillDocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                                             const juce::ARARestoreObjectsFilter* filter)
{
    juce::ignoreUnused (input, filter);
    return true;
}

ToneFillDocumentController* specialisedDocumentController (ARA::PlugIn::DocumentController* dc)
{
    if (dc == nullptr)
        return nullptr;

    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<
        ToneFillDocumentController> (dc);
}
} // namespace tonefill::plugin::ara

// JUCE's plugin client (IS_ARA_EFFECT) calls this global factory to advertise ARA support.
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<
        tonefill::plugin::ara::ToneFillDocumentController>();
}

#endif // TONEFILL_ARA_AVAILABLE
