#include "plugin/ui/MainView.h"
#include "plugin/ui/WaveformWindow.h"
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
    initKnob (*this, fragment_, "Chunk Size",
              "Length of each real chunk taken from the source. Longer = more natural texture, shorter = smoother but more repetitive.", LNF::purple());
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
    frA_ = std::make_unique<SA> (apvts, IDs::fragment,     fragment_.slider);
    blA_ = std::make_unique<SA> (apvts, IDs::blend,        blend_.slider);
    vaA_ = std::make_unique<SA> (apvts, IDs::randomness,   variation_.slider);
    mfA_ = std::make_unique<SA> (apvts, IDs::minFill,      minFill_.slider);
    flA_ = std::make_unique<SA> (apvts, IDs::flatness,     flatness_.slider);
    gaA_ = std::make_unique<SA> (apvts, IDs::outputGain,   gain_.slider);
    leA_ = std::make_unique<SA> (apvts, IDs::renderLength, length_.slider);

    // Learn-source mode: Auto vs Manual (one segmented section).
    for (auto* b : { &autoBtn_, &manualBtn_ }) { b->setClickingTogglesState (true); b->setRadioGroupId (200); addAndMakeVisible (*b); }
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
    wholeBtn_.setTooltip ("Analyze the WHOLE item to find clean room tone scattered across a long take. Off = the first 4 minutes.");
    addAndMakeVisible (wholeBtn_);
    wfA_ = std::make_unique<BA> (apvts, IDs::wholeFile, wholeBtn_);

    // Selection engine: Classic vs Experimental (statistical scoring). Driven manually on the param.
    for (auto* b : { &classicBtn_, &expBtn_ }) { b->setClickingTogglesState (false); addAndMakeVisible (*b); }
    classicBtn_.setTooltip ("Classic selection: the tuned gate stack (default).");
    expBtn_.setTooltip ("Experimental selection: weighted per-frame scoring. A/B against Classic by ear.");
    if (auto* sp = apvts.getParameter (IDs::statistical))
    {
        classicBtn_.onClick = [sp] { sp->setValueNotifyingHost (0.0f); };
        expBtn_.onClick     = [sp] { sp->setValueNotifyingHost (1.0f); };
    }

    expandBtn_.setTooltip ("Open a large waveform view with a timecode ruler, zoom and scroll for precise selecting.");
    expandBtn_.onClick = [this] { openWaveformWindow(); };
    addAndMakeVisible (expandBtn_);

    regenBtn_.setTooltip ("New random variation of the fill (same settings).");
    regenBtn_.onClick = [this] { auto& ss = processor_.sessionState(); ss.seed.fetch_add (0x9E3779B97F4A7C15ULL); ss.generation.fetch_add (1); };
    addAndMakeVisible (regenBtn_);
    exportBtn_.getProperties().set ("accent", true);
    exportBtn_.setTooltip ("Write the fill to a WAV file. Length is set by Export Len.");
    exportBtn_.onClick = [this] { exportWav(); };
    addAndMakeVisible (exportBtn_);

    normBtn_.setClickingTogglesState (true);
    normBtn_.setTooltip ("Bake the output to a fixed loudness target. When on, Output is bypassed.");
    addAndMakeVisible (normBtn_);
    neA_ = std::make_unique<BA> (apvts, IDs::normEnabled, normBtn_);

    normTarget_.setSliderStyle (juce::Slider::IncDecButtons);
    normTarget_.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 24);
    normTarget_.setRange (-60.0, 0.0, 0.1);
    normTarget_.setColour (juce::Slider::textBoxTextColourId, LNF::navy());
    normTarget_.setTooltip ("Target level to normalize to. Peak dBFS, or integrated LUFS.");
    addAndMakeVisible (normTarget_);
    ntA_ = std::make_unique<SA> (apvts, IDs::normTarget, normTarget_);

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
    hissBtn_.setTooltip ("Enhance only: a live low-pass that removes the HF hiss PaulStretch can add. "
                         "Freq sets the corner (3-15 kHz), Q the slope.");
    addAndMakeVisible (hissBtn_);
    hbA_ = std::make_unique<BA> (apvts, IDs::hissFilter, hissBtn_);
    initKnob (*this, hissFreq_, "Freq", "De-hiss corner frequency (3-15 kHz). Lower = more aggressive HF cut.", LNF::purple());
    hissFreq_.slider.setTextValueSuffix (" Hz");
    initKnob (*this, hissQ_, "Q", "De-hiss filter Q (slope / resonance at the corner).", LNF::purple());
    hfrA_ = std::make_unique<SA> (apvts, IDs::hissFreq, hissFreq_.slider);
    hqA_  = std::make_unique<SA> (apvts, IDs::hissQ,    hissQ_.slider);

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

void MainView::updateEmphasis()
{
    auto set = [] (Knob& k, bool on) { const float a = on ? 1.0f : 0.34f; k.slider.setAlpha (a); k.label.setAlpha (a); };
    const bool enhance = processor_.parameters().apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    set (threshold_, true); set (speech_, true); set (minFill_, true); set (flatness_, true);
    set (fragment_, ! enhance); set (blend_, ! enhance);
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
    groupBg (classicBtn_.getBounds(), expBtn_.getBounds());

    // Three group cards + headers.
    const char* titles[3] = { "DETECTION", "STRUCTURE", "TEXTURE" };
    const int   icons[3]  = { GiDetect, GiStruct, GiTexture };
    for (int i = 0; i < 3; ++i)
    {
        const auto cf = groupCard_[(std::size_t) i].toFloat();
        g.setColour (LNF::panel());     g.fillRoundedRectangle (cf, 12.0f);
        g.setColour (LNF::line());      g.drawRoundedRectangle (cf, 12.0f, 1.0f);
        const float hy = cf.getY() + 18.0f;
        g.setFont (juce::Font (11.0f));
        const int labW = juce::Font (11.0f).getStringWidth (titles[i]);
        const float total = (float) labW + 24.0f;
        const float lx = cf.getCentreX() - total * 0.5f;
        drawGroupIcon (g, { lx, hy - 7.0f, 14.0f, 14.0f }, icons[i], LNF::purple());
        g.setColour (LNF::label());
        g.drawText (titles[i], (int) (lx + 20.0f), (int) (hy - 7.0f), labW + 10, 14, juce::Justification::centredLeft, false);
        g.setColour (LNF::lineSoft());
        g.fillRect (cf.getX() + 14.0f, hy + 12.0f, cf.getWidth() - 28.0f, 1.0f);
    }

    // Knob value pills.
    for (const auto& pill : valuePills_)
    {
        g.setColour (LNF::panel());
        g.fillRoundedRectangle (pill.toFloat(), 6.0f);
        g.setColour (LNF::line());
        g.drawRoundedRectangle (pill.toFloat(), 6.0f, 1.0f);
    }

    // Vertical output meter.
    {
        const auto mf = meterArea_.toFloat();
        g.setColour (LNF::panel());  g.fillRoundedRectangle (mf, 12.0f);
        g.setColour (LNF::line());   g.drawRoundedRectangle (mf, 12.0f, 1.0f);
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
                juce::Colour col = frac > 0.82f ? LNF::coral() : frac > 0.6f ? LNF::purple() : navy.withAlpha (0.85f);
                g.setColour (lit ? col : LNF::lineSoft());
                g.fillRoundedRectangle (bx, by, bw, sh, 2.0f);
            }
        }
        g.setColour (navy);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (meterDb_, 1) + " dB", mf.withTop (mf.getBottom() - 16.0f).withHeight (14.0f),
                    juce::Justification::centred, false);
    }

    // Normalize card background.
    {
        g.setColour (LNF::panel());  g.fillRoundedRectangle (normCard_.toFloat(), 12.0f);
        g.setColour (LNF::line());   g.drawRoundedRectangle (normCard_.toFloat(), 12.0f, 1.0f);
    }

    // Hiss filter card (separate panel under Texture). Title dims when Enhance is off.
    {
        const bool enh = processor_.parameters().apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
        g.setColour (LNF::panel());  g.fillRoundedRectangle (hissCard_.toFloat(), 12.0f);
        g.setColour (LNF::line());   g.drawRoundedRectangle (hissCard_.toFloat(), 12.0f, 1.0f);
        g.setColour (enh ? LNF::coral() : LNF::muted());
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText ("HISS FILTER", hissCard_.getX() + 14, hissCard_.getY() + 6, 140, 12, juce::Justification::centredLeft, false);
        if (! enh)
        {
            g.setColour (LNF::muted());
            g.setFont (juce::Font (10.0f));
            g.drawText ("Enhance only", hissCard_.getRight() - 96, hissCard_.getY() + 6, 82, 12, juce::Justification::centredRight, false);
        }
    }

    // Tip card.
    {
        g.setColour (LNF::panel());  g.fillRoundedRectangle (tipCard_.toFloat(), 12.0f);
        g.setColour (LNF::line());   g.drawRoundedRectangle (tipCard_.toFloat(), 12.0f, 1.0f);
        g.setColour (LNF::accent());
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText ("TIP", tipCard_.getX() + 14, tipCard_.getY() + 8, 40, 14, juce::Justification::centredLeft, false);
    }

    // Bottom panel: waveform + data + status.
    {
        g.setColour (LNF::panel());  g.fillRoundedRectangle (bottomCard_.toFloat(), 12.0f);
        g.setColour (LNF::line());   g.drawRoundedRectangle (bottomCard_.toFloat(), 12.0f, 1.0f);
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
    g.setColour (LNF::good());
    g.fillEllipse ((float) dataArea_.getX(), (float) dataArea_.getBottom() + 6.0f, 8.0f, 8.0f);
    g.setColour (LNF::muted());
    g.setFont (juce::Font (11.0f));
    g.drawText ("READY   ·   " + juce::String (sr / 1000.0, 1) + " kHz   ·   24-bit   ·   " + formatClock (totalSec),
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
        auto engine = head.removeFromRight (196).withSizeKeepingCentre (196, 26);
        place (classicBtn_, expBtn_, engine);
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
    Knob* groups[3][3] = { { &threshold_, &speech_, &minFill_ }, { &flatness_, &fragment_, &blend_ }, { &variation_, &gain_, &length_ } };
    for (int i = 0; i < 3; ++i)
    {
        auto card = main.removeFromLeft (i < 2 ? cardW : main.getWidth());
        if (i < 2) main.removeFromLeft (gap);
        groupCard_[(std::size_t) i] = card;
        auto body = card.reduced (8, 0).withTrimmedTop (34);
        const int kw = body.getWidth() / 3;
        for (int j = 0; j < 3; ++j)
        {
            auto cell = body.removeFromLeft (j < 2 ? kw : body.getWidth());
            auto inner = cell.reduced (4, 6);
            inner.removeFromTop (0);
            auto lab = inner.removeFromTop (14);
            groups[i][j]->label.setBounds (lab);
            groups[i][j]->slider.setBounds (inner);
            valuePills_.push_back (inner.removeFromBottom (20).reduced (10, 1));
        }
    }
    r.removeFromTop (12);

    // Normalize + Hiss row: Normalize ends flush with the Structure card's right edge; the Hiss
    // panel starts flush with the Texture card's left edge (the gap between them separates them).
    auto normRow = r.removeFromTop (60);
    normCard_ = normRow.withRight (groupCard_[1].getRight());
    hissCard_ = normRow.withLeft (groupCard_[2].getX());
    {
        auto nr = normCard_.reduced (14, 0);
        normBtn_.setBounds (nr.removeFromLeft (100).withSizeKeepingCentre (100, 28));
        nr.removeFromLeft (10);
        normTarget_.setBounds (nr.removeFromLeft (124).withSizeKeepingCentre (124, 28));
        nr.removeFromLeft (10);
        normUnit_.setBounds (nr.removeFromLeft (72).withSizeKeepingCentre (72, 28));
        nr.removeFromLeft (10);
        normReadout_.setBounds (nr.withSizeKeepingCentre (nr.getWidth(), 24)); // "now" next to the unit picker
    }
    {
        auto hr = hissCard_.reduced (12, 6).withTrimmedTop (12); // leave room for the HISS FILTER title
        hissBtn_.setBounds (hr.removeFromLeft (96).withSizeKeepingCentre (96, 26));
        hr.removeFromLeft (8);
        auto placeMini = [] (Knob& k, juce::Rectangle<int> cell)
        {
            k.label.setBounds (cell.removeFromTop (11));
            k.slider.setBounds (cell);
        };
        const int kw = hr.getWidth() / 2;
        placeMini (hissFreq_, hr.removeFromLeft (kw));
        placeMini (hissQ_, hr);
    }
    r.removeFromTop (12);

    // Tip row + Regenerate / Export.
    auto tipRow = r.removeFromTop (46);
    exportBtn_.setBounds (tipRow.removeFromRight (150).withSizeKeepingCentre (150, 40));
    tipRow.removeFromRight (10);
    regenBtn_.setBounds (tipRow.removeFromRight (130).withSizeKeepingCentre (130, 40));
    tipRow.removeFromRight (12);
    tipCard_ = tipRow;
    tipLbl_.setBounds (tipCard_.reduced (14, 0).withTrimmedLeft (34));
    r.removeFromTop (12);

    // Bottom panel.
    bottomCard_ = r;
    auto bc = bottomCard_.reduced (14, 12);
    auto topStrip = bc.removeFromTop (18);
    expandBtn_.setBounds (topStrip.removeFromRight (90).withSizeKeepingCentre (90, 22));
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
    setB (IDs::hissFilter,   ss.hissFilter.load());
    setF (IDs::hissFreq,     ss.hissFreq.load());
    setF (IDs::hissQ,        ss.hissQ.load());

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
    if (changed) ss.generation.fetch_add (1);
    ss.outputGain.store (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (IDs::outputGain)->load()));
    ss.renderLength.store (apvts.getRawParameterValue (IDs::renderLength)->load());

    const bool normOn  = apvts.getRawParameterValue (IDs::normEnabled)->load() > 0.5f;
    const bool normLufs = apvts.getRawParameterValue (IDs::normUnit)->load() > 0.5f;
    ss.normalizeEnabled.store (normOn);
    ss.normalizeTarget.store (apvts.getRawParameterValue (IDs::normTarget)->load());
    ss.normalizeLufs.store (normLufs);
    ss.wholeFile.store (apvts.getRawParameterValue (IDs::wholeFile)->load() > 0.5f);

    // Hiss filter is live (audio thread) - mirror WITHOUT bumping generation. Active only in Enhance.
    const bool enh    = apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    const bool hissOn = apvts.getRawParameterValue (IDs::hissFilter)->load() > 0.5f;
    ss.hissFilter.store (hissOn);
    ss.hissFreq.store (apvts.getRawParameterValue (IDs::hissFreq)->load());
    ss.hissQ.store (apvts.getRawParameterValue (IDs::hissQ)->load());
    hissBtn_.setEnabled (enh);
    hissFreq_.slider.setEnabled (enh && hissOn); hissFreq_.label.setEnabled (enh && hissOn);
    hissQ_.slider.setEnabled (enh && hissOn);    hissQ_.label.setEnabled (enh && hissOn);
    const float hissA = enh ? 1.0f : 0.4f;
    hissBtn_.setAlpha (hissA);
    hissFreq_.slider.setAlpha (enh && hissOn ? 1.0f : 0.4f); hissFreq_.label.setAlpha (hissA);
    hissQ_.slider.setAlpha (enh && hissOn ? 1.0f : 0.4f);    hissQ_.label.setAlpha (hissA);

    const bool stat = apvts.getRawParameterValue (IDs::statistical)->load() > 0.5f;
    ss.statisticalMode.store (stat);
    classicBtn_.setToggleState (! stat, juce::dontSendNotification);
    expBtn_.setToggleState (stat, juce::dontSendNotification);

    gain_.slider.setEnabled (! normOn);
    normTarget_.setEnabled (normOn);
    normUnit_.setEnabled (normOn);
    normTarget_.setTextValueSuffix (normLufs ? " LUFS" : " dBFS");
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
