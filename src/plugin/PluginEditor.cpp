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
    // Lock the window proportions to the default (users can scale, but not distort the layout).
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (1200.0 / 656.0);
        c->setMinimumSize (1000, 547);   // ~ same aspect
        c->setMaximumSize (2400, 1312);  // ~ same aspect
    }
    setSize (1200, 656);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::resized()
{
    mainView_.setBounds (getLocalBounds());
}
} // namespace tonefill::plugin
