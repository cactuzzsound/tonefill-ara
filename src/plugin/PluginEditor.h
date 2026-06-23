#pragma once

#include "plugin/PluginProcessor.h"
#include "plugin/ui/MainView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tonefill::plugin
{
// Thin editor shell. All real UI lives in ui/MainView. Message thread only.
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor& p);
    ~PluginEditor() override;

    void resized() override;

private:
    PluginProcessor&   processor_;
    ui::MainView       mainView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace tonefill::plugin
