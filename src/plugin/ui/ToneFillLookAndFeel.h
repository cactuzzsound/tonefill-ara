#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonefill::plugin::ui
{
// Light "studio" theme (cream / navy / orange / purple). Vector knobs: a light face inside a navy
// ring, framed by a per-knob value arc (orange for Detection/Output, purple for Structure/Texture -
// set via Slider::rotarySliderFillColourId). Buttons read as flat segmented chips: navy when active.
class ToneFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour bg()       { return juce::Colour (0xfff6f4ef); } // cream page
    static juce::Colour panel()    { return juce::Colour (0xffffffff); } // card fill
    static juce::Colour panelHi()  { return juce::Colour (0xfff4f2ec); } // knob face / inset
    static juce::Colour line()     { return juce::Colour (0xffe7e3db); } // borders
    static juce::Colour lineSoft() { return juce::Colour (0xffece8e0); } // faint dividers / track
    static juce::Colour navy()     { return juce::Colour (0xff22305a); } // primary text + knob body
    static juce::Colour accent()   { return juce::Colour (0xffe28e2b); } // orange
    static juce::Colour purple()   { return juce::Colour (0xff6e5fb3); } // purple
    static juce::Colour coral()    { return juce::Colour (0xffe2603e); } // export / meter top
    static juce::Colour good()     { return juce::Colour (0xff3e8e5a); } // ready dot
    static juce::Colour text()     { return juce::Colour (0xff22305a); } // navy text
    static juce::Colour muted()    { return juce::Colour (0xff9a9585); } // captions
    static juce::Colour label()    { return juce::Colour (0xff7a7d86); } // knob labels

    ToneFillLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, navy());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, accent());
        setColour (juce::Label::textColourId, navy());
        setColour (juce::ComboBox::backgroundColourId, panel());
        setColour (juce::ComboBox::textColourId, navy());
        setColour (juce::ComboBox::outlineColourId, line());
        setColour (juce::ComboBox::arrowColourId, muted());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, navy());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, purple().withAlpha (0.16f));
        setColour (juce::TextEditor::backgroundColourId, panel());
        setColour (juce::TextEditor::textColourId, navy());
        setColour (juce::CaretComponent::caretColourId, purple());
        // Tooltips: navy box + white text so hover help is readable on the light UI.
        setColour (juce::TooltipWindow::backgroundColourId, navy());
        setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::outlineColourId, navy());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider& s) override
    {
        auto cell = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float d = juce::jmin (cell.getWidth(), cell.getHeight());
        const auto  c = cell.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float arcR  = d * 0.5f - 4.0f;
        const auto  arcCol = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xffe4e0d6));
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (arcCol);
        g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float rBody = d * 0.34f;
        g.setColour (navy());
        g.fillEllipse (c.x - rBody, c.y - rBody, rBody * 2.0f, rBody * 2.0f);
        const float rFace = rBody * 0.72f;
        g.setColour (panelHi());
        g.fillEllipse (c.x - rFace, c.y - rFace, rFace * 2.0f, rFace * 2.0f);

        const auto tip  = c.getPointOnCircumference (rBody - 3.0f, angle);
        const auto root = c.getPointOnCircumference (rFace * 0.45f, angle);
        g.setColour (navy());
        g.drawLine ({ root, tip }, 3.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                               bool over, bool down) override
    {
        const bool on     = btn.getToggleState();
        const bool coralB = btn.getProperties().getWithDefault ("accent", false);
        auto r = btn.getLocalBounds().toFloat().reduced (0.8f);
        const float rad = 8.0f;

        if (coralB)      g.setColour (coral().withAlpha (down ? 0.85f : 1.0f));
        else if (on)     g.setColour (navy().withAlpha (down ? 0.85f : 1.0f));
        else             g.setColour (over ? panelHi() : panel());
        g.fillRoundedRectangle (r, rad);

        if (! on && ! coralB)
        {
            g.setColour (line());
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        const bool on     = btn.getToggleState();
        const bool coralB = btn.getProperties().getWithDefault ("accent", false);
        g.setColour ((on || coralB) ? juce::Colours::white : navy());
        g.setFont (juce::Font (13.0f, juce::Font::plain));
        g.drawText (btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, false);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.8f);
        g.setColour (panel());
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (line());
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (muted());
        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        juce::Path p; p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        g.fillPath (p);
        juce::ignoreUnused (box);
    }
};
} // namespace tonefill::plugin::ui
