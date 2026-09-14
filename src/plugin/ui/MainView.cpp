#include "plugin/ui/MainView.h"
#include "plugin/ui/EqView.h"
#include "plugin/ui/WaveformWindow.h"
#include "plugin/ui/SpectralEditorWindow.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ParameterState.h"
#include "dsp/Loudness.h"
#include "BinaryData.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace tonefill::plugin::ui
{
using IDs = ParameterState::IDs;
using LNF = ToneFillLookAndFeel;

namespace
{
juce::String formatClock (double seconds)
{
    if (seconds < 0) seconds = 0;
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String::formatted ("%d:%04.1f", m, s);
}

void initKnob (juce::Component& parent, MainView::Knob& k, const juce::String& name,
               const juce::String& tip, juce::Colour arc)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    k.slider.setRange (0.0, 1.0, 0.001);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, arc);
    k.slider.setColour (juce::Slider::textBoxTextColourId, LNF::navy()); // value text: navy, not white
    k.slider.setTooltip (tip);
    parent.addAndMakeVisible (k.slider);
    k.label.setText (name.toUpperCase(), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (10.5f));
    k.label.setColour (juce::Label::textColourId, LNF::label());
    k.label.setTooltip (tip);
    parent.addAndMakeVisible (k.label);
}
} // namespace

MainView::MainView (PluginProcessor& processor) : processor_ (processor)
{
    setLookAndFeel (&lnf_);
    auto& apvts = processor_.parameters().apvts;

    initKnob (*this, threshold_, "Clean Level",
              "Auto mode: how quiet a moment must be to count as room tone. Lower = stricter, rejects claps, clicks and loud bits.", LNF::accent());
    initKnob (*this, speech_, "Voice Reject",
              "How hard to exclude voice / breaths / mouth noise. Higher = cleaner room tone.", LNF::accent());
    initKnob (*this, minFill_, "Min Fill",
              "Minimum length a stable fragment must have to be used. Longer = fewer, more consistent chunks and fewer audible joins.", LNF::accent());
    minFill_.slider.setRange (0.2, 5.0, 0.05); minFill_.slider.setTextValueSuffix (" s");
    initKnob (*this, flatness_, "Flatness",
              "How strict the stationarity requirement is. Higher = only very steady stretches, so the fill avoids audible jumps.", LNF::purple());
    initKnob (*this, bands_, "Bands",
              "Spectral only: how many frequency bands the source is split into. Fewer = wider bands, more = narrower bands.", LNF::purple());
    initKnob (*this, blend_, "Crossfade",
              "How much neighbouring chunks overlap and blend. Higher = smoother joins.", LNF::purple());
    initKnob (*this, variation_, "Smoothness",
              "Enhance only: resynthesis window size. Higher = smoother, more diffuse; lower = keeps more fine texture.", LNF::purple());
    initKnob (*this, gain_, "Output", "Output volume, in dB.", LNF::accent());
    gain_.slider.setRange (-24.0, 24.0, 0.1);
    initKnob (*this, length_, "Export Len",
              "Length of the WAV written by Export, in seconds. The loop is tiled seamlessly to this length.", LNF::purple());
    length_.slider.setRange (0.5, 30.0, 0.1); length_.slider.setTextValueSuffix (" s");

    thA_ = std::make_unique<SA> (apvts, IDs::threshold,    threshold_.slider);
    spA_ = std::make_unique<SA> (apvts, IDs::speechReject, speech_.slider);
    blA_ = std::make_unique<SA> (apvts, IDs::blend,        blend_.slider);
    sbA_ = std::make_unique<SA> (apvts, IDs::spectralBands, bands_.slider);
    vaA_ = std::make_unique<SA> (apvts, IDs::randomness,   variation_.slider);
    mfA_ = std::make_unique<SA> (apvts, IDs::minFill,      minFill_.slider);
    flA_ = std::make_unique<SA> (apvts, IDs::flatness,     flatness_.slider);
    gaA_ = std::make_unique<SA> (apvts, IDs::outputGain,   gain_.slider);
    leA_ = std::make_unique<SA> (apvts, IDs::renderLength, length_.slider);

    // Learn-source mode: Auto vs Manual (one segmented section).
    for (auto* b : { &autoBtn_, &manualBtn_ }) { b->setClickingTogglesState (true); b->setRadioGroupId (200); b->getProperties().set ("primary", true); addAndMakeVisible (*b); }
    autoBtn_.setToggleState (true, juce::dontSendNotification);
    autoBtn_.setTooltip ("Plugin finds clean room tone automatically.");
    manualBtn_.setTooltip ("You pick the room-tone regions yourself: drag on the waveform below to add, click a region to remove.");
    autoBtn_.onClick   = [this] { manualMode_ = false; processor_.sessionState().manualMode.store (false); repaint(); };
    manualBtn_.onClick = [this] { manualMode_ = true;  processor_.sessionState().manualMode.store (true);  repaint(); };

    // Processing group: Enhance + Full.
    enhanceBtn_.setClickingTogglesState (true);
    enhanceBtn_.setTooltip ("Extra smoothing resynthesis for when the selected fragments won't blend into a clean bed. Enables Smoothness.");
    addAndMakeVisible (enhanceBtn_);
    enA_ = std::make_unique<BA> (apvts, IDs::paulStretch, enhanceBtn_);
    wholeBtn_.setClickingTogglesState (true);
    wholeBtn_.setTooltip ("Analyze the WHOLE item to find clean room tone scattered across a long take (slower, more thorough). Off = the first 10 minutes.");
    addAndMakeVisible (wholeBtn_);
    wfA_ = std::make_unique<BA> (apvts, IDs::wholeFile, wholeBtn_);
    // Mode (mutually exclusive): Classic / Experimental pick the selection engine; Spectral switches
    // to the per-band mosaic. All three driven manually on the statistical + spectral params.
    for (auto* b : { &classicBtn_, &expBtn_, &spectralBtn_ }) { b->setClickingTogglesState (false); addAndMakeVisible (*b); }
    classicBtn_.setTooltip ("Classic selection: the tuned gate stack (default).");
    expBtn_.setTooltip ("Experimental selection: weighted per-frame scoring. A/B against Classic by ear.");
    spectralBtn_.setTooltip ("Spectral (experimental): split into frequency bands, find clean room tone per band and recombine. Steadier low end, more usable material. The Bands knob sets how many bands. Slower to render.");
    {
        auto* stat = apvts.getParameter (IDs::statistical);
        auto* spec = apvts.getParameter (IDs::spectral);
        auto setMode = [stat, spec] (bool s, bool sp)
        {
            if (stat) stat->setValueNotifyingHost (s  ? 1.0f : 0.0f);
            if (spec) spec->setValueNotifyingHost (sp ? 1.0f : 0.0f);
        };
        classicBtn_.onClick  = [setMode] { setMode (false, false); };
        expBtn_.onClick      = [setMode] { setMode (true,  false); };
        spectralBtn_.onClick = [setMode] { setMode (false, true);  };
    }
    advBtn_.setClickingTogglesState (true);
    advBtn_.setTooltip ("Spectral Advanced: open a spectrum editor to place each band's frequency edges by hand on a spectrogram of the source.");
    addAndMakeVisible (advBtn_);
    adA_ = std::make_unique<BA> (apvts, IDs::spectralAdv, advBtn_);
    advBtn_.onClick = [this] { if (advBtn_.getToggleState()) openSpectralWindow(); };

    expandBtn_.setTooltip ("Open a large waveform view with a timecode ruler, zoom and scroll for precise selecting.");
    expandBtn_.getProperties().set ("icon", (int) LNF::IcExpand);
    expandBtn_.onClick = [this] { openWaveformWindow(); };
    addAndMakeVisible (expandBtn_);

    regenBtn_.setTooltip ("New random variation of the fill (same settings).");
    regenBtn_.getProperties().set ("icon", (int) LNF::IcRefresh);
    regenBtn_.onClick = [this] { auto& ss = processor_.sessionState(); ss.seed.fetch_add (0x9E3779B97F4A7C15ULL); ss.generation.fetch_add (1); };
    addAndMakeVisible (regenBtn_);
    exportBtn_.getProperties().set ("accent", true);
    exportBtn_.getProperties().set ("icon", (int) LNF::IcDownload);
    exportBtn_.setTooltip ("Write the fill to a WAV file. Length is set by Export Len.");
    exportBtn_.onClick = [this] { exportWav(); };
    addAndMakeVisible (exportBtn_);
    bypassBtn_.setClickingTogglesState (true);
    bypassBtn_.getProperties().set ("icon", (int) LNF::IcPower);
    bypassBtn_.setTooltip ("Monitor the original source instead of the room tone, to A/B them. Does not change what Export writes.");
    addAndMakeVisible (bypassBtn_);
    byA_ = std::make_unique<BA> (apvts, IDs::bypass, bypassBtn_);

    normBtn_.setClickingTogglesState (true);
    normBtn_.getProperties().set ("icon", (int) LNF::IcSparkle);
    normBtn_.setTooltip ("Bake the output to a fixed loudness target. When on, Output is bypassed.");
    addAndMakeVisible (normBtn_);
    neA_ = std::make_unique<BA> (apvts, IDs::normEnabled, normBtn_);

    // Hidden parameter-bound backing; presented as a −/value/+ stepper.
    normTarget_.setSliderStyle (juce::Slider::IncDecButtons);
    normTarget_.setRange (-60.0, 0.0, 0.1);
    normTarget_.setTooltip ("Target level to normalize to. Peak dBFS, or integrated LUFS.");
    addChildComponent (normTarget_);
    ntA_ = std::make_unique<SA> (apvts, IDs::normTarget, normTarget_);

    normValueLbl_.setJustificationType (juce::Justification::centred);
    normValueLbl_.setFont (juce::Font (13.5f, juce::Font::bold));
    normValueLbl_.setColour (juce::Label::textColourId, LNF::navy());
    normValueLbl_.setTooltip ("Target level to normalize to. Peak dBFS, or integrated LUFS.");
    addAndMakeVisible (normValueLbl_);
    for (auto* b : { &normMinus_, &normPlus_ }) addAndMakeVisible (*b);
    normMinus_.getProperties().set ("icon", (int) LNF::IcMinus);
    normPlus_.getProperties().set ("icon", (int) LNF::IcPlus);
    normMinus_.setTooltip ("Lower the target by 0.5.");
    normPlus_.setTooltip ("Raise the target by 0.5.");
    normMinus_.onClick = [this] { normTarget_.setValue (normTarget_.getValue() - 0.5, juce::sendNotificationSync); };
    normPlus_.onClick  = [this] { normTarget_.setValue (normTarget_.getValue() + 0.5, juce::sendNotificationSync); };

    normUnit_.addItem ("dBFS", 1); normUnit_.addItem ("LUFS", 2);
    normUnit_.setTooltip ("dBFS = normalize the peak. LUFS = normalize integrated loudness (EBU R128).");
    addAndMakeVisible (normUnit_);
    nuA_ = std::make_unique<CBA> (apvts, IDs::normUnit, normUnit_);

    normReadout_.setFont (juce::Font (12.0f));
    normReadout_.setColour (juce::Label::textColourId, LNF::purple());
    normReadout_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (normReadout_);

    // Enhance-only HISS FILTER (separate panel under Texture); the switch is active only in Enhance.
    hissBtn_.setClickingTogglesState (true);
    hissBtn_.getProperties().set ("icon", (int) LNF::IcWave);
    hissBtn_.setTooltip ("Enhance only: a 5-band parametric EQ on the room-tone bed (also tames the HF hiss "
                         "PaulStretch can add). Toggle on to reveal the EQ editor below.");
    addAndMakeVisible (hissBtn_);
    hbA_ = std::make_unique<BA> (apvts, IDs::hissFilter, hissBtn_);
    // The EQ editor (graph + Freq/Gain/Q controls) expands under the action bar when EQ is on.
    EqView::Access eqAcc;
    eqAcc.getReal = [&apvts] (int band, const char* field) -> float
    {
        if (auto* rp = apvts.getRawParameterValue (ParameterState::eqId (band + 1, field))) return rp->load();
        return 0.0f;
    };
    eqAcc.setReal = [&apvts] (int band, const char* field, float v)
    {
        if (auto* p = apvts.getParameter (ParameterState::eqId (band + 1, field)))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    eqView_ = std::make_unique<EqView> (std::move (eqAcc));
    addChildComponent (*eqView_);

    tips_ = {
        "Push Voice Reject up to strip breaths and mouth noise from the bed.",
        "In Auto, keep Clean Level low - it rejects claps and loud bits harder.",
        "Raise Min Fill for longer, more consistent chunks and fewer joins.",
        "Turn on Enhance when the fragments won't blend into a smooth bed.",
        "Flatness high = only very steady stretches, so fewer audible jumps.",
        "Nudge Crossfade up if you hear clicks at the chunk boundaries.",
        "Use Manual to drag your own room-tone regions on the waveform below.",
        "Full scans the whole take for clean tone scattered across a long clip.",
        "Normalize bakes the output to a fixed LUFS or dBFS target on export.",
        "Regenerate rolls a new random variation with the same settings.",
        "Experimental selection scores every frame - A/B it against Classic."
    };
    tipLbl_.setText (tips_[0], juce::dontSendNotification);
    tipLbl_.setFont (juce::Font (12.5f));
    tipLbl_.setColour (juce::Label::textColourId, juce::Colour (0xff4a4d55));
    tipLbl_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tipLbl_);

    startTimerHz (15);
}

MainView::~MainView() { stopTimer(); setLookAndFeel (nullptr); }

void MainView::openWaveformWindow()
{
    if (waveWin_ == nullptr) { waveWin_ = std::make_unique<WaveformWindow> (processor_.sessionState()); waveWin_->onClose = [this] { waveWin_.reset(); }; }
    else waveWin_->toFront (true);
}

void MainView::openSpectralWindow()
{
    if (specWin_ == nullptr)
    {
        // Auto Analyze / edge edits can change the band count; push it back onto the Bands parameter
        // (host-notifying) so the knob, automation and saved state stay in sync.
        auto setBands = [this] (int n)
        {
            if (auto* p = processor_.parameters().apvts.getParameter (IDs::spectralBands))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) n));
        };
        specWin_ = std::make_unique<SpectralEditorWindow> (processor_.sessionState(), std::move (setBands));
        specWin_->onClose = [this] { specWin_.reset(); };
    }
    else specWin_->toFront (true);
}

void MainView::updateEmphasis()
{
    auto set = [] (Knob& k, bool on) { const float a = on ? 1.0f : 0.34f; k.slider.setAlpha (a); k.label.setAlpha (a); };
    const bool enhance = processor_.parameters().apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    set (threshold_, true); set (speech_, true); set (minFill_, true); set (flatness_, true);
    set (blend_, ! enhance);
    set (variation_, enhance);
    set (gain_, true); set (length_, true);
}

void MainView::drawGroupIcon (juce::Graphics& g, juce::Rectangle<float> b, int icon, juce::Colour col) const
{
    g.setColour (col);
    const float cx = b.getCentreX(), cy = b.getCentreY(), w = b.getWidth(), h = b.getHeight();
    const juce::PathStrokeType st (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (icon)
    {
        case GiDetect:
            g.drawEllipse (cx - w * 0.42f, cy - h * 0.42f, w * 0.84f, h * 0.84f, 1.4f);
            g.drawEllipse (cx - w * 0.18f, cy - h * 0.18f, w * 0.36f, h * 0.36f, 1.4f);
            g.fillEllipse (cx - 1.3f, cy - 1.3f, 2.6f, 2.6f);
            break;
        case GiStruct:
            for (int iy = 0; iy < 2; ++iy) for (int ix = 0; ix < 2; ++ix)
            {
                const float x = b.getX() + w * (0.16f + 0.42f * ix), y = b.getY() + h * (0.16f + 0.42f * iy);
                g.drawRoundedRectangle (x, y, w * 0.28f, h * 0.28f, 1.5f, 1.3f);
            }
            break;
        case GiTexture:
        {
            juce::Path p; const int N = 20;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float) i / (float) N;
                const float x = b.getX() + t * w;
                const float y = cy - std::sin (t * juce::MathConstants<float>::twoPi) * h * 0.34f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, st);
            break;
        }
        default: break;
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (LNF::bg());
    const auto navy = LNF::navy();

    // Logo (embedded PNG, trimmed to its content bounding box so the transparent margins don't
    // waste header space), scaled to fit preserving aspect.
    {
        static const juce::Image logo = []
        {
            auto full = juce::ImageCache::getFromMemory (BinaryData::tonefilllogo_png, BinaryData::tonefilllogo_pngSize);
            return (full.isValid() && full.getWidth() >= 1652) ? full.getClippedImage ({ 331, 462, 1321, 280 }) : full;
        }();
        if (logo.isValid())
            g.drawImageWithin (logo, 16, 10, 264, 42,
                               juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, false);
    }

    // Faint grouped backgrounds behind the three header button clusters.
    auto groupBg = [&] (juce::Rectangle<int> a, juce::Rectangle<int> b)
    {
        auto u = a.getUnion (b).toFloat().expanded (4.0f);
        g.setColour (LNF::panelHi());
        g.fillRoundedRectangle (u, 9.0f);
        g.setColour (LNF::line());
        g.drawRoundedRectangle (u, 9.0f, 1.0f);
    };
    groupBg (autoBtn_.getBounds(), manualBtn_.getBounds());
    groupBg (enhanceBtn_.getBounds(), wholeBtn_.getBounds());
    groupBg (classicBtn_.getBounds(), spectralBtn_.getBounds()); // engine group spans all 3 modes

    // Three group cards + headers.
    const char* titles[3] = { "DETECTION", "STRUCTURE", "TEXTURE" };
    const int   icons[3]  = { GiDetect, GiStruct, GiTexture };
    for (int i = 0; i < 3; ++i)
    {
        const auto cf = groupCard_[(std::size_t) i].toFloat();
        LNF::softShadow (g, cf, 16.0f);
        g.setColour (LNF::panel());     g.fillRoundedRectangle (cf, 16.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (cf, 16.0f, 1.0f);
        const float hy = cf.getY() + 18.0f;
        g.setFont (juce::Font (11.0f));
        const int labW = juce::Font (11.0f).getStringWidth (titles[i]);
        const float total = (float) labW + 24.0f;
        const float lx = cf.getCentreX() - total * 0.5f;
        drawGroupIcon (g, { lx, hy - 7.0f, 14.0f, 14.0f }, icons[i], LNF::navy().withAlpha (0.7f));
        g.setColour (LNF::label());
        g.drawText (titles[i], (int) (lx + 20.0f), (int) (hy - 7.0f), labW + 10, 14, juce::Justification::centredLeft, false);
        g.setColour (LNF::lineSoft());
        g.fillRect (cf.getX() + 14.0f, hy + 12.0f, cf.getWidth() - 28.0f, 1.0f);
    }

    // Knob value pills.
    for (const auto& pill : valuePills_)
    {
        g.setColour (LNF::panelHi());
        g.fillRoundedRectangle (pill.toFloat(), 7.0f);
        g.setColour (LNF::lineSoft());
        g.drawRoundedRectangle (pill.toFloat(), 7.0f, 1.0f);
    }

    // Vertical output meter.
    {
        const auto mf = meterArea_.toFloat();
        LNF::softShadow (g, mf, 16.0f);
        g.setColour (LNF::panel());     g.fillRoundedRectangle (mf, 16.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (mf, 16.0f, 1.0f);
        g.setColour (LNF::label());
        g.setFont (juce::Font (10.0f));
        g.drawText ("OUTPUT", meterArea_.removeFromTop (0).withY (meterArea_.getY() + 8).withHeight (12).withX (meterArea_.getX()).withWidth (meterArea_.getWidth()),
                    juce::Justification::centred, false);

        const float lvl = juce::jlimit (0.0f, 1.0f, (meterDb_ + 60.0f) / 60.0f);
        auto barsArea = mf.reduced (14.0f, 30.0f).withTrimmedBottom (18.0f);
        const float bw = (barsArea.getWidth() - 8.0f) * 0.5f;
        const int segs = 22;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float bx = barsArea.getX() + ch * (bw + 8.0f);
            const float sh = (barsArea.getHeight() - (segs - 1) * 2.0f) / segs;
            for (int sIdx = 0; sIdx < segs; ++sIdx)
            {
                const float frac = (float) (segs - 1 - sIdx) / (float) (segs - 1);
                const float by = barsArea.getY() + sIdx * (sh + 2.0f);
                const bool lit = frac <= lvl;
                juce::Colour col = frac > 0.85f ? LNF::accent() : LNF::purple(); // clip = orange, else Cool Sky
                g.setColour (lit ? col : LNF::lineSoft());
                g.fillRoundedRectangle (bx, by, bw, sh, 2.0f);
            }
        }
        g.setColour (navy);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (meterDb_, 1) + " dB", mf.withTop (mf.getBottom() - 16.0f).withHeight (14.0f),
                    juce::Justification::centred, false);
    }

    // Action bar: Normalize + LUFS stepper on the left, Hiss / Bypass / Regenerate / Export on the
    // right. Grows a line when the Hiss filter reveals its Freq / Q knobs.
    {
        LNF::softShadow (g, actionCard_.toFloat(), 16.0f);
        g.setColour (LNF::panel());     g.fillRoundedRectangle (actionCard_.toFloat(), 16.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (actionCard_.toFloat(), 16.0f, 1.0f);
        // Value pill behind the −/value/+ stepper.
        const auto vp = normValueLbl_.getBounds().toFloat();
        g.setColour (LNF::panelHi());   g.fillRoundedRectangle (vp, 8.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (vp, 8.0f, 1.0f);
    }

    // Tip card + bulb glyph.
    {
        LNF::softShadow (g, tipCard_.toFloat(), 16.0f);
        g.setColour (LNF::panel());     g.fillRoundedRectangle (tipCard_.toFloat(), 16.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (tipCard_.toFloat(), 16.0f, 1.0f);
        LNF::drawIcon (g, { (float) tipCard_.getX() + 14.0f, (float) tipCard_.getCentreY() - 8.0f, 16.0f, 16.0f }, LNF::IcBulb, LNF::accent());
        g.setColour (LNF::accent());
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText ("TIP", tipCard_.getX() + 36, tipCard_.getY(), 40, tipCard_.getHeight(), juce::Justification::centredLeft, false);
    }

    // Bottom panel: waveform + data + status.
    {
        LNF::softShadow (g, bottomCard_.toFloat(), 16.0f);
        g.setColour (LNF::panel());     g.fillRoundedRectangle (bottomCard_.toFloat(), 16.0f);
        g.setColour (LNF::lineSoft());  g.drawRoundedRectangle (bottomCard_.toFloat(), 16.0f, 1.0f);
    }

    // Waveform.
    g.setColour (LNF::panelHi());
    g.fillRoundedRectangle (waveArea_.toFloat(), 6.0f);
    const int srcN = processor_.sessionState().sourceSamples.load();
    auto selectedBin = [&] (int i, int nBins) -> bool
    {
        if (srcN <= 0) return false;
        const int s0 = (int) ((long long) i * srcN / nBins), s1 = (int) ((long long) (i + 1) * srcN / nBins);
        for (const auto& sel : selections_) if (sel.first < s1 && sel.second > s0) return true;
        if (dragging_) { const int a = juce::jmin (dragStartSample_, dragCurSample_), b = juce::jmax (dragStartSample_, dragCurSample_); if (a < s1 && b > s0) return true; }
        return false;
    };
    if (! wave_.peak.empty())
    {
        const int n = (int) wave_.peak.size();
        const float w = (float) waveArea_.getWidth(), midY = (float) waveArea_.getCentreY();
        const float halfH = ((float) waveArea_.getHeight() - 8.0f) * 0.5f, bw = w / (float) n;
        g.setColour (LNF::line());
        g.fillRect ((float) waveArea_.getX(), midY - 0.5f, w, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float amp = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, wave_.peak[(std::size_t) i])));
            const float hh = juce::jmax (1.0f, amp * halfH);
            const bool lit = manualMode_ ? selectedBin (i, n) : (i < (int) wave_.clean.size() && wave_.clean[(std::size_t) i]);
            g.setColour (lit ? LNF::accent() : juce::Colour (0xffc9c4b8));
            g.fillRect ((float) waveArea_.getX() + (float) i * bw, midY - hh, juce::jmax (1.0f, bw + 0.5f), 2.0f * hh);
        }
    }
    if (manualMode_)
    {
        auto edge = [&] (int s0, int s1, juce::Colour c)
        {
            const float x0 = sampleToX (juce::jmin (s0, s1)), x1 = sampleToX (juce::jmax (s0, s1));
            g.setColour (c);
            g.drawRect (juce::Rectangle<float> { x0, (float) waveArea_.getY(), juce::jmax (2.0f, x1 - x0), (float) waveArea_.getHeight() }, 1.0f);
        };
        for (const auto& sel : selections_) edge (sel.first, sel.second, LNF::accent().withAlpha (0.9f));
        if (dragging_) edge (dragStartSample_, dragCurSample_, LNF::accent());
    }

    // Timecode ruler.
    {
        const double sr = processor_.sessionState().sourceSampleRate.load();
        const double totalSec = (srcN > 0 && sr > 0) ? srcN / sr : 0.0;
        g.setFont (juce::Font (9.5f));
        if (totalSec > 0.0)
            for (int i = 0; i <= 6; ++i)
            {
                const float x = waveRuler_.getX() + (float) i / 6.0f * waveRuler_.getWidth();
                g.setColour (LNF::line());   g.fillRect (x, (float) waveRuler_.getY(), 1.0f, 4.0f);
                g.setColour (LNF::muted());
                const auto just = i == 6 ? juce::Justification::centredRight : juce::Justification::centredLeft;
                g.drawText (formatClock (totalSec * i / 6.0),
                            juce::Rectangle<int> ((int) x - (i == 6 ? 60 : -2), waveRuler_.getY() + 3, 60, 10), just, false);
            }
    }

    // Data line (used / chunks / avail / seam / in) + status.
    auto& ss = processor_.sessionState();
    const int phase = ss.phase.load();
    const juce::String ph = phase == 0 ? "idle" : (phase == 1 ? "analyzing" : "ready");
    const float seam = ss.seamRiskDb.load();
    const juce::String seamStr = seam > 0.05f ? "   ·   seam " + juce::String (seam, 1) + " dB" + (seam > 4.0f ? " !" : "") : juce::String();
    g.setColour (LNF::navy());
    g.setFont (juce::Font (12.0f));
    g.drawText ("used " + juce::String (ss.learnSeconds.load(), 1) + " s (" + juce::String (ss.cleanChunks.load()) + " chunks)"
                + "   ·   avail " + juce::String (ss.availSeconds.load(), 1) + " s" + seamStr
                + "   ·   in " + juce::String (ss.levelDb.load(), 0) + " dB",
                dataArea_, juce::Justification::centredLeft, false);

    const double sr = processor_.sessionState().sourceSampleRate.load();
    const double totalSec = (srcN > 0 && sr > 0) ? srcN / sr : 0.0;
    const bool computing = processor_.sessionState().computing.load();
    g.setColour (computing ? LNF::accent() : LNF::good());
    g.fillEllipse ((float) dataArea_.getX(), (float) dataArea_.getBottom() + 6.0f, 8.0f, 8.0f);
    g.setColour (computing ? LNF::accent() : LNF::muted());
    g.setFont (juce::Font (11.0f));
    const juce::String head = computing ? "RECOMPUTING..." : "READY";
    g.drawText (head + "   ·   " + juce::String (sr / 1000.0, 1) + " kHz   ·   24-bit   ·   " + formatClock (totalSec),
                dataArea_.getX() + 14, dataArea_.getBottom() + 3, dataArea_.getWidth(), 14, juce::Justification::centredLeft, false);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto head = r.removeFromTop (40);
    head.removeFromLeft (290); // logo/wordmark drawn in paint
    {
        auto place = [] (juce::TextButton& a, juce::TextButton& b, juce::Rectangle<int> box)
        { const int w = (box.getWidth() - 4) / 2; a.setBounds (box.removeFromLeft (w)); box.removeFromLeft (4); b.setBounds (box); };
        auto engine = head.removeFromRight (270).withSizeKeepingCentre (270, 26);
        { const int ew = (engine.getWidth() - 8) / 3;
          classicBtn_.setBounds (engine.removeFromLeft (ew)); engine.removeFromLeft (4);
          expBtn_.setBounds (engine.removeFromLeft (ew));     engine.removeFromLeft (4);
          spectralBtn_.setBounds (engine); }
        head.removeFromRight (8);
        advBtn_.setBounds (head.removeFromRight (84).withSizeKeepingCentre (84, 26));
        head.removeFromRight (10);
        auto proc = head.removeFromRight (150).withSizeKeepingCentre (150, 26);
        place (enhanceBtn_, wholeBtn_, proc);
        head.removeFromRight (10);
        auto learn = head.removeFromRight (130).withSizeKeepingCentre (130, 26);
        place (autoBtn_, manualBtn_, learn);
    }
    r.removeFromTop (12);

    // Main row: 3 group cards + meter column.
    valuePills_.clear();
    auto main = r.removeFromTop (208);
    meterArea_ = main.removeFromRight (96);
    main.removeFromRight (12);
    const int gap = 12, cardW = (main.getWidth() - 2 * gap) / 3;
    // Cards hold a variable knob count and centre against the 3-slot width.
    std::vector<std::vector<Knob*>> groups = {
        { &threshold_, &speech_, &minFill_ },
        { &flatness_, &bands_, &blend_ },
        { &variation_, &gain_, &length_ }
    };
    for (int i = 0; i < 3; ++i)
    {
        auto card = main.removeFromLeft (i < 2 ? cardW : main.getWidth());
        if (i < 2) main.removeFromLeft (gap);
        groupCard_[(std::size_t) i] = card;
        auto body = card.reduced (8, 0).withTrimmedTop (34);
        const int count = (int) groups[(std::size_t) i].size();
        const int kw = body.getWidth() / juce::jmax (3, count);
        if (count < 3) body.removeFromLeft ((3 - count) * kw / 2); // centre a short card
        for (int j = 0; j < count; ++j)
        {
            auto cell = body.removeFromLeft (kw);
            auto inner = cell.reduced (4, 6);
            inner.removeFromTop (0);
            auto lab = inner.removeFromTop (14);
            groups[(std::size_t) i][(std::size_t) j]->label.setBounds (lab);
            groups[(std::size_t) i][(std::size_t) j]->slider.setBounds (inner);
            valuePills_.push_back (inner.removeFromBottom (20).reduced (10, 1));
        }
    }
    r.removeFromTop (12);

    // Action bar (one full-width card): Normalize + −/value/+ stepper + unit + "now …" readout on the
    // left; Hiss / Bypass / Regenerate / Export on the right. When the Hiss filter is on (Enhance) the
    // card grows a line and reveals its Freq / Q knobs beneath the right cluster.
    auto& apvts = processor_.parameters().apvts;
    const bool hissEnh  = apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    const bool hissShow = hissEnh && apvts.getRawParameterValue (IDs::hissFilter)->load() > 0.5f;
    const int  hissExtra = hissShow ? 196 : 0;
    auto actionRow = r.removeFromTop (60 + hissExtra);
    actionCard_ = actionRow;
    auto bar = actionRow.withHeight (60).reduced (14, 0);
    {
        normBtn_.setBounds (bar.removeFromLeft (128).withSizeKeepingCentre (128, 34));
        bar.removeFromLeft (12);
        normMinus_.setBounds (bar.removeFromLeft (34).withSizeKeepingCentre (34, 34));
        bar.removeFromLeft (6);
        normValueLbl_.setBounds (bar.removeFromLeft (104).withSizeKeepingCentre (104, 30));
        bar.removeFromLeft (6);
        normPlus_.setBounds (bar.removeFromLeft (34).withSizeKeepingCentre (34, 34));
        bar.removeFromLeft (12);
        normUnit_.setBounds (bar.removeFromLeft (78).withSizeKeepingCentre (78, 30));
        bar.removeFromLeft (10);
        // Right cluster (laid out from the right edge inward).
        exportBtn_.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 40));
        bar.removeFromRight (10);
        regenBtn_.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 40));
        bar.removeFromRight (10);
        bypassBtn_.setBounds (bar.removeFromRight (116).withSizeKeepingCentre (116, 40));
        bar.removeFromRight (10);
        hissBtn_.setBounds (bar.removeFromRight (128).withSizeKeepingCentre (128, 34));
        bar.removeFromRight (12);
        normReadout_.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 24)); // "now …" fills the middle
    }
    if (hissShow && eqView_ != nullptr)
        eqView_->setBounds (actionRow.withTrimmedTop (60).reduced (14, 8));
    r.removeFromTop (12);

    // Tip row: rotating tip on the left, Expand pill on the right (inside the card).
    auto tipRow = r.removeFromTop (46);
    tipCard_ = tipRow;
    expandBtn_.setBounds (tipRow.removeFromRight (124).withSizeKeepingCentre (108, 32));
    tipLbl_.setBounds (tipCard_.reduced (14, 0).withTrimmedLeft (44).withTrimmedRight (116));
    r.removeFromTop (12);

    // Bottom panel: waveform + ruler + data line.
    bottomCard_ = r;
    auto bc = bottomCard_.reduced (14, 12);
    waveArea_  = bc.removeFromTop (juce::jmax (40, bc.getHeight() - 46));
    waveRuler_ = bc.removeFromTop (12);
    bc.removeFromTop (4);
    dataArea_ = bc.removeFromTop (16);
}

void MainView::loadParamsFromState (SessionState& ss)
{
    auto& apvts = processor_.parameters().apvts;
    auto setF = [&apvts] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto setB = [&apvts] (const juce::String& id, bool v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (v ? 1.0f : 0.0f);
    };

    // Only the parameters the timer mirrors the other way, so load/store stay symmetric.
    setF (IDs::threshold,    ss.threshold.load());
    setF (IDs::speechReject, ss.speechReject.load());
    setF (IDs::fragment,     ss.fragment.load());
    setF (IDs::blend,        ss.blend.load());
    setF (IDs::randomness,   ss.randomness.load());
    setF (IDs::minFill,      ss.minFill.load());
    setF (IDs::flatness,     ss.flatness.load());
    setB (IDs::paulStretch,  ss.paulStretch.load());
    setF (IDs::outputGain,   juce::jlimit (-24.0f, 24.0f, juce::Decibels::gainToDecibels (ss.outputGain.load())));
    setF (IDs::renderLength, ss.renderLength.load());
    setB (IDs::normEnabled,  ss.normalizeEnabled.load());
    setF (IDs::normTarget,   ss.normalizeTarget.load());
    setB (IDs::normUnit,     ss.normalizeLufs.load());
    setB (IDs::wholeFile,    ss.wholeFile.load());
    setB (IDs::statistical,  ss.statisticalMode.load());
    setB (IDs::spectral,     ss.spectralMode.load());
    setB (IDs::hissFilter,   ss.hissFilter.load());
    for (int b = 0; b < kEqBands; ++b)
    {
        setB (ParameterState::eqId (b + 1, "On"), ss.eqBandOn[b].load());
        if (auto* p = apvts.getParameter (ParameterState::eqId (b + 1, "Type")))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) ss.eqBandType[b].load()));
        setF (ParameterState::eqId (b + 1, "Freq"), ss.eqBandFreq[b].load());
        setF (ParameterState::eqId (b + 1, "Gain"), ss.eqBandGain[b].load());
        setF (ParameterState::eqId (b + 1, "Q"),    ss.eqBandQ[b].load());
    }
    setF (IDs::spectralBands, (float) ss.spectralBands.load());
    setB (IDs::spectralAdv,   ss.spectralAdvanced.load());

    // Auto/Manual is not an APVTS parameter (it drives the waveform selection UI).
    manualMode_ = ss.manualMode.load();
    autoBtn_.setToggleState   (! manualMode_, juce::dontSendNotification);
    manualBtn_.setToggleState (  manualMode_, juce::dontSendNotification);
}

void MainView::timerCallback()
{
   #if TONEFILL_ARA_AVAILABLE
    // Follow the host's clip selection: this editor is one persistent instance the host re-points
    // at whichever clip is selected. Until it resolves, the knobs below would write where no worker
    // reads; after a clip switch, they must retarget the newly selected clip's state.
    processor_.tryResolveSharedState (true);
   #endif

    auto& apvts = processor_.parameters().apvts;
    auto& ss = processor_.sessionState();

    // The editor re-pointed at another clip (one persistent editor instance follows the host's
    // selection). Load THIS clip's parameters into the knobs, otherwise the mirror below would
    // stamp the previous clip's knob values onto it -- a new clip would inherit the old settings.
    // Return this tick: getRawParameterValue may not reflect the just-set values yet, and mirroring
    // it back would clobber. The read-only UI (meters/wave/tips) catches up 66 ms later.
    const bool realClip = (&ss != processor_.ownStatePtr());
    if (shownState_ != &ss)
    {
        shownState_ = &ss;
        if (waveWin_ != nullptr) waveWin_.reset();
        if (realClip)
        {
            // Load this clip's own parameters into the knobs: defaults for a fresh clip, or the
            // values an ARA archive restore put there for a reloaded one. A plain (non-ARA) insert
            // stays on ownState, so the knobs keep being driven straight from the APVTS instead.
            loadParamsFromState (ss);
            loadedEpoch_ = ss.paramsEpoch.load();
            return;
        }
    }
    else if (realClip && ss.paramsEpoch.load() != loadedEpoch_)
    {
        // An ARA archive restore landed after we were already showing this clip: adopt its values.
        loadParamsFromState (ss);
        loadedEpoch_ = ss.paramsEpoch.load();
        return;
    }

    bool changed = false;
    auto mirror = [&changed] (std::atomic<float>& dst, float v) { if (std::abs (dst.load() - v) > 1.0e-4f) { dst.store (v); changed = true; } };
    auto mirrorB = [&changed] (std::atomic<bool>& dst, bool v) { if (dst.load() != v) { dst.store (v); changed = true; } };
    mirror (ss.threshold,    apvts.getRawParameterValue (IDs::threshold)->load());
    mirror (ss.speechReject, apvts.getRawParameterValue (IDs::speechReject)->load());
    mirror (ss.fragment,     apvts.getRawParameterValue (IDs::fragment)->load());
    mirror (ss.blend,        apvts.getRawParameterValue (IDs::blend)->load());
    mirror (ss.randomness,   apvts.getRawParameterValue (IDs::randomness)->load());
    mirror (ss.minFill,      apvts.getRawParameterValue (IDs::minFill)->load());
    mirror (ss.flatness,     apvts.getRawParameterValue (IDs::flatness)->load());
    mirrorB (ss.paulStretch, apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f);
    mirrorB (ss.spectralMode, apvts.getRawParameterValue (IDs::spectral)->load() > 0.5f); // re-render on toggle
    { const int nb = (int) std::lround (apvts.getRawParameterValue (IDs::spectralBands)->load());
      if (ss.spectralBands.load() != nb) { ss.spectralBands.store (nb); changed = true; } } // re-render on band count
    mirrorB (ss.spectralAdvanced, apvts.getRawParameterValue (IDs::spectralAdv)->load() > 0.5f);
    if (changed) ss.generation.fetch_add (1);
    ss.outputGain.store (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (IDs::outputGain)->load()));
    ss.renderLength.store (apvts.getRawParameterValue (IDs::renderLength)->load());
    ss.bypass.store (apvts.getRawParameterValue (IDs::bypass)->load() > 0.5f); // A/B monitor, no re-render

    const bool normOn  = apvts.getRawParameterValue (IDs::normEnabled)->load() > 0.5f;
    const bool normLufs = apvts.getRawParameterValue (IDs::normUnit)->load() > 0.5f;
    ss.normalizeEnabled.store (normOn);
    ss.normalizeTarget.store (apvts.getRawParameterValue (IDs::normTarget)->load());
    ss.normalizeLufs.store (normLufs);
    ss.wholeFile.store (apvts.getRawParameterValue (IDs::wholeFile)->load() > 0.5f);

    // Hiss filter is live (audio thread) - mirror WITHOUT bumping generation. Active only in Enhance.
    const bool enh    = apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    const bool hissOn = apvts.getRawParameterValue (IDs::hissFilter)->load() > 0.5f;
    ss.hissFilter.store (hissOn); // parametric EQ master enable
    // Mirror the EQ band params (APVTS -> SessionState atomics the audio thread reads). Live, no re-render.
    for (int b = 0; b < kEqBands; ++b)
    {
        ss.eqBandOn[b].store   (apvts.getRawParameterValue (ParameterState::eqId (b + 1, "On"))->load() > 0.5f);
        ss.eqBandType[b].store ((int) std::lround (apvts.getRawParameterValue (ParameterState::eqId (b + 1, "Type"))->load()));
        ss.eqBandFreq[b].store (apvts.getRawParameterValue (ParameterState::eqId (b + 1, "Freq"))->load());
        ss.eqBandGain[b].store (apvts.getRawParameterValue (ParameterState::eqId (b + 1, "Gain"))->load());
        ss.eqBandQ[b].store    (apvts.getRawParameterValue (ParameterState::eqId (b + 1, "Q"))->load());
    }
    hissBtn_.setEnabled (enh);
    hissBtn_.setAlpha (enh ? 1.0f : 0.4f);
    // The EQ editor shows only while Enhance AND EQ are on; toggling reflows the action card.
    const bool hissShow = enh && hissOn;
    if (eqView_ != nullptr)
    {
        if (eqView_->isVisible() != hissShow) { eqView_->setVisible (hissShow); resized(); }
        if (hissShow) eqView_->refresh();
    }

    const bool stat = apvts.getRawParameterValue (IDs::statistical)->load() > 0.5f;
    const bool spec = apvts.getRawParameterValue (IDs::spectral)->load() > 0.5f;
    ss.statisticalMode.store (stat);
    classicBtn_.setToggleState  (! stat && ! spec, juce::dontSendNotification);
    expBtn_.setToggleState      (stat && ! spec,   juce::dontSendNotification);
    spectralBtn_.setToggleState (spec,             juce::dontSendNotification);

    // Bands knob is only meaningful in Spectral mode.
    bands_.slider.setEnabled (spec); bands_.label.setEnabled (spec);
    bands_.slider.setAlpha (spec ? 1.0f : 0.4f); bands_.label.setAlpha (spec ? 1.0f : 0.4f);

    // Advanced (band-range editor) is a Spectral sub-option.
    advBtn_.setEnabled (spec); advBtn_.setAlpha (spec ? 1.0f : 0.4f);

    gain_.slider.setEnabled (! normOn);
    normUnit_.setEnabled (normOn);
    normValueLbl_.setText (juce::String (normTarget_.getValue(), 1) + (normLufs ? " LUFS" : " dBFS"), juce::dontSendNotification);
    for (auto* c : { (juce::Component*) &normMinus_, (juce::Component*) &normPlus_, (juce::Component*) &normValueLbl_ })
    {
        c->setEnabled (normOn);
        c->setAlpha (normOn ? 1.0f : 0.5f);
    }
    const float meas = normLufs ? ss.measuredLufs.load() : ss.measuredPeakDb.load();
    normReadout_.setText (meas > -119.0f ? "now " + juce::String (meas, 1) + (normLufs ? " LUFS" : " dBFS") : "", juce::dontSendNotification);

    updateEmphasis();
    if (normOn) { gain_.slider.setAlpha (0.34f); gain_.label.setAlpha (0.34f); }

    // Rotate the tip every ~30 s (timer is 15 Hz -> 450 ticks).
    if (++tipTick_ >= 450) { tipTick_ = 0; tipIdx_ = (tipIdx_ + 1) % (int) tips_.size(); tipLbl_.setText (tips_[(std::size_t) tipIdx_], juce::dontSendNotification); }

    wave_ = ss.getWave();
    if (! dragging_) selections_ = ss.getManualRanges();
    meterDb_ = meterDb_ * 0.7f + ss.outMeterDb.load() * 0.3f;
    repaint();
}

void MainView::exportWav()
{
    double sr = 48000.0;
    auto fill = processor_.sessionState().getExportFill (sr);
    if (fill == nullptr || fill->empty() || (*fill)[0].empty()) return;

    const float lengthSec = processor_.sessionState().renderLength.load();
    const int loopLen = (int) (*fill)[0].size();
    const int outLen = juce::jmax (1, (int) std::lround (lengthSec * sr));

    auto& ss = processor_.sessionState();
    const bool  normOn     = ss.normalizeEnabled.load();
    const bool  normLufs   = ss.normalizeLufs.load();
    const float normTarget = ss.normalizeTarget.load();

    chooser_ = std::make_unique<juce::FileChooser> ("Export ToneFill ambience", juce::File(), "*.wav");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                           [fill, sr, loopLen, outLen, normOn, normLufs, normTarget] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.withFileExtension ("wav").createOutputStream());
        if (stream == nullptr) return;
        const int numCh = (int) fill->size();
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, (unsigned int) numCh, 24, {}, 0));
        if (writer == nullptr) return;
        stream.release();

        std::vector<std::vector<float>> tiled ((std::size_t) numCh, std::vector<float> ((std::size_t) outLen));
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto& src = (*fill)[(std::size_t) ch];
            auto& dst = tiled[(std::size_t) ch];
            for (int i = 0; i < outLen; ++i) dst[(std::size_t) i] = src[(std::size_t) (i % loopLen)];
        }
        if (normOn)
        {
            const float meas = normLufs ? tonefill::dsp::integratedLufs (tiled, sr) : tonefill::dsp::peakDbfs (tiled);
            if (meas > -119.0f)
            {
                float gainDb = normTarget - meas;
                if (normLufs) { const float peakDb = tonefill::dsp::peakDbfs (tiled); const float np = peakDb + gainDb; if (np > -1.0f) gainDb -= (np + 1.0f); }
                const float gg = std::pow (10.0f, gainDb / 20.0f);
                if (std::fabs (gg - 1.0f) > 1.0e-4f) for (auto& c : tiled) for (auto& smp : c) smp *= gg;
            }
        }
        std::vector<const float*> ptrs;
        for (const auto& c : tiled) ptrs.push_back (c.data());
        writer->writeFromFloatArrays (ptrs.data(), numCh, outLen);
    });
}

int MainView::xToSample (int x) const
{
    const int n = processor_.sessionState().sourceSamples.load();
    if (n <= 0 || waveArea_.getWidth() <= 0) return 0;
    const float f = (float) (x - waveArea_.getX()) / (float) waveArea_.getWidth();
    return juce::jlimit (0, n, (int) std::lround (f * (float) n));
}

float MainView::sampleToX (int sample) const
{
    const int n = processor_.sessionState().sourceSamples.load();
    if (n <= 0) return (float) waveArea_.getX();
    return (float) waveArea_.getX() + (float) juce::jlimit (0, n, sample) / (float) n * (float) waveArea_.getWidth();
}

void MainView::pushSelections()
{
    std::sort (selections_.begin(), selections_.end());
    std::vector<std::pair<int, int>> merged;
    for (const auto& s : selections_)
    {
        if (! merged.empty() && s.first <= merged.back().second) merged.back().second = juce::jmax (merged.back().second, s.second);
        else merged.push_back (s);
    }
    selections_ = merged;
    processor_.sessionState().setManualRanges (selections_);
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (! manualMode_ || ! waveArea_.contains (e.getPosition()) || processor_.sessionState().sourceSamples.load() <= 0) return;
    dragging_ = true;
    dragStartSample_ = dragCurSample_ = xToSample (e.x);
    repaint();
}

void MainView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    dragCurSample_ = xToSample (e.x);
    repaint();
}

void MainView::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    dragging_ = false;
    const int a = juce::jmin (dragStartSample_, dragCurSample_), b = juce::jmax (dragStartSample_, dragCurSample_);
    const float px = sampleToX (b) - sampleToX (a);
    if (px < 4.0f)
    {
        const int s = xToSample (e.x);
        const auto before = selections_.size();
        selections_.erase (std::remove_if (selections_.begin(), selections_.end(),
                                           [s] (const std::pair<int, int>& rr) { return s >= rr.first && s <= rr.second; }),
                           selections_.end());
        if (selections_.size() != before) pushSelections();
    }
    else { selections_.emplace_back (a, b); pushSelections(); }
    repaint();
}
} // namespace tonefill::plugin::ui
