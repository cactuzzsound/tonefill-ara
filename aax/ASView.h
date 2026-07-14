#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <functional>
#include <memory>
#include <vector>

namespace tonefill_aax
{
// Dedicated AudioSuite view (NOT a port of the plugin's MainView): compact, tuned for the Pro Tools
// workflow (no Export Len / Full / Audition — PT provides those). Shares the plugin's LookAndFeel
// and logo. Talks to the AAX parameters through a Bridge (normalized 0..1 get/set).
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
    enum Kind { KPct, KMinFill, KGain, KFreq, KQ, KSeed, KNormTgt };

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        const char*  id = nullptr;
        Kind         kind = KPct;
    };
    struct Toggle { juce::TextButton btn; const char* id = nullptr; };

    void timerCallback() override;          // reflect host/automation changes into the controls
    Knob&   addKnob (const char* id, const juce::String& name, Kind kind, juce::Colour arc);
    Toggle& addToggle (const char* id, const juce::String& name);
    static juce::String format (Kind, double norm);

    Bridge bridge_;
    tonefill::plugin::ui::ToneFillLookAndFeel lnf_;

    std::vector<std::unique_ptr<Knob>>   knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;

    // Rectangles rebuilt in resized(), drawn in paint().
    std::array<juce::Rectangle<int>, 3> groupCard_;
    std::vector<juce::Rectangle<int>>   valuePills_;
    juce::Rectangle<int> toggleCard_, extraCard_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASView)
};
} // namespace tonefill_aax
