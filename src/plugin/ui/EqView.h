#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"
#include "plugin/SessionState.h"

#include <array>

namespace tonefill::plugin::ui
{
// Enhance-only parametric EQ editor: an interactive frequency-response graph (drag band nodes) plus
// Type / Freq / Gain / Q controls for the selected band. Reads and writes the APVTS EQ parameters
// directly; MainView calls refresh() from its timer to keep the controls in sync.
class EqView : public juce::Component
{
public:
    explicit EqView (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void refresh(); // pull current param values into the controls + repaint

private:
    using LNF = ToneFillLookAndFeel;

    juce::RangedAudioParameter* param (int band, const char* field) const;
    float realVal (int band, const char* field) const;
    void  setReal (int band, const char* field, float v);
    bool  bandOn  (int band) const;
    void  setBandOn (int band, bool on);
    int   bandType (int band) const;
    static bool typeHasGain (int type);

    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    int   hitNode (juce::Point<float> pos) const;
    juce::Colour bandColour (int band) const;
    void  selectBand (int b);
    void  syncSelectedControls();

    juce::AudioProcessorValueTreeState& apvts_;
    int  sel_ = 0;
    bool dragging_ = false;

    juce::Rectangle<int> graph_;
    juce::Slider   freqK_, gainK_, qK_;
    juce::Label    freqL_, gainL_, qL_;
    juce::ComboBox typeBox_;
    juce::TextButton onBtn_ { "Band On" };
    std::array<juce::TextButton, kEqBands> bandBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqView)
};
} // namespace tonefill::plugin::ui
