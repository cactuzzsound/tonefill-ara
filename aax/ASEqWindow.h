#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"
#include "plugin/ui/EqView.h"
#include "ToneFillAS_Defs.h"

#include <functional>
#include <memory>
#include <string>

namespace tonefill_aax
{
// AudioSuite EQ editor window: hosts the SHARED EqView (same graph as the VST) and bridges its
// real-value Access to the AAX normalized parameter get/set. A small timer keeps the controls synced.
class ASEqWindow : public juce::DocumentWindow
{
    using LNF = tonefill::plugin::ui::ToneFillLookAndFeel;
    using EV  = tonefill::plugin::ui::EqView;

    // Map an EqView field token to its AAX param field + real range.
    static void fieldSpec (const char* field, std::string& idField, double& lo, double& hi, bool& isBool)
    {
        const juce::String f (field);
        if      (f == "On")   { idField = "on";   lo = 0;   hi = 1;                       isBool = true;  }
        else if (f == "Type") { idField = "type"; lo = 0;   hi = kEqNumTypesAAX - 1;      isBool = false; }
        else if (f == "Freq") { idField = "freq"; lo = 20;  hi = 20000;                   isBool = false; }
        else if (f == "Gain") { idField = "gain"; lo = -18; hi = 18;                      isBool = false; }
        else                  { idField = "q";    lo = 0.1; hi = 10;                      isBool = false; }
    }

    struct Content : public juce::Component, private juce::Timer
    {
        Content (EV::Access a) : eq (std::move (a)) { addAndMakeVisible (eq); startTimerHz (12); }
        void resized() override { eq.setBounds (getLocalBounds().reduced (12)); }
        void timerCallback() override { eq.refresh(); }
        EV eq;
    };

public:
    std::function<void()> onClose;

    ASEqWindow (std::function<double (const char*)> getNorm,
                std::function<void (const char*, double)> setNorm)
        : juce::DocumentWindow ("ToneFill EQ", LNF::bg(), juce::DocumentWindow::closeButton)
    {
        EV::Access acc;
        acc.getReal = [getNorm] (int band, const char* field) -> float
        {
            std::string idf; double lo, hi; bool isB; fieldSpec (field, idf, lo, hi, isB);
            const std::string id = eqAaxId (band + 1, idf.c_str());
            const double norm = getNorm (id.c_str());
            return isB ? (norm > 0.5 ? 1.0f : 0.0f) : (float) (lo + norm * (hi - lo));
        };
        acc.setReal = [setNorm] (int band, const char* field, float v)
        {
            std::string idf; double lo, hi; bool isB; fieldSpec (field, idf, lo, hi, isB);
            const std::string id = eqAaxId (band + 1, idf.c_str());
            const double norm = isB ? (v > 0.5f ? 1.0 : 0.0)
                                    : juce::jlimit (0.0, 1.0, ((double) v - lo) / (hi - lo));
            setNorm (id.c_str(), norm);
        };

        setContentOwned (new Content (std::move (acc)), false);
        setResizable (true, true);
        setUsingNativeTitleBar (true);
        centreWithSize (760, 340);
        setVisible (true);
    }

    void closeButtonPressed() override { if (onClose) onClose(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASEqWindow)
};
} // namespace tonefill_aax
