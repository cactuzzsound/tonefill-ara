#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace tonefill::plugin
{
// Temporary bring-up trace (always-on path, independent of ARA) -> ~/tonefill_ara.log.
static void pluginLog (const juce::String& msg)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("tonefill_ara.log")
        .appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  [proc] " + msg + "\n");
}

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params_ (*this)
{
    pluginLog ("PluginProcessor ctor (ARA_AVAILABLE=" + juce::String ((int) TONEFILL_ARA_AVAILABLE) + ")");
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
#if TONEFILL_ARA_AVAILABLE
    // Route ARA playback/bounce through the ARA renderer roles.
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#else
    juce::ignoreUnused (sampleRate, samplesPerBlock);
#endif
}

void PluginProcessor::releaseResources()
{
#if TONEFILL_ARA_AVAILABLE
    releaseResourcesForARA();
#endif
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

#if TONEFILL_ARA_AVAILABLE
    static std::atomic<bool> loggedProc { false };
    if (! loggedProc.exchange (true))
        pluginLog ("processBlock first call: isBoundToARA=" + juce::String ((int) isBoundToARA())
                   + " ch=" + juce::String (buffer.getNumChannels()));
    // When ARA-bound, the ARAPlaybackRenderer serves the synthesized fill for the region.
    if (processBlockForARA (buffer, isRealtime(), getPlayHead()))
        return;
#endif
    // Not ARA-bound (plain insert) -> passthrough for now.
    // TODO(TF-705): mix in the cached preview fill when previewing.
    juce::ignoreUnused (buffer);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = params_.apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        params_.apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace tonefill::plugin

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonefill::plugin::PluginProcessor();
}
