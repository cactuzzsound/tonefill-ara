#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

namespace tonefill::plugin::ui
{
// Dark "Earthy Forest" theme. Knobs use the Modern_4 chrome film-strip skin (embedded), framed
// by a soft green value arc; cards, tabs and buttons use the olive/fern/teal palette.
//
// Palette: Dust Grey #DAD7CD, Dry Sage #A3B18A, Fern #588157, Hunter Green #3A5A40,
//          Pine Teal #344E41.
class ToneFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour bg()        { return juce::Colour (0xff0f1512); } // near-black forest
    static juce::Colour panel()     { return juce::Colour (0xff19221d); } // card fill
    static juce::Colour panelHi()   { return juce::Colour (0xff223029); } // hover / inset
    static juce::Colour line()      { return juce::Colour (0xff2c3a31); } // borders / tracks
    static juce::Colour accent()    { return juce::Colour (0xff6aa57f); } // brightened Fern (glow)
    static juce::Colour accentDeep(){ return juce::Colour (0xff3a5a40); } // Hunter Green
    static juce::Colour text()      { return juce::Colour (0xffdad7cd); } // Dust Grey
    static juce::Colour muted()     { return juce::Colour (0xffa3b18a); } // Dry Sage
    static juce::Colour dust()      { return juce::Colour (0xffdad7cd); } // Dust Grey (icons)
    static juce::Colour sage()      { return juce::Colour (0xffa3b18a); } // Dry Sage

    ToneFillLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, text());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text());
        setColour (juce::ComboBox::backgroundColourId, panelHi());
        setColour (juce::ComboBox::textColourId, text());
        setColour (juce::ComboBox::outlineColourId, line());
        setColour (juce::ComboBox::arrowColourId, accent());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent().withAlpha (0.25f));
        setColour (juce::TextEditor::backgroundColourId, panelHi());
        setColour (juce::TextEditor::textColourId, text());
        setColour (juce::TextEditor::highlightColourId, accent().withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, accent());
    }

    static const juce::Image& knobImage()
    {
        static juce::Image img =
            juce::ImageCache::getFromMemory (BinaryData::knob_modern_png, BinaryData::knob_modern_pngSize);
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto cell = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float d = juce::jmin (cell.getWidth(), cell.getHeight());
        const auto  c = cell.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float arcR = d * 0.5f - 2.5f;

        // Value-arc ring: green progress over a faint track (the on-brand "glow" from the mockup).
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (line().withAlpha (0.7f));
        g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (accent());
        g.strokePath (val, juce::PathStrokeType (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // soft outer glow
        g.setColour (accent().withAlpha (0.18f));
        g.strokePath (val, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto& img = knobImage();
        if (img.isValid() && img.getWidth() > 0)
        {
            const int fw     = img.getWidth();
            const int frames = juce::jmax (1, img.getHeight() / fw);
            const int idx    = juce::jlimit (0, frames - 1, (int) std::lround (pos * (float) (frames - 1)));
            const float kd   = d * 0.78f;
            auto dest = juce::Rectangle<float> (kd, kd).withCentre (c);
            g.drawImage (img, (int) dest.getX(), (int) dest.getY(), (int) dest.getWidth(), (int) dest.getHeight(),
                         0, idx * fw, fw, fw, false);
        }
        else
        {
            // Fallback: simple vector knob if the skin failed to load.
            const float r = d * 0.36f;
            g.setColour (panelHi());
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
            const auto tip  = c.getPointOnCircumference (r - 2.0f, angle);
            const auto root = c.getPointOnCircumference (r * 0.4f, angle);
            g.setColour (text());
            g.drawLine ({ root, tip }, 2.0f);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                               bool over, bool down) override
    {
        const bool on = btn.getToggleState() || btn.getProperties().getWithDefault ("accent", false);
        auto r = btn.getLocalBounds().toFloat().reduced (0.8f);
        const float rad = 9.0f;

        g.setColour (on ? accent().withAlpha (down ? 0.32f : 0.18f)
                        : (over ? panelHi() : panel()));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? accent() : line());
        g.drawRoundedRectangle (r, rad, on ? 1.3f : 0.9f);
        if (on)
        {
            g.setColour (accent().withAlpha (0.12f));
            g.drawRoundedRectangle (r.reduced (1.4f), rad - 1.4f, 1.4f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        const bool on = btn.getToggleState() || btn.getProperties().getWithDefault ("accent", false);
        g.setColour (on ? accent().brighter (0.25f) : text());
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, false);
    }
};
} // namespace tonefill::plugin::ui
