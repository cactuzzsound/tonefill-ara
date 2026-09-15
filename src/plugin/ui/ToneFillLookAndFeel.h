#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace tonefill::plugin::ui
{
// Soft studio theme. Palette: Alice Blue page, white cards with a soft drop shadow, Pale Sky
// tracks/borders, Coffee Bean ink (text + knob body + selected chips), Carrot Orange accent
// (Detection/Output arcs, Auto + Export), Cool Sky blue (Structure/Texture arcs).
class ToneFillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Line-art glyphs drawn on buttons (set via getProperties().set("icon", (int) IcXxx)).
    enum Icon { IcNone = 0, IcSparkle, IcWave, IcPower, IcRefresh, IcDownload, IcMinus, IcPlus, IcExpand, IcBulb };

    static juce::Colour bg()       { return juce::Colour (0xffe8eef2); } // Alice Blue page
    static juce::Colour panel()    { return juce::Colour (0xffffffff); } // card fill
    static juce::Colour panelHi()  { return juce::Colour (0xffeef3f7); } // knob face / hover / inset
    static juce::Colour line()     { return juce::Colour (0xffc7d3dd); } // Pale Sky: knob track
    static juce::Colour lineSoft() { return juce::Colour (0xffdde6ec); } // faint dividers / card border
    static juce::Colour navy()     { return juce::Colour (0xff1f1300); } // Coffee Bean: ink + knob body
    static juce::Colour accent()   { return juce::Colour (0xfffb9f37); } // Carrot Orange
    static juce::Colour purple()   { return juce::Colour (0xff77b6ea); } // Cool Sky (Structure/Texture)
    static juce::Colour coral()    { return juce::Colour (0xfffb9f37); } // export = Carrot Orange
    static juce::Colour good()     { return juce::Colour (0xff3caa72); } // ready dot
    static juce::Colour text()     { return juce::Colour (0xff1f1300); } // ink text
    static juce::Colour muted()    { return juce::Colour (0xff9aa6b2); } // captions
    static juce::Colour label()    { return juce::Colour (0xff7c8794); } // knob labels
    static juce::Colour shadow()   { return juce::Colour (0x22101826); } // soft card/knob shadow

    // Soft neumorphic drop shadow under a rounded card. Cheap layered fill (no image blur).
    static void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colour (0x14101826).withAlpha (0.05f * (float) i));
            g.fillRoundedRectangle (r.translated (0.0f, (float) i * 1.6f).expanded ((float) i * 0.8f), radius + (float) i);
        }
    }

    // Crop an image to the bounding box of its non-transparent pixels (so a logo element centred in a
    // square canvas draws tight in a header slot). Run once and cache — it scans every pixel.
    static juce::Image alphaTrim (const juce::Image& src)
    {
        if (! src.isValid()) return src;
        const int w = src.getWidth(), h = src.getHeight();
        juce::Image::BitmapData bd (src, juce::Image::BitmapData::readOnly);
        int minX = w, minY = h, maxX = -1, maxY = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (bd.getPixelColour (x, y).getAlpha() > 8)
                { minX = juce::jmin (minX, x); minY = juce::jmin (minY, y); maxX = juce::jmax (maxX, x); maxY = juce::jmax (maxY, y); }
        if (maxX < minX) return src;
        return src.getClippedImage ({ minX, minY, maxX - minX + 1, maxY - minY + 1 });
    }

    // Small line-art icon inside box b, in colour col.
    static void drawIcon (juce::Graphics& g, juce::Rectangle<float> b, int id, juce::Colour col)
    {
        g.setColour (col);
        const float cx = b.getCentreX(), cy = b.getCentreY(), w = b.getWidth(), h = b.getHeight(), sw = 1.6f;
        switch (id)
        {
            case IcSparkle:
            {
                auto star = [&] (float scx, float scy, float r)
                {
                    const float ir = r * 0.32f;
                    juce::Path p;
                    p.startNewSubPath (scx, scy - r);
                    p.lineTo (scx + ir, scy - ir); p.lineTo (scx + r, scy);
                    p.lineTo (scx + ir, scy + ir); p.lineTo (scx, scy + r);
                    p.lineTo (scx - ir, scy + ir); p.lineTo (scx - r, scy);
                    p.lineTo (scx - ir, scy - ir); p.closeSubPath();
                    g.fillPath (p);
                };
                star (cx - w * 0.10f, cy + h * 0.08f, w * 0.30f);
                star (cx + w * 0.26f, cy - h * 0.22f, w * 0.15f);
                break;
            }
            case IcWave:
            {
                const float hs[7] = { 0.30f, 0.62f, 0.96f, 0.50f, 0.82f, 0.40f, 0.26f };
                const float bw = w / 13.0f;
                for (int i = 0; i < 7; ++i)
                {
                    const float bh = h * hs[i];
                    g.fillRoundedRectangle (b.getX() + (float) i * 2.0f * bw, cy - bh * 0.5f, bw, bh, bw * 0.45f);
                }
                break;
            }
            case IcPower:
            {
                const float r = juce::jmin (w, h) * 0.36f;
                juce::Path ring;
                ring.addCentredArc (cx, cy, r, r, 0.0f, 0.85f, juce::MathConstants<float>::twoPi - 0.85f, true);
                g.strokePath (ring, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.drawLine (cx, cy - r * 1.05f, cx, cy - r * 0.10f, sw);
                break;
            }
            case IcRefresh:
            {
                const float r = juce::jmin (w, h) * 0.34f;
                const float a1 = 0.7f, a2 = juce::MathConstants<float>::twoPi - 0.05f;
                juce::Path arc;
                arc.addCentredArc (cx, cy, r, r, 0.0f, a1, a2, true);
                g.strokePath (arc, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float ex = cx + r * std::sin (a2), ey = cy - r * std::cos (a2);
                juce::Path head;
                head.addTriangle (ex - 3.4f, ey, ex + 2.4f, ey - 3.4f, ex + 2.8f, ey + 3.0f);
                g.fillPath (head);
                break;
            }
            case IcDownload:
            {
                g.drawLine (cx, cy - h * 0.34f, cx, cy + h * 0.08f, sw);
                juce::Path head; head.addTriangle (cx - w * 0.17f, cy - h * 0.04f, cx + w * 0.17f, cy - h * 0.04f, cx, cy + h * 0.22f);
                g.fillPath (head);
                g.drawLine (cx - w * 0.26f, cy + h * 0.32f, cx + w * 0.26f, cy + h * 0.32f, sw);
                g.drawLine (cx - w * 0.26f, cy + h * 0.20f, cx - w * 0.26f, cy + h * 0.32f, sw);
                g.drawLine (cx + w * 0.26f, cy + h * 0.20f, cx + w * 0.26f, cy + h * 0.32f, sw);
                break;
            }
            case IcMinus:
                g.drawLine (cx - w * 0.22f, cy, cx + w * 0.22f, cy, 2.0f);
                break;
            case IcPlus:
                g.drawLine (cx - w * 0.22f, cy, cx + w * 0.22f, cy, 2.0f);
                g.drawLine (cx, cy - h * 0.22f, cx, cy + h * 0.22f, 2.0f);
                break;
            case IcExpand:
            {
                const float x0 = cx - w * 0.24f, y0 = cy + h * 0.24f, x1 = cx + w * 0.24f, y1 = cy - h * 0.24f;
                g.drawLine (x0, y0, x1, y1, sw);
                g.drawLine (x1, y1, x1 - w * 0.20f, y1, sw); g.drawLine (x1, y1, x1, y1 + h * 0.20f, sw);
                g.drawLine (x0, y0, x0 + w * 0.20f, y0, sw); g.drawLine (x0, y0, x0, y0 - h * 0.20f, sw);
                break;
            }
            case IcBulb:
            {
                const float r = juce::jmin (w, h) * 0.30f;
                g.drawEllipse (cx - r, cy - r - h * 0.08f, r * 2.0f, r * 2.0f, sw);
                g.drawLine (cx - r * 0.5f, cy + r * 0.95f, cx + r * 0.5f, cy + r * 0.95f, sw);
                g.drawLine (cx - r * 0.4f, cy + r * 1.35f, cx + r * 0.4f, cy + r * 1.35f, sw);
                break;
            }
            default: break;
        }
    }

    ToneFillLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, navy());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, accent());
        setColour (juce::Label::textColourId, navy());
        setColour (juce::ComboBox::backgroundColourId, panel());
        setColour (juce::ComboBox::textColourId, navy());
        setColour (juce::ComboBox::outlineColourId, lineSoft());
        setColour (juce::ComboBox::arrowColourId, muted());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, navy());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, purple().withAlpha (0.22f));
        setColour (juce::TextEditor::backgroundColourId, panel());
        setColour (juce::TextEditor::textColourId, navy());
        setColour (juce::CaretComponent::caretColourId, accent());
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
        g.setColour (line());
        g.strokePath (track, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc: gradient ALONG the arc from the start (low) to the current value (high). Same hue
        // throughout (no strong recolour) -- only saturation rises low->high and brightness eases
        // gently lighter->darker, so the filled part reads as "more" as it grows.
        if (angle > startAngle + 1.0e-3f)
        {
            const float bh = arcCol.getHue(), bs = arcCol.getSaturation(), bb = arcCol.getBrightness();
            const auto loCol = juce::Colour::fromHSV (bh, bs * 0.50f, juce::jmin (1.0f, bb * 1.10f), 1.0f);
            const auto hiCol = juce::Colour::fromHSV (bh, juce::jmin (1.0f, bs * 1.02f), bb * 0.90f, 1.0f);
            const int NSEG = 32;
            for (int i = 0; i < NSEG; ++i)
            {
                const float t0 = (float) i / (float) NSEG, t1 = (float) (i + 1) / (float) NSEG;
                const float s0 = startAngle + (angle - startAngle) * t0;
                const float s1 = startAngle + (angle - startAngle) * t1;
                juce::Path seg; seg.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, s0, s1, true);
                g.setColour (loCol.interpolatedWith (hiCol, (t0 + t1) * 0.5f));
                g.strokePath (seg, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            }
            // Round off the leading tip so the value end isn't a flat edge.
            const auto tipC = c.getPointOnCircumference (arcR, angle);
            g.setColour (hiCol);
            g.fillEllipse (tipC.x - 2.75f, tipC.y - 2.75f, 5.5f, 5.5f);
        }

        const float rBody = d * 0.36f;

        // Soft raised shadow under the knob body.
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colour (0x14101826).withAlpha (0.06f * (float) i));
            const float rr = rBody + (float) i * 0.7f;
            g.fillEllipse (c.x - rr, c.y - rr + (float) i * 1.1f, rr * 2.0f, rr * 2.0f);
        }

        // Dark body: a light->dark gradient anchored to the indicator, so the sheen turns with the
        // knob (bright toward the white pointer, dark opposite). Plus a specular hotspot near the
        // pointer and a thin dark rim for depth and freshness.
        const auto pLite = c.getPointOnCircumference (rBody * 1.15f, angle);
        const auto pDark = c.getPointOnCircumference (rBody * 1.15f, angle + juce::MathConstants<float>::pi);
        juce::ColourGradient bodyGrad (navy().brighter (0.62f), pLite.x, pLite.y,
                                       navy().darker (0.52f),   pDark.x, pDark.y, false);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (c.x - rBody, c.y - rBody, rBody * 2.0f, rBody * 2.0f);
        const auto spec = c.getPointOnCircumference (rBody * 0.46f, angle);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillEllipse (spec.x - rBody * 0.45f, spec.y - rBody * 0.45f, rBody * 0.9f, rBody * 0.9f);
        g.setColour (juce::Colours::black.withAlpha (0.20f));
        g.drawEllipse (c.x - rBody, c.y - rBody, rBody * 2.0f, rBody * 2.0f, 1.0f);

        // Light indicator line on the dark body.
        const auto tip  = c.getPointOnCircumference (rBody - 4.0f, angle);
        const auto root = c.getPointOnCircumference (rBody * 0.42f, angle);
        g.setColour (juce::Colour (0xffe8eef2));
        g.drawLine ({ root, tip }, 2.6f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                               bool over, bool down) override
    {
        const bool on      = btn.getToggleState();
        const bool isAccent = btn.getProperties().getWithDefault ("accent", false); // orange (Export)
        const bool primary  = btn.getProperties().getWithDefault ("primary", false); // orange-when-on (Auto/Manual)
        auto r = btn.getLocalBounds().toFloat().reduced (0.8f);
        const float rad = juce::jmin (r.getHeight() * 0.5f, 16.0f);

        if (isAccent || (on && primary)) g.setColour (accent().withAlpha (down ? 0.85f : 1.0f));
        else if (on)                     g.setColour (navy().withAlpha (down ? 0.85f : 1.0f));
        else                             g.setColour (over ? panelHi() : panel());
        g.fillRoundedRectangle (r, rad);

        if (! on && ! isAccent)
        {
            g.setColour (lineSoft());
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        const bool on       = btn.getToggleState();
        const bool isAccent = btn.getProperties().getWithDefault ("accent", false);
        const int  icon     = (int) btn.getProperties().getWithDefault ("icon", 0);
        const auto fg = (on || isAccent) ? juce::Colours::white : navy();
        const auto b  = btn.getLocalBounds();
        const juce::String txt = btn.getButtonText();
        const juce::Font   font (13.0f, juce::Font::plain);
        g.setColour (fg);
        g.setFont (font);

        if (icon == IcNone)
        {
            g.drawText (txt, b, juce::Justification::centred, false);
            return;
        }
        const float isz = 16.0f;
        if (txt.isEmpty()) // icon-only button (e.g. the +/- steppers)
        {
            drawIcon (g, b.toFloat().withSizeKeepingCentre (isz, isz), icon, fg);
            return;
        }
        const float gap = 8.0f;
        const int   tw  = font.getStringWidth (txt);
        const float total = isz + gap + (float) tw;
        const float x = (float) b.getCentreX() - total * 0.5f;
        drawIcon (g, { x, (float) b.getCentreY() - isz * 0.5f, isz, isz }, icon, fg);
        g.setColour (fg);
        g.drawText (txt, juce::Rectangle<float> { x + isz + gap, (float) b.getY(), (float) tw + 4.0f, (float) b.getHeight() },
                    juce::Justification::centredLeft, false);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.8f);
        g.setColour (panel());
        g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 14.0f));
        g.setColour (lineSoft());
        g.drawRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 14.0f), 1.0f);
        g.setColour (muted());
        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        juce::Path p; p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        g.fillPath (p);
        juce::ignoreUnused (box);
    }
};
} // namespace tonefill::plugin::ui
