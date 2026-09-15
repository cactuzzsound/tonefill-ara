#include "licensing/ActivationComponent.h"
#include "licensing/LicenseManager.h"

namespace tonefill::licensing
{
ActivationComponent::ActivationComponent (const juce::String& preFilledKey)
{
    title_.setText ("Activate ToneFill", juce::dontSendNotification);
    title_.setFont (juce::Font (20.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, LNF::navy());
    title_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title_);

    subtitle_.setText ("Enter your license key. Demo mode plays and auditions freely; "
                       "Export and offline Render unlock after activation.", juce::dontSendNotification);
    subtitle_.setFont (juce::Font (12.5f));
    subtitle_.setColour (juce::Label::textColourId, LNF::label());
    subtitle_.setJustificationType (juce::Justification::centredTop);
    addAndMakeVisible (subtitle_);

    key_.setText (preFilledKey, juce::dontSendNotification);
    key_.setTextToShowWhenEmpty ("XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX", LNF::muted());
    key_.setJustification (juce::Justification::centred);
    key_.setFont (juce::Font (14.0f));
    key_.setColour (juce::TextEditor::backgroundColourId, LNF::panelHi());
    key_.setColour (juce::TextEditor::outlineColourId, LNF::line());
    key_.setColour (juce::TextEditor::focusedOutlineColourId, LNF::accent());
    key_.setColour (juce::TextEditor::textColourId, LNF::navy());
    key_.onReturnKey = [this] { attemptActivation(); };
    addAndMakeVisible (key_);

    activateBtn_.getProperties().set ("accent", true); // orange
    activateBtn_.onClick = [this] { attemptActivation(); };
    addAndMakeVisible (activateBtn_);

    buyBtn_.onClick = [] { juce::URL (LicenseManager::kBuyUrl).launchInDefaultBrowser(); };
    addAndMakeVisible (buyBtn_);

    closeBtn_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn_);

    status_.setFont (juce::Font (12.0f));
    status_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (status_);

    setWantsKeyboardFocus (true);
}

ActivationComponent::~ActivationComponent() = default;

void ActivationComponent::setStatus (const juce::String& msg, juce::Colour col)
{
    status_.setText (msg, juce::dontSendNotification);
    status_.setColour (juce::Label::textColourId, col);
}

void ActivationComponent::setBusy (bool busy)
{
    busy_ = busy;
    activateBtn_.setEnabled (! busy);
    activateBtn_.setButtonText (busy ? "Checking…" : "Activate");
    key_.setEnabled (! busy);
}

void ActivationComponent::attemptActivation()
{
    if (busy_) return;
    setBusy (true);
    setStatus ("Verifying with the license server…", LNF::label());
    LicenseManager::getInstance().verifyAndActivate (key_.getText(),
        [this] (LicenseManager::Result r, juce::String msg)
        {
            setBusy (false);
            if (r == LicenseManager::Result::Success)
            {
                setStatus (msg, LNF::good());
                if (onActivated) onActivated();
            }
            else
            {
                setStatus (msg, r == LicenseManager::Result::NetworkError ? LNF::accent() : juce::Colour (0xffd0454a));
            }
        });
}

void ActivationComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xcc0a1420)); // dim scrim over the editor
    LNF::softShadow (g, card_.toFloat(), 18.0f);
    g.setColour (LNF::panel());
    g.fillRoundedRectangle (card_.toFloat(), 18.0f);
    g.setColour (LNF::lineSoft());
    g.drawRoundedRectangle (card_.toFloat(), 18.0f, 1.0f);
}

void ActivationComponent::resized()
{
    const int cw = juce::jmin (440, getWidth() - 40), chh = 300;
    card_ = juce::Rectangle<int> (0, 0, cw, chh).withCentre (getLocalBounds().getCentre());

    auto b = card_.reduced (28, 24);
    title_.setBounds (b.removeFromTop (28));
    b.removeFromTop (6);
    subtitle_.setBounds (b.removeFromTop (46));
    b.removeFromTop (10);
    key_.setBounds (b.removeFromTop (38));
    b.removeFromTop (8);
    status_.setBounds (b.removeFromTop (34));
    b.removeFromTop (6);
    auto row = b.removeFromTop (40);
    activateBtn_.setBounds (row.removeFromLeft ((row.getWidth() - 10) / 2));
    row.removeFromLeft (10);
    buyBtn_.setBounds (row);
    b.removeFromTop (8);
    closeBtn_.setBounds (b.removeFromTop (26));
}
} // namespace tonefill::licensing
