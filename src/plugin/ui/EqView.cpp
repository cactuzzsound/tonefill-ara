#include "plugin/ui/EqView.h"
#include "plugin/ParameterState.h"
#include "dsp/EqCoefficients.h"

#include <cmath>

namespace tonefill::plugin::ui
{
namespace
{
constexpr double kDisplaySr = 48000.0;
constexpr float  kFLo = 20.0f, kFHi = 20000.0f, kGRange = 18.0f;

// Magnitude (dB) of one band at frequency f, from its biquad coefficients.
double bandDbAt (int type, float freq, float gain, float q, double f)
{
    const auto co = tonefill::dsp::makeEqCoefficients (kDisplaySr, type, freq, gain, q);
    const float* c = co.coefficients; // b0, b1, b2, a1, a2 (a0 normalised to 1)
    const double w = juce::MathConstants<double>::twoPi * f / kDisplaySr;
    const double c1 = std::cos (w), c2 = std::cos (2.0 * w), s1 = std::sin (w), s2 = std::sin (2.0 * w);
    const double nre = c[0] + c[1] * c1 + c[2] * c2, nim = -(c[1] * s1 + c[2] * s2);
    const double dre = 1.0 + c[3] * c1 + c[4] * c2, dim = -(c[3] * s1 + c[4] * s2);
    const double mag = std::sqrt ((nre * nre + nim * nim) / juce::jmax (1.0e-12, dre * dre + dim * dim));
    return 20.0 * std::log10 (juce::jmax (1.0e-6, mag));
}

const char* const kTypeNames[] = { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
} // namespace

EqView::EqView (juce::AudioProcessorValueTreeState& apvts) : apvts_ (apvts)
{
    auto setupKnob = [this] (juce::Slider& k, juce::Label& l, const juce::String& name)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
        k.setColour (juce::Slider::textBoxTextColourId, LNF::navy());
        addAndMakeVisible (k);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (10.0f));
        l.setColour (juce::Label::textColourId, LNF::label());
        addAndMakeVisible (l);
    };
    setupKnob (freqK_, freqL_, "FREQ");
    setupKnob (gainK_, gainL_, "GAIN");
    setupKnob (qK_,    qL_,    "Q");

    freqK_.setRange (kFLo, kFHi, 1.0);   freqK_.setSkewFactorFromMidPoint (1000.0);
    freqK_.setTextValueSuffix (" Hz");
    gainK_.setRange (-18.0, 18.0, 0.1);  gainK_.setTextValueSuffix (" dB");
    qK_.setRange (0.1, 10.0, 0.001);     qK_.setSkewFactorFromMidPoint (1.0);

    freqK_.setColour (juce::Slider::rotarySliderFillColourId, LNF::purple());
    gainK_.setColour (juce::Slider::rotarySliderFillColourId, LNF::accent());
    qK_.setColour    (juce::Slider::rotarySliderFillColourId, LNF::purple());

    freqK_.onValueChange = [this] { setReal (sel_, "Freq", (float) freqK_.getValue()); repaint(); };
    gainK_.onValueChange = [this] { setReal (sel_, "Gain", (float) gainK_.getValue()); repaint(); };
    qK_.onValueChange    = [this] { setReal (sel_, "Q",    (float) qK_.getValue());    repaint(); };

    for (int t = 0; t < kEqNumTypes; ++t) typeBox_.addItem (kTypeNames[t], t + 1);
    typeBox_.setTooltip ("Filter type for the selected band.");
    addAndMakeVisible (typeBox_);
    typeBox_.onChange = [this]
    {
        const int t = typeBox_.getSelectedId() - 1;
        if (t >= 0) { setReal (sel_, "Type", (float) t); syncSelectedControls(); repaint(); }
    };

    onBtn_.setClickingTogglesState (true);
    onBtn_.setTooltip ("Enable / bypass the selected band.");
    addAndMakeVisible (onBtn_);
    onBtn_.onClick = [this] { setBandOn (sel_, onBtn_.getToggleState()); repaint(); };

    for (int b = 0; b < kEqBands; ++b)
    {
        auto& btn = bandBtn_[(std::size_t) b];
        btn.setButtonText (juce::String (b + 1));
        btn.setClickingTogglesState (false);
        btn.setTooltip ("Select band " + juce::String (b + 1) + ".");
        addAndMakeVisible (btn);
        btn.onClick = [this, b] { selectBand (b); };
    }
    selectBand (0);
}

juce::RangedAudioParameter* EqView::param (int band, const char* field) const
{
    return apvts_.getParameter (ParameterState::eqId (band + 1, field));
}
float EqView::realVal (int band, const char* field) const
{
    if (auto* rp = apvts_.getRawParameterValue (ParameterState::eqId (band + 1, field))) return rp->load();
    return 0.0f;
}
void EqView::setReal (int band, const char* field, float v)
{
    if (auto* p = param (band, field)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
bool EqView::bandOn (int band) const   { return realVal (band, "On") > 0.5f; }
void EqView::setBandOn (int band, bool on)
{
    if (auto* p = param (band, "On")) p->setValueNotifyingHost (on ? 1.0f : 0.0f);
}
int  EqView::bandType (int band) const { return (int) std::lround (realVal (band, "Type")); }
bool EqView::typeHasGain (int type)    { return type == EqBell || type == EqLowShelf || type == EqHighShelf; }

float EqView::freqToX (float f) const
{
    const float t = std::log (juce::jlimit (kFLo, kFHi, f) / kFLo) / std::log (kFHi / kFLo);
    return (float) graph_.getX() + t * (float) graph_.getWidth();
}
float EqView::xToFreq (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - (float) graph_.getX()) / (float) graph_.getWidth());
    return kFLo * std::pow (kFHi / kFLo, t);
}
float EqView::gainToY (float g) const
{
    const float t = (juce::jlimit (-kGRange, kGRange, g) + kGRange) / (2.0f * kGRange);
    return (float) graph_.getBottom() - t * (float) graph_.getHeight();
}
float EqView::yToGain (float y) const
{
    const float t = juce::jlimit (0.0f, 1.0f, ((float) graph_.getBottom() - y) / (float) graph_.getHeight());
    return t * 2.0f * kGRange - kGRange;
}

juce::Colour EqView::bandColour (int band) const
{
    // Distinct hues around the palette so each band's node/curve is identifiable.
    const float hues[kEqBands] = { 0.58f, 0.50f, 0.09f, 0.02f, 0.72f };
    return juce::Colour::fromHSV (hues[band % kEqBands], 0.55f, 0.85f, 1.0f);
}

int EqView::hitNode (juce::Point<float> pos) const
{
    int best = -1; float bd = 16.0f * 16.0f;
    for (int b = 0; b < kEqBands; ++b)
    {
        const float x = freqToX (realVal (b, "Freq"));
        const float y = typeHasGain (bandType (b)) ? gainToY (realVal (b, "Gain")) : gainToY (0.0f);
        const float dx = x - pos.x, dy = y - pos.y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

void EqView::selectBand (int b)
{
    sel_ = juce::jlimit (0, kEqBands - 1, b);
    syncSelectedControls();
    repaint();
}

void EqView::syncSelectedControls()
{
    freqK_.setValue (realVal (sel_, "Freq"), juce::dontSendNotification);
    gainK_.setValue (realVal (sel_, "Gain"), juce::dontSendNotification);
    qK_.setValue    (realVal (sel_, "Q"),    juce::dontSendNotification);
    typeBox_.setSelectedId (bandType (sel_) + 1, juce::dontSendNotification);
    onBtn_.setToggleState (bandOn (sel_), juce::dontSendNotification);
    const bool hasGain = typeHasGain (bandType (sel_));
    gainK_.setEnabled (hasGain); gainL_.setEnabled (hasGain);
    gainK_.setAlpha (hasGain ? 1.0f : 0.4f); gainL_.setAlpha (hasGain ? 1.0f : 0.4f);
}

void EqView::refresh()
{
    syncSelectedControls();
    for (int b = 0; b < kEqBands; ++b)
        bandBtn_[(std::size_t) b].setToggleState (b == sel_, juce::dontSendNotification);
    repaint();
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    if (! graph_.contains (e.getPosition())) return;
    const int n = hitNode (e.position);
    if (n >= 0)
    {
        selectBand (n);
        if (! bandOn (n)) { setBandOn (n, true); onBtn_.setToggleState (true, juce::dontSendNotification); }
        dragging_ = true;
    }
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    setReal (sel_, "Freq", xToFreq (e.position.x));
    if (typeHasGain (bandType (sel_))) setReal (sel_, "Gain", yToGain (e.position.y));
    syncSelectedControls();
    repaint();
}

void EqView::mouseUp (const juce::MouseEvent&) { dragging_ = false; }

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! graph_.contains (e.getPosition())) return;
    const int n = hitNode (e.position);
    if (n >= 0) { setBandOn (n, ! bandOn (n)); refresh(); }
}

void EqView::resized()
{
    auto r = getLocalBounds();
    auto right = r.removeFromRight (300);
    r.removeFromRight (12);
    graph_ = r.reduced (2);

    // Right control column.
    auto top = right.removeFromTop (26);
    { // 5 band-select buttons
        const int bw = (top.getWidth() - 4 * 4) / kEqBands;
        for (int b = 0; b < kEqBands; ++b)
        {
            bandBtn_[(std::size_t) b].setBounds (top.removeFromLeft (bw));
            if (b < kEqBands - 1) top.removeFromLeft (4);
        }
    }
    right.removeFromTop (8);
    auto row2 = right.removeFromTop (28);
    typeBox_.setBounds (row2.removeFromLeft (right.getWidth() - 92).withSizeKeepingCentre (right.getWidth() - 92, 26));
    row2.removeFromLeft (8);
    onBtn_.setBounds (row2.withSizeKeepingCentre (84, 26));
    right.removeFromTop (10);

    // Freq / Gain / Q knobs.
    const int kw = right.getWidth() / 3;
    auto place = [] (juce::Slider& k, juce::Label& l, juce::Rectangle<int> cell)
    {
        l.setBounds (cell.removeFromTop (13));
        k.setBounds (cell);
    };
    place (freqK_, freqL_, right.removeFromLeft (kw));
    place (gainK_, gainL_, right.removeFromLeft (kw));
    place (qK_,    qL_,    right);
}

void EqView::paint (juce::Graphics& g)
{
    const auto gf = graph_.toFloat();
    g.setColour (LNF::panelHi());
    g.fillRoundedRectangle (gf, 8.0f);
    g.setColour (LNF::lineSoft());
    g.drawRoundedRectangle (gf, 8.0f, 1.0f);

    // Grid: freq decades + labels, and the 0 dB centre line.
    g.setFont (juce::Font (9.0f));
    const float decades[] = { 30, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000, 20000 };
    for (float f : decades)
    {
        const float x = freqToX (f);
        g.setColour (LNF::lineSoft());
        g.fillRect (x, gf.getY() + 2.0f, 1.0f, gf.getHeight() - 4.0f);
    }
    const char* labels[] = { "100", "1k", "10k" };
    const float labelF[] = { 100.0f, 1000.0f, 10000.0f };
    g.setColour (LNF::muted());
    for (int i = 0; i < 3; ++i)
        g.drawText (labels[i], (int) freqToX (labelF[i]) - 14, graph_.getBottom() - 12, 28, 11, juce::Justification::centred, false);
    // Horizontal gain guides (-12, -6, 0, +6, +12).
    for (int dB = -12; dB <= 12; dB += 6)
    {
        const float y = gainToY ((float) dB);
        g.setColour (dB == 0 ? LNF::line() : LNF::lineSoft());
        g.fillRect (gf.getX() + 2.0f, y, gf.getWidth() - 4.0f, dB == 0 ? 1.0f : 0.6f);
    }

    // Sample the composite (and per-band) response across the graph width.
    const int N = juce::jmax (2, graph_.getWidth());
    juce::Path composite;
    std::array<juce::Path, kEqBands> bandPaths;
    const bool masterActive = isEnabled();
    for (int px = 0; px < N; ++px)
    {
        const float x = (float) graph_.getX() + (float) px;
        const double f = (double) xToFreq (x);
        double sum = 0.0;
        for (int b = 0; b < kEqBands; ++b)
        {
            if (! bandOn (b)) continue;
            const double db = bandDbAt (bandType (b), realVal (b, "Freq"), realVal (b, "Gain"), realVal (b, "Q"), f);
            sum += db;
            const float by = gainToY ((float) db);
            if (px == 0) bandPaths[(std::size_t) b].startNewSubPath (x, by); else bandPaths[(std::size_t) b].lineTo (x, by);
        }
        const float y = gainToY ((float) sum);
        if (px == 0) composite.startNewSubPath (x, y); else composite.lineTo (x, y);
    }

    // Faint per-band curves.
    for (int b = 0; b < kEqBands; ++b)
    {
        if (! bandOn (b)) continue;
        g.setColour (bandColour (b).withAlpha (b == sel_ ? 0.55f : 0.30f));
        g.strokePath (bandPaths[(std::size_t) b], juce::PathStrokeType (1.2f));
    }
    // Composite curve.
    g.setColour (masterActive ? LNF::navy() : LNF::muted());
    g.strokePath (composite, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Band nodes.
    for (int b = 0; b < kEqBands; ++b)
    {
        const float x = freqToX (realVal (b, "Freq"));
        const float y = typeHasGain (bandType (b)) ? gainToY (realVal (b, "Gain")) : gainToY (0.0f);
        const bool on = bandOn (b);
        const float rr = (b == sel_) ? 7.0f : 5.5f;
        g.setColour (on ? bandColour (b) : LNF::muted().withAlpha (0.5f));
        g.fillEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f);
        if (b == sel_)
        {
            g.setColour (LNF::navy());
            g.drawEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f, 1.6f);
        }
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), (int) (x - 6.0f), (int) (y - 6.0f), 12, 12, juce::Justification::centred, false);
    }
}
} // namespace tonefill::plugin::ui
