#include "plugin/ui/SpectralEditorWindow.h"
#include "plugin/SessionState.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tonefill::plugin::ui
{
using LNF = ToneFillLookAndFeel;

namespace
{
// RX / inferno-style colour map: 0 = background-dark, 1 = hot yellow-white.
juce::Colour heat (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    struct P { float t; juce::uint8 r, g, b; };
    static const P stops[] = {
        { 0.00f,  10,  12,  20 }, { 0.20f,  30,  20,  80 }, { 0.40f, 110,  30, 130 },
        { 0.60f, 200,  50,  90 }, { 0.78f, 240, 130,  40 }, { 0.90f, 250, 200,  70 },
        { 1.00f, 255, 245, 200 }
    };
    for (int i = 1; i < (int) (sizeof (stops) / sizeof (P)); ++i)
        if (t <= stops[i].t)
        {
            const auto& a = stops[i - 1]; const auto& b = stops[i];
            const float f = (t - a.t) / juce::jmax (1.0e-6f, b.t - a.t);
            return juce::Colour ((juce::uint8) (a.r + f * (b.r - a.r)),
                                 (juce::uint8) (a.g + f * (b.g - a.g)),
                                 (juce::uint8) (a.b + f * (b.b - a.b)));
        }
    return juce::Colour (255, 245, 200);
}

juce::String freqLabel (double hz)
{
    if (hz >= 1000.0) return juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k";
    return juce::String ((int) std::lround (hz));
}
} // namespace

//==============================================================================
class SpectralView : public juce::Component, private juce::Timer
{
public:
    explicit SpectralView (SessionState& s) : state_ (s) { startTimerHz (8); }

    void fit()
    {
        fLo_ = 20.0; fHi_ = juce::jmax (2000.0, nyquist_); tLo_ = 0.0; tHi_ = 1.0;
        displayDirty_ = true; repaint();
    }

    // Zoom (physical buttons). factor < 1 = zoom in, > 1 = zoom out; around the current centre.
    void zoomFreq (double factor)
    {
        const double cf = std::sqrt (fLo_ * fHi_); // geometric centre
        double ratio = juce::jlimit (2.0, 3000.0, std::pow (fHi_ / fLo_, factor));
        double lo = juce::jmax (15.0, cf / std::sqrt (ratio));
        double hi = juce::jmin (nyquist_, cf * std::sqrt (ratio));
        if (hi / lo > 1.3) { fLo_ = lo; fHi_ = hi; displayDirty_ = true; repaint(); }
    }
    void zoomTime (double factor)
    {
        const double c = (tLo_ + tHi_) * 0.5;
        const double span = juce::jlimit (0.01, 1.0, (tHi_ - tLo_) * factor);
        tLo_ = juce::jlimit (0.0, 1.0 - span, c - span * 0.5);
        tHi_ = tLo_ + span; displayDirty_ = true; repaint();
    }
    // Pan. panFreq: +octaves moves the view up; panTime: +frac moves it right.
    void panFreq (double octaves)
    {
        const double f = std::pow (2.0, octaves);
        double lo = fLo_ * f, hi = fHi_ * f;
        if (hi > nyquist_) { const double k = nyquist_ / hi; lo *= k; hi *= k; }
        if (lo < 15.0)      { const double k = 15.0 / lo;     lo *= k; hi *= k; }
        fLo_ = lo; fHi_ = juce::jmin (nyquist_, hi); displayDirty_ = true; repaint();
    }
    void panTime (double frac)
    {
        const double span = tHi_ - tLo_;
        tLo_ = juce::jlimit (0.0, 1.0 - span, tLo_ + frac * span);
        tHi_ = tLo_ + span; displayDirty_ = true; repaint();
    }

    //== spectrogram build =====================================================
    void rebuildSpectrogram()
    {
        double sr = 48000.0;
        auto src = state_.getSourcePreview (sr);
        srcGen_ = state_.sourceSamples.load();
        grid_.clear(); numFrames_ = 0; numBins_ = 0;
        if (src == nullptr || src->empty() || (*src)[0].empty()) { displayDirty_ = true; return; }

        const int nCh = (int) src->size();
        const int n = (int) (*src)[0].size();
        nyquist_ = sr * 0.5; sr_ = sr;

        const int order = 10, fftSize = 1 << order;              // 1024
        const int hop = juce::jmax (fftSize / 2, n / 4000 + 1);  // cap ~4000 frames
        numBins_ = fftSize / 2 + 1;
        binHz_   = sr / fftSize;
        const int frames = juce::jmax (1, (n - fftSize) / hop + 1);

        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> win (fftSize, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> buf ((std::size_t) fftSize * 2);
        grid_.assign ((std::size_t) frames * numBins_, 0.0f);

        float maxDb = -200.0f;
        for (int f = 0; f < frames; ++f)
        {
            const int pos = f * hop;
            for (int i = 0; i < fftSize; ++i)
            {
                float v = 0.0f;
                const int s = pos + i;
                if (s < n) for (int c = 0; c < nCh; ++c) v += (*src)[(std::size_t) c][(std::size_t) s];
                buf[(std::size_t) i] = nCh > 1 ? v / (float) nCh : v;
            }
            std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
            win.multiplyWithWindowingTable (buf.data(), (std::size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int b = 0; b < numBins_; ++b)
            {
                const float db = 20.0f * std::log10 (buf[(std::size_t) b] + 1.0e-9f);
                grid_[(std::size_t) f * numBins_ + b] = db;
                maxDb = juce::jmax (maxDb, db);
            }
        }
        // Normalise to [max-80 dB .. max] for good contrast at any level.
        const float lo = maxDb - 80.0f, span = 80.0f;
        for (auto& v : grid_) v = juce::jlimit (0.0f, 1.0f, (v - lo) / span);
        numFrames_ = frames;
        displayDirty_ = true;
    }

    //== freq <-> y (log) ======================================================
    juce::Rectangle<int> plot() const { return getLocalBounds().withTrimmedLeft (46).withTrimmedBottom (2); }
    float  freqToY (double hz) const
    {
        const auto a = plot();
        const double frac = std::log (juce::jmax (1.0, hz) / fLo_) / std::log (fHi_ / fLo_);
        return (float) a.getBottom() - (float) juce::jlimit (0.0, 1.0, frac) * (float) a.getHeight();
    }
    double yToFreq (float y) const
    {
        const auto a = plot();
        const double frac = juce::jlimit (0.0, 1.0, (double) (a.getBottom() - y) / juce::jmax (1, a.getHeight()));
        return fLo_ * std::pow (fHi_ / fLo_, frac);
    }

    void rebuildDisplay()
    {
        displayDirty_ = false;
        const auto a = plot();
        if (a.getWidth() <= 0 || a.getHeight() <= 0 || numFrames_ == 0) { display_ = juce::Image(); return; }

        display_ = juce::Image (juce::Image::RGB, a.getWidth(), a.getHeight(), false);
        juce::Image::BitmapData bmp (display_, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < a.getHeight(); ++py)
        {
            const double hz  = yToFreq ((float) (a.getY() + py));
            const double binf = hz / binHz_;
            const int    b0  = juce::jlimit (0, numBins_ - 1, (int) binf);
            for (int px = 0; px < a.getWidth(); ++px)
            {
                const double tf = tLo_ + (double) px / juce::jmax (1, a.getWidth()) * (tHi_ - tLo_);
                const int f0 = juce::jlimit (0, numFrames_ - 1, (int) (tf * (numFrames_ - 1)));
                const float v = grid_[(std::size_t) f0 * numBins_ + b0];
                const auto c = heat (v);
                bmp.setPixelColour (px, py, c);
            }
        }
    }

    //== edges =================================================================
    int bandCount() const { return juce::jlimit (3, 12, state_.spectralBands.load()); }

    void syncEdges()
    {
        const int need = bandCount() - 1;
        auto ue = state_.getSpectralEdges();
        if ((int) ue.size() == need) { edges_ = ue; return; }
        // (re)initialise to a geometric 150..8000 spread for the current count.
        edges_.assign ((std::size_t) need, 0.0f);
        const double lo = 150.0, hi = 8000.0;
        for (int e = 0; e < need; ++e)
        { const double t = need > 1 ? (double) e / (double) (need - 1) : 0.0; edges_[(std::size_t) e] = (float) (lo * std::pow (hi / lo, t)); }
        commitEdges();
    }

    void commitEdges()
    {
        std::sort (edges_.begin(), edges_.end());
        state_.setSpectralEdges (edges_);
        state_.generation.fetch_add (1); // trigger a re-render with the new edges
    }

    //== drawing ===============================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (LNF::bg());
        const auto a = plot();
        if (displayDirty_) rebuildDisplay();
        if (display_.isValid()) g.drawImageAt (display_, a.getX(), a.getY());
        else { g.setColour (LNF::muted()); g.drawText ("Analysing... open a clip and let the analysis run", a, juce::Justification::centred, false); }

        // frequency ruler (log): label a few decade-ish frequencies inside the view.
        g.setFont (juce::Font (10.0f));
        static const double marks[] = { 30, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000 };
        for (double hz : marks)
        {
            if (hz < fLo_ || hz > fHi_) continue;
            const float y = freqToY (hz);
            g.setColour (LNF::line().withAlpha (0.25f));
            g.fillRect ((float) a.getX(), y, (float) a.getWidth(), 1.0f);
            g.setColour (LNF::muted());
            g.drawText (freqLabel (hz), 4, (int) y - 6, 40, 12, juce::Justification::centredRight, false);
        }

        // band edges (draggable), with the band index between them.
        for (int i = 0; i < (int) edges_.size(); ++i)
        {
            const float y = freqToY (edges_[(std::size_t) i]);
            const bool hot = (i == dragEdge_);
            g.setColour (hot ? LNF::accent() : juce::Colours::white.withAlpha (0.85f));
            g.fillRect ((float) a.getX(), y - (hot ? 1.5f : 0.5f), (float) a.getWidth(), hot ? 3.0f : 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle ((float) a.getRight() - 66.0f, y - 8.0f, 60.0f, 16.0f, 4.0f);
            g.setColour (juce::Colours::white);
            g.drawText (freqLabel (edges_[(std::size_t) i]) + " Hz", (int) a.getRight() - 64, (int) y - 8, 56, 16, juce::Justification::centredLeft, false);
        }
    }

    //== interaction ===========================================================
    int edgeAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) edges_.size(); ++i)
            if (std::abs (freqToY (edges_[(std::size_t) i]) - (float) p.y) < 6.0f) return i;
        return -1;
    }
    void mouseMove (const juce::MouseEvent& e) override
    { setMouseCursor (edgeAt (e.getPosition()) >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragEdge_ = edgeAt (e.getPosition());
        panLast_ = e.getPosition();
        if (dragEdge_ < 0) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragEdge_ >= 0)
        {
            const double hz = yToFreq ((float) e.y);
            const double lo = dragEdge_ > 0 ? edges_[(std::size_t) dragEdge_ - 1] + 1.0 : 20.0;
            const double hi = dragEdge_ + 1 < (int) edges_.size() ? edges_[(std::size_t) dragEdge_ + 1] - 1.0 : nyquist_ - 1.0;
            edges_[(std::size_t) dragEdge_] = (float) juce::jlimit (lo, hi, hz);
            repaint();
            return;
        }
        // Grab-pan the canvas: the content follows the cursor.
        const auto a = plot();
        const int dx = e.x - panLast_.x, dy = e.y - panLast_.y;
        panLast_ = e.getPosition();
        const double octPerPx = std::log2 (fHi_ / fLo_) / (double) juce::jmax (1, a.getHeight());
        panFreq (-dy * octPerPx); // drag down -> view moves to lower freqs (content follows)
        const double fracPerPx = (tHi_ - tLo_) / (double) juce::jmax (1, a.getWidth());
        panTime (-dx * fracPerPx); // drag right -> view moves earlier (content follows)
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (dragEdge_ >= 0) { dragEdge_ = -1; commitEdges(); repaint(); }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        // Main wheel = pan up/down (frequency); side wheel = pan left/right (time). No zoom on wheel.
        if (std::abs (w.deltaX) > std::abs (w.deltaY)) panTime (-w.deltaX * 0.35);
        else                                          panFreq (w.deltaY * 0.5);
    }

    void resized() override { displayDirty_ = true; }

private:
    void timerCallback() override
    {
        if (state_.sourceSamples.load() != srcGen_) rebuildSpectrogram();
        if ((int) edges_.size() != bandCount() - 1) syncEdges();
        // adopt fit range once the spectrogram exists
        if (numFrames_ > 0 && fHi_ <= 20.0) fit();
    }

    SessionState& state_;
    std::vector<float> grid_; int numFrames_ = 0, numBins_ = 0; double binHz_ = 46.875, sr_ = 48000.0, nyquist_ = 24000.0;
    int srcGen_ = -1;
    juce::Image display_; bool displayDirty_ = true;
    double fLo_ = 20.0, fHi_ = 20.0, tLo_ = 0.0, tHi_ = 1.0; // fHi_<=20 => not yet fitted
    std::vector<float> edges_; int dragEdge_ = -1;
    juce::Point<int> panLast_;
};

//==============================================================================
class SpectralContent : public juce::Component
{
public:
    explicit SpectralContent (SessionState& s) : view_ (s)
    {
        addAndMakeVisible (view_);
        auto addBtn = [this] (juce::TextButton& b, const juce::String& t, const juce::String& tip, std::function<void()> fn)
        { b.setButtonText (t); b.setTooltip (tip); b.onClick = std::move (fn); addAndMakeVisible (b); };
        addBtn (hInBtn_,  "H +", "Zoom in on time",       [this] { view_.zoomTime (1.0 / 1.5); });
        addBtn (hOutBtn_, "H -", "Zoom out on time",      [this] { view_.zoomTime (1.5); });
        addBtn (vInBtn_,  "V +", "Zoom in on frequency",  [this] { view_.zoomFreq (1.0 / 1.5); });
        addBtn (vOutBtn_, "V -", "Zoom out on frequency", [this] { view_.zoomFreq (1.5); });
        addBtn (fitBtn_,  "Fit", "Reset zoom to the full spectrum.", [this] { view_.fit(); });
        hint_.setText ("Drag lines = set band edges. Drag canvas = pan. Wheel = up/down, side-wheel = left/right. "
                       "H/V buttons zoom. Band count follows the Bands knob.",
                       juce::dontSendNotification);
        hint_.setFont (juce::Font (11.5f));
        hint_.setColour (juce::Label::textColourId, LNF::muted());
        addAndMakeVisible (hint_);
        setSize (960, 520);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto tb = r.removeFromTop (30).reduced (6, 4);
        for (auto* b : { &hInBtn_, &hOutBtn_, &vInBtn_, &vOutBtn_, &fitBtn_ })
        { b->setBounds (tb.removeFromLeft (46)); tb.removeFromLeft (4); }
        tb.removeFromLeft (8);
        hint_.setBounds (tb);
        view_.setBounds (r.reduced (6, 4));
    }
    void paint (juce::Graphics& g) override { g.fillAll (LNF::bg()); }

private:
    SpectralView view_;
    juce::TextButton hInBtn_, hOutBtn_, vInBtn_, vOutBtn_, fitBtn_;
    juce::Label hint_;
};

//==============================================================================
SpectralEditorWindow::SpectralEditorWindow (SessionState& state)
    : juce::DocumentWindow ("ToneFill - Spectral Band Editor", ToneFillLookAndFeel::bg(), juce::DocumentWindow::allButtons)
{
    setLookAndFeel (&lnf_);
    content_ = std::make_unique<SpectralContent> (state);
    setContentNonOwned (content_.get(), true);
    setResizable (true, true);
    setUsingNativeTitleBar (true);
    centreWithSize (980, 560);
    setVisible (true);
}

SpectralEditorWindow::~SpectralEditorWindow()
{
    setLookAndFeel (nullptr);
    clearContentComponent();
}

void SpectralEditorWindow::closeButtonPressed() { if (onClose) onClose(); }
} // namespace tonefill::plugin::ui
