#include "plugin/ara/DocumentController.h"
#include "plugin/ara/DocumentControllerImpl.h"

// Real JUCE<->ARA binding (TF-005). CMake excludes this TU when the SDK is absent.
#if TONEFILL_ARA_AVAILABLE

namespace tonefill::plugin::ara
{
namespace
{
// Archive layout: [magic][archiveVer][count] then per clip [persistentID string][blockLen][block].
// Each block is self-describing (its own version) and length-prefixed, so an unknown clip's block
// can be skipped and a future format extended without desyncing the stream.
constexpr int kArchiveMagic  = 0x54464152; // 'TFAR'
constexpr int kArchiveVer    = 1;
constexpr int kParamBlockVer = 2; // v2 appended spectralMode + spectralBands; v1 blocks still read

void writeParamBlock (juce::OutputStream& os, plugin::SessionState& ss)
{
    os.writeInt   (kParamBlockVer);
    os.writeFloat (ss.threshold.load());
    os.writeFloat (ss.speechReject.load());
    os.writeFloat (ss.fragment.load());
    os.writeFloat (ss.blend.load());
    os.writeFloat (ss.randomness.load());
    os.writeFloat (ss.minFill.load());
    os.writeFloat (ss.flatness.load());
    os.writeFloat (ss.outputGain.load());   // linear
    os.writeFloat (ss.renderLength.load());
    os.writeFloat (ss.normalizeTarget.load());
    os.writeFloat (ss.hissFreq.load());
    os.writeFloat (ss.hissQ.load());
    os.writeBool  (ss.paulStretch.load());
    os.writeBool  (ss.normalizeEnabled.load());
    os.writeBool  (ss.normalizeLufs.load());
    os.writeBool  (ss.wholeFile.load());
    os.writeBool  (ss.statisticalMode.load());
    os.writeBool  (ss.hissFilter.load());
    os.writeBool  (ss.manualMode.load());
    os.writeInt64 ((juce::int64) ss.seed.load());

    const auto ranges = ss.getManualRanges();
    os.writeInt ((int) ranges.size());
    for (const auto& r : ranges) { os.writeInt (r.first); os.writeInt (r.second); }

    os.writeBool (ss.spectralMode.load());  // v2+
    os.writeInt  (ss.spectralBands.load()); // v2+
}

bool readParamBlock (juce::InputStream& is, plugin::SessionState& ss)
{
    const int ver = is.readInt();
    if (ver < 1 || ver > kParamBlockVer) return false;
    ss.threshold.store       (is.readFloat());
    ss.speechReject.store    (is.readFloat());
    ss.fragment.store        (is.readFloat());
    ss.blend.store           (is.readFloat());
    ss.randomness.store      (is.readFloat());
    ss.minFill.store         (is.readFloat());
    ss.flatness.store        (is.readFloat());
    ss.outputGain.store      (is.readFloat());
    ss.renderLength.store    (is.readFloat());
    ss.normalizeTarget.store (is.readFloat());
    ss.hissFreq.store        (is.readFloat());
    ss.hissQ.store           (is.readFloat());
    ss.paulStretch.store     (is.readBool());
    ss.normalizeEnabled.store(is.readBool());
    ss.normalizeLufs.store   (is.readBool());
    ss.wholeFile.store       (is.readBool());
    ss.statisticalMode.store (is.readBool());
    ss.hissFilter.store      (is.readBool());
    ss.manualMode.store      (is.readBool());
    ss.seed.store            ((std::uint64_t) is.readInt64());

    const int n = is.readInt();
    if (n < 0 || n > (1 << 20)) return false;
    std::vector<std::pair<int, int>> ranges;
    ranges.reserve ((size_t) n);
    for (int i = 0; i < n; ++i) { const int a = is.readInt(); const int b = is.readInt(); ranges.emplace_back (a, b); }
    ss.setManualRanges (std::move (ranges));

    if (ver >= 2)
    {
        ss.spectralMode.store (is.readBool());
        ss.spectralBands.store (juce::jlimit (3, 12, is.readInt()));
    }

    ss.paramsEpoch.fetch_add (1);  // editor reloads the knobs
    ss.generation.fetch_add (1);   // worker re-renders with the restored params
    return true;
}
} // namespace

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

juce::ARAEditorRenderer* ToneFillDocumentController::doCreateEditorRenderer() noexcept
{
    araLog ("DocumentController::doCreateEditorRenderer");
    return new ToneFillEditorRenderer (getDocumentController(), *this);
}

bool ToneFillDocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output,
                                                         const juce::ARAStoreObjectsFilter* filter)
{
    // Collect the (persistent-ID, state) pairs first so we can write the count up front.
    std::vector<std::pair<juce::String, std::shared_ptr<plugin::SessionState>>> toStore;
    {
        const std::lock_guard<std::mutex> l (statesLock_);
        for (const auto* source : filter->getAudioSourcesToStore<juce::ARAAudioSource>())
        {
            const auto it = states_.find (source);
            if (it != states_.end() && it->second != nullptr)
                toStore.emplace_back (juce::String (source->getPersistentID().c_str()), it->second);
        }
    }

    output.writeInt (kArchiveMagic);
    output.writeInt (kArchiveVer);
    output.writeInt ((int) toStore.size());
    for (auto& [persistentID, ss] : toStore)
    {
        juce::MemoryOutputStream block;
        writeParamBlock (block, *ss);
        output.writeString (persistentID);
        output.writeInt ((int) block.getDataSize());
        output.write (block.getData(), block.getDataSize());
    }
    return true;
}

bool ToneFillDocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                                             const juce::ARARestoreObjectsFilter* filter)
{
    if (input.readInt() != kArchiveMagic) return true; // empty / not ours -> nothing to restore
    if (input.readInt() != kArchiveVer)   return true; // newer format -> skip rather than fail

    const int count = input.readInt();
    if (count < 0 || count > (1 << 20)) return false;

    for (int i = 0; i < count && ! input.isExhausted(); ++i)
    {
        const juce::String persistentID = input.readString();
        const int blockLen = input.readInt();
        if (blockLen < 0 || blockLen > (1 << 20)) return false;

        // Apply to the live source the host mapped this archived ID to; if none, skip the block.
        if (auto* source = filter->getAudioSourceToRestoreStateWithID<juce::ARAAudioSource> (persistentID.toRawUTF8()))
        {
            juce::MemoryBlock mb;
            input.readIntoMemoryBlock (mb, blockLen);
            juce::MemoryInputStream block (mb, false);
            readParamBlock (block, *stateForSource (source));
        }
        else
        {
            input.skipNextBytes (blockLen);
        }
    }
    return ! input.failed();
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
