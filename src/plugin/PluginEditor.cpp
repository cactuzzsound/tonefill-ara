#include "plugin/PluginEditor.h"

namespace tonefill::plugin
{
PluginEditor::PluginEditor (PluginProcessor& p)
    : juce::AudioProcessorEditor (&p), processor_ (p), mainView_ (p)
{
    addAndMakeVisible (mainView_);
    setResizable (true, true);
    if (auto* c = getConstrainer()) c->setMinimumSize (860, 560);
    setSize (980, 640);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::resized()
{
    mainView_.setBounds (getLocalBounds());
}
} // namespace tonefill::plugin
