#include "plugin/PluginEditor.h"

namespace tonefill::plugin
{
PluginEditor::PluginEditor (PluginProcessor& p)
    : juce::AudioProcessorEditor (&p),
     #if TONEFILL_ARA_AVAILABLE
      juce::AudioProcessorEditorARAExtension (&p),
     #endif
      processor_ (p), mainView_ (p)
{
    addAndMakeVisible (mainView_);
    setResizable (true, true);
    if (auto* c = getConstrainer()) c->setMinimumSize (1080, 580);
    setSize (1200, 656);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::resized()
{
    mainView_.setBounds (getLocalBounds());
}
} // namespace tonefill::plugin
