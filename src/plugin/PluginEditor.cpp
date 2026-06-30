#include "plugin/PluginEditor.h"

namespace tonefill::plugin
{
PluginEditor::PluginEditor (PluginProcessor& p)
    : juce::AudioProcessorEditor (&p), processor_ (p), mainView_ (p)
{
    addAndMakeVisible (mainView_);
    setResizable (true, true);
    setSize (600, 700);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::resized()
{
    mainView_.setBounds (getLocalBounds());
}
} // namespace tonefill::plugin
