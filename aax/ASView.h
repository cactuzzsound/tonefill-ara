#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace tonefill_aax
{
// Dedicated AudioSuite view. Mirrors the plugin's MainView as closely as AudioSuite allows: the
// only things dropped are the three Pro Tools provides natively (Full = WHOLE FILE, Audition =
// Preview, Export = Render). Shares the plugin LookAndFeel + logo. Talks to the AAX parameters
// through a Bridge (normalized 0..1 get/set).
class ASView : public juce::Component, private juce::Timer
{
public:
    struct Bridge
    {
        std::function<double (const char*)>       getNorm; // -> normalized [0,1]
        std::function<void (const char*, double)> setNorm; // normalized [0,1]
    };

    explicit ASView (Bridge bridge);
    ~ASView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Kind { KPct, KMinFill, KGain, KFreq, KQ, KNormTgt };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        const char*  id = nullptr;
        Kind         kind = KPct;
    };
    struct Toggle { juce::TextButton btn; const char* id = nullptr; };

    void timerCallback() override;
    Knob&   addKnob (const char* id, const juce::String& name, Kind kind, juce::Colour arc, const juce::String& tip);
    Toggle& addToggle (const char* id, const juce::String& name, const juce::String& tip);
    static juce::String format (Kind, double norm);

    Bridge bridge_;
    tonefill::plugin::ui::ToneFillLookAndFeel lnf_;
    juce::TooltipWindow tooltip_ { this, 600 };

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    juce::TextButton classicBtn_ { "Classic" }, expBtn_ { "Experimental" };
    juce::TextButton regenBtn_ { "Regenerate" };

    juce::Label tipLbl_;
    std::array<juce::String, 8> tips_;
    int tipIdx_ = 0, tipTick_ = 0;
    juce::Random rng_;

    std::array<juce::Rectangle<int>, 3> groupCard_;
    std::vector<juce::Rectangle<int>>   valuePills_;
    juce::Rectangle<int> normCard_, hissCard_, tipCard_, headerGroup_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASView)
};
} // namespace tonefill_aax
