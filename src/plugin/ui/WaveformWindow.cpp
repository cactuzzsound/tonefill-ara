#include "plugin/ui/WaveformWindow.h"
#include "plugin/SessionState.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace tonefill::plugin::ui
{
namespace
{
juce::String formatTC (double seconds)
{
    if (seconds < 0) seconds = 0;
    const int h = (int) (seconds / 3600.0);
    const int m = (int) (std::fmod (seconds, 3600.0) / 60.0);
    const double s = std::fmod (seconds, 60.0);
    if (h > 0) return juce::String::formatted ("%d:%02d:%05.2f", h, m, s);
    return juce::String::formatted ("%d:%05.2f", m, s);
}

// A "nice" ruler step (seconds) so there are ~6-10 labels across the view.
double niceStep (double spanSeconds)
{
    const double raw = spanSeconds / 8.0;
    const double pow10 = std::pow (10.0, std::floor (std::log10 (juce::jmax (1.0e-4, raw))));
    const double n = raw / pow10;
    const double mult = n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0;
    return mult * pow10;
}
} // namespace

//==============================================================================
// Interactive waveform view.
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (SessionState& s) : state_ (s)
    {
        startTimerHz (12);
    }

    std::function<void()> onViewChanged;

    double totalSamples() const { return juce::jmax (1, state_.sourceSamples.load()); }
    double viewStart()    const { return viewStart_; }
    double viewLen()      const { return viewLen_; }

    void ensureInit()
    {
        const double total = totalSamples();
        if (viewLen_ <= 0.0 || viewLen_ > total) { viewStart_ = 0.0; viewLen_ = total; }
        viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, total - viewLen_), viewStart_);
    }

    void setViewStart (double s)
    {
        viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, totalSamples() - viewLen_), s);
        repaint();
    }

    void zoomH (double factor, double centerSample = -1.0)
    {
        const double total = totalSamples();
        if (centerSample < 0) centerSample = viewStart_ + viewLen_ * 0.5;
        const double newLen = juce::jlimit (juce::jmin (total, 256.0), total, viewLen_ * factor);
        const double frac = (centerSample - viewStart_) / juce::jmax (1.0, viewLen_);
        viewLen_ = newLen;
        viewStart_ = juce::jlimit (0.0, juce::jmax (0.0, total - viewLen_), centerSample - frac * newLen);
        if (onViewChanged) onViewChanged();
        repaint();
    }

    void zoomV (double factor) { ampScale_ = juce::jlimit (0.5f, 40.0f, ampScale_ * (float) factor); repaint(); }
    void fit()  { viewStart_ = 0.0; viewLen_ = totalSamples(); ampScale_ = juce::jmax (ampScale_, 1.0f); if (onViewChanged) onViewChanged(); repaint(); }
    void clearSelection() { state_.setManualRanges ({}); repaint(); }

    //== drawing ===============================================================
    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedBottom (rulerH_); }

    float sampleToX (double sample) const
    {
        const auto a = waveArea();
        return (float) a.getX() + (float) ((sample - viewStart_) / juce::jmax (1.0, viewLen_)) * (float) a.getWidth();
    }
    double xToSample (int x) const
    {
        const auto a = waveArea();
        return viewStart_ + (double) (x - a.getX()) / juce::jmax (1, a.getWidth()) * viewLen_;
    }

    void paint (juce::Graphics& g) override
    {
        ensureInit();
        g.fillAll (ToneFillLookAndFeel::bg());
        const auto a = waveArea();
        g.setColour (juce::Colour (0xff11150f));
        g.fillRect (a);

        const auto wave = state_.getWave();
        const int bins = (int) wave.peak.size();
        const double total = totalSamples();
        const float midY = (float) a.getCentreY();
        const float halfH = (float) a.getHeight() * 0.5f - 3.0f;

        // centre line
        g.setColour (ToneFillLookAndFeel::line().withAlpha (0.35f));
        g.fillRect ((float) a.getX(), midY - 0.5f, (float) a.getWidth(), 1.0f);

        // selections (source samples) -> highlight bins that fall inside
        auto ranges = state_.getManualRanges();
        auto selected = [&] (double s0, double s1)
        {
            for (const auto& r : ranges) if ((double) r.first < s1 && (double) r.second > s0) return true;
            if (dragging_)
            {
                const double a0 = juce::jmin (dragA_, dragB_), b0 = juce::jmax (dragA_, dragB_);
                if (a0 < s1 && b0 > s0) return true;
            }
            return false;
        };

        if (bins > 0)
        {
            for (int x = a.getX(); x < a.getRight(); ++x)
            {
                const double s0 = xToSample (x), s1 = xToSample (x + 1);
                if (s1 <= 0 || s0 >= total) continue;
                const int b0 = juce::jlimit (0, bins - 1, (int) (s0 / total * bins));
                const int b1 = juce::jlimit (0, bins - 1, (int) (s1 / total * bins));
                float pk = 0.0f;
                for (int b = b0; b <= b1; ++b) pk = juce::jmax (pk, wave.peak[(std::size_t) b]);
                const float amp = juce::jlimit (0.0f, 1.0f, std::sqrt (pk) * ampScale_ * 0.5f);
                const float h = juce::jmax (1.0f, amp * halfH);
                const bool sel = selected (s0, s1);
                g.setColour (sel ? ToneFillLookAndFeel::accent() : ToneFillLookAndFeel::line());
                g.fillRect ((float) x, midY - h, 1.0f, 2.0f * h);
            }
        }

        // selection edges
        for (const auto& r : ranges)
        {
            const float x0 = sampleToX (r.first), x1 = sampleToX (r.second);
            g.setColour (ToneFillLookAndFeel::accent().withAlpha (0.9f));
            g.drawRect (juce::Rectangle<float> (x0, (float) a.getY(), juce::jmax (1.0f, x1 - x0), (float) a.getHeight()), 1.0f);
        }
        if (dragging_)
        {
            const float x0 = sampleToX (juce::jmin (dragA_, dragB_)), x1 = sampleToX (juce::jmax (dragA_, dragB_));
            g.setColour (ToneFillLookAndFeel::accent());
            g.drawRect (juce::Rectangle<float> (x0, (float) a.getY(), juce::jmax (1.0f, x1 - x0), (float) a.getHeight()), 1.0f);
        }

        // timecode ruler
        const double sr = state_.sourceSampleRate.load();
        auto ruler = getLocalBounds().removeFromBottom (rulerH_);
        g.setColour (ToneFillLookAndFeel::panel());
        g.fillRect (ruler);
        g.setColour (ToneFillLookAndFeel::muted());
        g.setFont (juce::Font (10.0f));
        const double spanSec = viewLen_ / juce::jmax (1.0, sr);
        const double step = niceStep (spanSec);
        const double startSec = viewStart_ / sr;
        const double firstTick = std::ceil (startSec / step) * step;
        for (double t = firstTick; t < startSec + spanSec; t += step)
        {
            const float x = sampleToX (t * sr);
            g.setColour (ToneFillLookAndFeel::line());
            g.fillRect (x, (float) ruler.getY(), 1.0f, 5.0f);
            g.setColour (ToneFillLookAndFeel::muted());
            g.drawText (formatTC (t), (int) x + 2, ruler.getY() + 4, 90, 12, juce::Justification::centredLeft, false);
        }
    }

    //== interaction ==========================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! waveArea().contains (e.getPosition())) return;
        dragging_ = true;
        dragA_ = dragB_ = xToSample (e.x);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging_) return;
        dragB_ = xToSample (e.x);
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging_) return;
        dragging_ = false;
        const int a0 = (int) juce::jmin (dragA_, dragB_);
        const int b0 = (int) juce::jmax (dragA_, dragB_);
        auto ranges = state_.getManualRanges();
        if (std::abs (sampleToX (b0) - sampleToX (a0)) < 4.0f)
        {
            // click: remove a range under the cursor
            const double s = xToSample (e.x);
            const auto before = ranges.size();
            ranges.erase (std::remove_if (ranges.begin(), ranges.end(),
                          [s] (const std::pair<int, int>& r) { return s >= r.first && s <= r.second; }), ranges.end());
            if (ranges.size() != before) state_.setManualRanges (std::move (ranges));
        }
        else
        {
            ranges.emplace_back (a0, b0);
            std::sort (ranges.begin(), ranges.end());
            std::vector<std::pair<int, int>> merged;
            for (const auto& r : ranges)
                if (! merged.empty() && r.first <= merged.back().second) merged.back().second = juce::jmax (merged.back().second, r.second);
                else merged.push_back (r);
            state_.setManualRanges (std::move (merged));
        }
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isAltDown() || e.mods.isCommandDown())      zoomV (w.deltaY > 0 ? 1.2 : 1.0 / 1.2);
        else if (e.mods.isShiftDown())                          setViewStart (viewStart_ - w.deltaY * viewLen_ * 0.25),
                                                                (onViewChanged ? onViewChanged() : void());
        else                                                    zoomH (w.deltaY > 0 ? 1.0 / 1.25 : 1.25, xToSample (e.x));
    }

private:
    void timerCallback() override { repaint(); }

    SessionState& state_;
    double viewStart_ = 0.0, viewLen_ = 0.0;
    float  ampScale_ = 3.0f;
    int    rulerH_ = 20;
    bool   dragging_ = false;
    double dragA_ = 0.0, dragB_ = 0.0;
};

//==============================================================================
// Content: toolbar + view + horizontal scrollbar.
class WaveformContent : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit WaveformContent (SessionState& s) : view_ (s), hScroll_ (false)
    {
        addAndMakeVisible (view_);
        addAndMakeVisible (hScroll_);
        hScroll_.addListener (this);

        auto add = [this] (juce::TextButton& b, const juce::String& t) { b.setButtonText (t); addAndMakeVisible (b); };
        add (zoomInH_,  "H +");  add (zoomOutH_, "H -");
        add (zoomInV_,  "V +");  add (zoomOutV_, "V -");
        add (fitBtn_,   "Fit");  add (clearBtn_, "Clear");

        zoomInH_.onClick  = [this] { view_.zoomH (1.0 / 1.5); syncScroll(); };
        zoomOutH_.onClick = [this] { view_.zoomH (1.5);        syncScroll(); };
        zoomInV_.onClick  = [this] { view_.zoomV (1.4); };
        zoomOutV_.onClick = [this] { view_.zoomV (1.0 / 1.4); };
        fitBtn_.onClick   = [this] { view_.fit(); syncScroll(); };
        clearBtn_.onClick = [this] { view_.clearSelection(); };

        view_.onViewChanged = [this] { syncScroll(); };
        setSize (900, 340);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto tb = r.removeFromTop (30).reduced (6, 4);
        for (auto* b : { &zoomInH_, &zoomOutH_, &zoomInV_, &zoomOutV_, &fitBtn_, &clearBtn_ })
        { b->setBounds (tb.removeFromLeft (54)); tb.removeFromLeft (6); }
        hScroll_.setBounds (r.removeFromBottom (14));
        view_.setBounds (r);
        syncScroll();
    }

    void paint (juce::Graphics& g) override { g.fillAll (ToneFillLookAndFeel::bg()); }

private:
    void syncScroll()
    {
        const double total = view_.totalSamples();
        hScroll_.setRangeLimits (0.0, total, juce::dontSendNotification);
        hScroll_.setCurrentRange (view_.viewStart(), juce::jmin (view_.viewLen(), total), juce::dontSendNotification);
    }
    void scrollBarMoved (juce::ScrollBar*, double newStart) override { view_.setViewStart (newStart); }

    WaveformView view_;
    juce::ScrollBar hScroll_;
    juce::TextButton zoomInH_, zoomOutH_, zoomInV_, zoomOutV_, fitBtn_, clearBtn_;
};

//==============================================================================
WaveformWindow::WaveformWindow (SessionState& state)
    : juce::DocumentWindow ("ToneFill - Waveform", ToneFillLookAndFeel::bg(), juce::DocumentWindow::allButtons)
{
    setLookAndFeel (&lnf_);
    content_ = std::make_unique<WaveformContent> (state);
    setContentNonOwned (content_.get(), true);
    setResizable (true, true);
    setUsingNativeTitleBar (true);
    centreWithSize (960, 380);
    setVisible (true);
}

WaveformWindow::~WaveformWindow()
{
    setLookAndFeel (nullptr);
    clearContentComponent();
}

void WaveformWindow::closeButtonPressed()
{
    if (onClose) onClose();
}
} // namespace tonefill::plugin::ui
