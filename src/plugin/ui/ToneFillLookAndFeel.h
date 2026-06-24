#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonefill::plugin::ui
{
// Flat, vector-drawn dark theme (no image assets). Matches the design sketch: ring + indicator
// knobs with a teal value arc, flat dark buttons, dark combo.
class ToneFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour bg()     { return juce::Colour (0xff1b1d22); }
    static juce::Colour panel()  { return juce::Colour (0xff23262e); }
    static juce::Colour line()   { return juce::Colour (0xff3a3f4a); }
    static juce::Colour accent() { return juce::Colour (0xff5fd08a); }
    static juce::Colour text()   { return juce::Colour (0xffc8ccd4); }
    static juce::Colour muted()  { return juce::Colour (0xff7a808c); }

    ToneFillLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, muted());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text());
        setColour (juce::ComboBox::backgroundColourId, panel());
        setColour (juce::ComboBox::textColourId, text());
        setColour (juce::ComboBox::outlineColourId, line());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent().withAlpha (0.25f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (5.0f);
        const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (line());
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (accent());
        g.strokePath (arc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto tip  = c.getPointOnCircumference (radius - 2.0f, angle);
        const auto root = c.getPointOnCircumference (radius * 0.42f, angle);
        g.setColour (text().withAlpha (0.9f));
        g.drawLine ({ root, tip }, 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                               bool over, bool down) override
    {
        const bool accentBtn = btn.getToggleState() || btn.getProperties().getWithDefault ("accent", false);
        auto r = btn.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (accentBtn ? accent().withAlpha (down ? 0.4f : 0.22f)
                               : (over ? panel().brighter (0.15f) : panel()));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accentBtn ? accent().withAlpha (0.7f) : line());
        g.drawRoundedRectangle (r, 8.0f, 0.8f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        const bool accentBtn = btn.getToggleState() || btn.getProperties().getWithDefault ("accent", false);
        g.setColour (accentBtn ? accent() : text());
        g.setFont (juce::Font (13.0f));
        g.drawText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, false);
    }
};
} // namespace tonefill::plugin::ui
