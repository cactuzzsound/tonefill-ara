#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"

#include <functional>

namespace tonefill::licensing
{
// On-demand activation dialog (ToneFill light theme). Shown when the user hits Export/Render or an
// "Activate" affordance while unactivated. Covers its parent with a scrim + a centred card.
class ActivationComponent : public juce::Component
{
public:
    explicit ActivationComponent (const juce::String& preFilledKey = {});
    ~ActivationComponent() override;

    std::function<void()> onActivated; // license accepted + stored
    std::function<void()> onClose;     // dismissed (only offered during an active trial)

    // blocking = trial expired: the dialog can't be dismissed until a key is entered.
    void setMode (bool blocking, int trialDaysLeft);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using LNF = tonefill::plugin::ui::ToneFillLookAndFeel;

    void attemptActivation();
    void setStatus (const juce::String& msg, juce::Colour col);
    void setBusy (bool busy);

    juce::Label      title_, subtitle_, status_;
    juce::TextEditor key_;
    juce::TextButton activateBtn_ { "Activate" }, buyBtn_ { "Buy license" }, closeBtn_ { "Continue in demo" };
    juce::Rectangle<int> card_;
    bool busy_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActivationComponent)
};
} // namespace tonefill::licensing
