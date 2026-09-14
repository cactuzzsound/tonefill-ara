#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <array>
#include <functional>

namespace tonefill::plugin::ui
{
// The number of EQ bands (kept local so this widget has no dependency on plugin/AAX state headers).
static constexpr int kEqViewBands = 5;

// Filter-type order MUST match SessionState::EqType / dsp::makeEqCoefficients / the AAX param order.
enum EqViewType { EvBell = 0, EvLowShelf, EvHighShelf, EvHighPass, EvLowPass, EvNotch, kEqViewNumTypes };

// Enhance-only parametric EQ editor: an interactive frequency-response graph (drag band nodes) plus
// Type / Freq / Gain / Q controls for the selected band. Backend-agnostic: it reads/writes bands via
// an Access adapter (APVTS in the VST, the AAX parameter bridge in AudioSuite). The owner calls
// refresh() periodically to keep the controls in sync. Field tokens are "On"/"Type"/"Freq"/"Gain"/"Q";
// band is 0-based; getReal/setReal use REAL values (On/Type as 0/1 and the type index).
class EqView : public juce::Component
{
public:
    struct Access
    {
        std::function<float (int band, const char* field)>            getReal;
        std::function<void  (int band, const char* field, float val)> setReal;
    };

    explicit EqView (Access access);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void refresh(); // pull current param values into the controls + repaint

private:
    using LNF = ToneFillLookAndFeel;
    static constexpr int kEqBands = kEqViewBands;

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

    Access access_;
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
