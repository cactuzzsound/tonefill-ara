#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"
#include "ASShared.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace tonefill_aax
{
// Large, zoomable waveform for AAX manual selection: mirrors the VST "Expand" window but reads
// ASShared (peak/clean overlay + sourceSamples/sampleRate) and writes ASShared.manualRanges (source
// samples). No audition here (AudioSuite has Preview).
class ASWaveformWindow : public juce::DocumentWindow
{
    using LNF = tonefill::plugin::ui::ToneFillLookAndFeel;

    class View : public juce::Component, private juce::Timer
    {
    public:
        explicit View (ASShared& s) : sh_ (s) { startTimerHz (12); }

        double total() const { const juce::SpinLock::ScopedLockType l (sh_.lock); return juce::jmax (1, sh_.sourceSamples); }
        void ensureInit() { const double t = total(); if (viewLen_ <= 0.0 || viewLen_ > t) { viewStart_ = 0.0; viewLen_ = t; } viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, t - viewLen_), viewStart_); }
        void zoomH (double f, double centre = -1.0)
        {
            const double t = total();
            if (centre < 0) centre = viewStart_ + viewLen_ * 0.5;
            const double newLen = juce::jlimit (juce::jmin (t, 256.0), t, viewLen_ * f);
            const double frac = (centre - viewStart_) / juce::jmax (1.0, viewLen_);
            viewLen_ = newLen; viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, t - viewLen_), centre - frac * newLen);
            repaint();
        }
        void fit() { viewStart_ = 0.0; viewLen_ = total(); ampScale_ = juce::jmax (ampScale_, 1.0f); repaint(); }
        void clearSel() { const juce::SpinLock::ScopedLockType l (sh_.lock); sh_.manualRanges.clear(); repaint(); }

        juce::Rectangle<int> area() const { return getLocalBounds().withTrimmedBottom (20); }
        float  sampleToX (double s) const { const auto a = area(); return (float) a.getX() + (float) ((s - viewStart_) / juce::jmax (1.0, viewLen_)) * (float) a.getWidth(); }
        double xToSample (int x) const { const auto a = area(); return viewStart_ + (double) (x - a.getX()) / juce::jmax (1, a.getWidth()) * viewLen_; }

        void paint (juce::Graphics& g) override
        {
            ensureInit();
            g.fillAll (LNF::bg());
            const auto a = area();
            g.setColour (juce::Colour (0xff11150f)); g.fillRect (a);

            std::vector<float> peak; std::vector<char> clean; std::vector<std::pair<int,int>> ranges; double t, sr;
            { const juce::SpinLock::ScopedLockType l (sh_.lock); peak = sh_.peak; clean = sh_.clean; ranges = sh_.manualRanges; t = juce::jmax (1, sh_.sourceSamples); sr = sh_.sampleRate; }

            const int bins = (int) peak.size();
            const float midY = (float) a.getCentreY(), halfH = (float) a.getHeight() * 0.5f - 3.0f;
            g.setColour (LNF::line().withAlpha (0.35f)); g.fillRect ((float) a.getX(), midY - 0.5f, (float) a.getWidth(), 1.0f);

            auto selected = [&] (double s0, double s1)
            {
                for (const auto& r : ranges) if ((double) r.first < s1 && (double) r.second > s0) return true;
                if (dragging_) { const double a0 = juce::jmin (dragA_, dragB_), b0 = juce::jmax (dragA_, dragB_); if (a0 < s1 && b0 > s0) return true; }
                return false;
            };
            if (bins > 0)
                for (int x = a.getX(); x < a.getRight(); ++x)
                {
                    const double s0 = xToSample (x), s1 = xToSample (x + 1);
                    if (s1 <= 0 || s0 >= t) continue;
                    const int b0 = juce::jlimit (0, bins - 1, (int) (s0 / t * bins)), b1 = juce::jlimit (0, bins - 1, (int) (s1 / t * bins));
                    float pk = 0.0f; bool cl = false;
                    for (int b = b0; b <= b1; ++b) { pk = juce::jmax (pk, peak[(std::size_t) b]); if (b < (int) clean.size() && clean[(std::size_t) b]) cl = true; }
                    const float h = juce::jmax (1.0f, juce::jlimit (0.0f, 1.0f, std::sqrt (pk) * ampScale_ * 0.5f) * halfH);
                    g.setColour (selected (s0, s1) ? LNF::accent() : (cl ? LNF::accent().withAlpha (0.55f) : LNF::line()));
                    g.fillRect ((float) x, midY - h, 1.0f, 2.0f * h);
                }
            for (const auto& r : ranges)
            { const float x0 = sampleToX (r.first), x1 = sampleToX (r.second); g.setColour (LNF::accent().withAlpha (0.9f)); g.drawRect (juce::Rectangle<float> (x0, (float) a.getY(), juce::jmax (1.0f, x1 - x0), (float) a.getHeight()), 1.0f); }
            if (dragging_) { const float x0 = sampleToX (juce::jmin (dragA_, dragB_)), x1 = sampleToX (juce::jmax (dragA_, dragB_)); g.setColour (LNF::accent()); g.drawRect (juce::Rectangle<float> (x0, (float) a.getY(), juce::jmax (1.0f, x1 - x0), (float) a.getHeight()), 1.0f); }

            auto ruler = getLocalBounds().removeFromBottom (20);
            g.setColour (LNF::panel()); g.fillRect (ruler);
            g.setColour (LNF::muted()); g.setFont (juce::Font (10.0f));
            const double spanSec = viewLen_ / juce::jmax (1.0, sr), step = niceStep (spanSec), startSec = viewStart_ / sr;
            for (double tt = std::ceil (startSec / step) * step; tt < startSec + spanSec; tt += step)
            { const float x = sampleToX (tt * sr); g.setColour (LNF::line()); g.fillRect (x, (float) ruler.getY(), 1.0f, 5.0f); g.setColour (LNF::muted()); g.drawText (juce::String (tt, 2) + "s", (int) x + 2, ruler.getY() + 4, 70, 12, juce::Justification::centredLeft, false); }
        }

        void mouseDown (const juce::MouseEvent& e) override { if (! area().contains (e.getPosition())) return; dragging_ = true; dragA_ = dragB_ = xToSample (e.x); repaint(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (dragging_) { dragB_ = xToSample (e.x); repaint(); } }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! dragging_) return; dragging_ = false;
            const int a0 = (int) juce::jmin (dragA_, dragB_), b0 = (int) juce::jmax (dragA_, dragB_);
            const juce::SpinLock::ScopedLockType l (sh_.lock);
            if (std::abs (sampleToX (b0) - sampleToX (a0)) < 4.0f)
            { const double s = xToSample (e.x); sh_.manualRanges.erase (std::remove_if (sh_.manualRanges.begin(), sh_.manualRanges.end(), [s] (const std::pair<int,int>& r) { return s >= r.first && s <= r.second; }), sh_.manualRanges.end()); }
            else
            { sh_.manualRanges.emplace_back (a0, b0); std::sort (sh_.manualRanges.begin(), sh_.manualRanges.end()); std::vector<std::pair<int,int>> m; for (const auto& r : sh_.manualRanges) if (! m.empty() && r.first <= m.back().second) m.back().second = juce::jmax (m.back().second, r.second); else m.push_back (r); sh_.manualRanges = std::move (m); }
            repaint();
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isShiftDown()) { const auto a = area(); viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, total() - viewLen_), viewStart_ - w.deltaY * viewLen_ * 0.25); repaint(); }
            else zoomH (w.deltaY > 0 ? 1.0 / 1.25 : 1.25, xToSample (e.x));
        }

    private:
        static double niceStep (double span) { const double raw = span / 8.0, p = std::pow (10.0, std::floor (std::log10 (juce::jmax (1.0e-4, raw)))), nn = raw / p; return (nn < 1.5 ? 1.0 : nn < 3.5 ? 2.0 : nn < 7.5 ? 5.0 : 10.0) * p; }
        void timerCallback() override { repaint(); }
        ASShared& sh_;
        double viewStart_ = 0.0, viewLen_ = 0.0; float ampScale_ = 3.0f;
        bool dragging_ = false; double dragA_ = 0.0, dragB_ = 0.0;
    };

    class Content : public juce::Component
    {
    public:
        explicit Content (ASShared& s) : view_ (s)
        {
            addAndMakeVisible (view_);
            auto add = [this] (juce::TextButton& b, const juce::String& t, std::function<void()> fn) { b.setButtonText (t); b.onClick = std::move (fn); addAndMakeVisible (b); };
            add (inBtn_,  "H +",   [this] { view_.zoomH (1.0 / 1.5); });
            add (outBtn_, "H -",   [this] { view_.zoomH (1.5); });
            add (fitBtn_, "Fit",   [this] { view_.fit(); });
            add (clrBtn_, "Clear", [this] { view_.clearSel(); });
            hint_.setText ("Manual mode: drag to add a room-tone region, click one to remove. Wheel = zoom, Shift+wheel = scroll.", juce::dontSendNotification);
            hint_.setFont (juce::Font (11.5f)); hint_.setColour (juce::Label::textColourId, LNF::muted()); addAndMakeVisible (hint_);
            setSize (960, 340);
        }
        void resized() override { auto r = getLocalBounds(); auto tb = r.removeFromTop (30).reduced (6, 4); for (auto* b : { &inBtn_, &outBtn_, &fitBtn_, &clrBtn_ }) { b->setBounds (tb.removeFromLeft (54)); tb.removeFromLeft (6); } tb.removeFromLeft (8); hint_.setBounds (tb); view_.setBounds (r.reduced (6, 4)); }
        void paint (juce::Graphics& g) override { g.fillAll (LNF::bg()); }
    private:
        View view_; juce::TextButton inBtn_, outBtn_, fitBtn_, clrBtn_; juce::Label hint_;
    };

public:
    explicit ASWaveformWindow (ASShared& sh)
        : juce::DocumentWindow ("ToneFill - Waveform", LNF::bg(), juce::DocumentWindow::allButtons)
    {
        setLookAndFeel (&lnf_);
        content_ = std::make_unique<Content> (sh);
        setContentNonOwned (content_.get(), true);
        setResizable (true, true); setUsingNativeTitleBar (true);
        centreWithSize (980, 380); setVisible (true);
    }
    ~ASWaveformWindow() override { setLookAndFeel (nullptr); clearContentComponent(); }
    void closeButtonPressed() override { if (onClose) onClose(); }
    std::function<void()> onClose;

private:
    tonefill::plugin::ui::ToneFillLookAndFeel lnf_;
    std::unique_ptr<juce::Component> content_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASWaveformWindow)
};
} // namespace tonefill_aax
