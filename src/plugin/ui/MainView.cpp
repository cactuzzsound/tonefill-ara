#include "plugin/ui/MainView.h"
#include "plugin/ui/WaveformWindow.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ParameterState.h"
#include "dsp/Loudness.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace tonefill::plugin::ui
{
using IDs = ParameterState::IDs;

namespace
{
juce::String formatClock (double seconds)
{
    if (seconds < 0) seconds = 0;
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String::formatted ("%d:%04.1f", m, s);
}

void initKnob (juce::Component& parent, juce::Slider& s, juce::Label& l,
               const juce::String& name, const juce::String& tip)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    s.setRange (0.0, 1.0, 0.001);
    s.setTooltip (tip);
    parent.addAndMakeVisible (s);
    l.setText (name.toUpperCase(), juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setFont (juce::Font (11.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, ToneFillLookAndFeel::muted());
    l.setTooltip (tip);
    parent.addAndMakeVisible (l);
}
} // namespace

MainView::MainView (PluginProcessor& processor) : processor_ (processor)
{
    setLookAndFeel (&lnf_);
    auto& apvts = processor_.parameters().apvts;

    titleLbl_.setText ("ToneFill", juce::dontSendNotification);
    titleLbl_.setFont (juce::Font (17.0f, juce::Font::bold));
    titleLbl_.setColour (juce::Label::textColourId, juce::Colour (0xffe6e9ee));
    addAndMakeVisible (titleLbl_);
    subLbl_.setText ("dialogue room tone fill", juce::dontSendNotification);
    subLbl_.setFont (juce::Font (12.0f));
    subLbl_.setColour (juce::Label::textColourId, ToneFillLookAndFeel::muted());
    addAndMakeVisible (subLbl_);

    initKnob (*this, threshold_.slider, threshold_.label, "Clean Level",
              "Auto mode: how quiet a moment must be to count as room tone. Lower = stricter, rejects claps, clicks and loud bits.");
    initKnob (*this, speech_.slider,    speech_.label,    "Voice Reject",
              "How hard to exclude voice / breaths / mouth noise from the learned material. Higher = cleaner room tone (push high; keep Clean Level low).");
    initKnob (*this, fragment_.slider,  fragment_.label,  "Chunk Size",
              "Length of each real chunk taken from the source. Longer = more natural texture, shorter = smoother but more repetitive.");
    initKnob (*this, blend_.slider,     blend_.label,     "Crossfade",
              "How much neighbouring chunks overlap and blend together. Higher = smoother joins.");
    initKnob (*this, variation_.slider, variation_.label, "Smoothness",
              "Enhance only: resynthesis window size. Higher = smoother, more diffuse; lower = keeps more of the source's fine texture.");
    initKnob (*this, minFill_.slider,   minFill_.label,   "Min Fill",
              "Minimum length a stable audio fragment must have to be used in the fill. Longer = fewer, more consistent chunks and fewer audible joins.");
    minFill_.slider.setRange (0.2, 5.0, 0.05);
    minFill_.slider.setTextValueSuffix (" s");
    initKnob (*this, flatness_.slider,  flatness_.label,  "Flatness",
              "How strict the stationarity requirement is. Higher = only very steady stretches, so the fill avoids audible jumps and crossfades.");
    initKnob (*this, gain_.slider,      gain_.label,      "Output",
              "Output volume, in dB.");
    gain_.slider.setRange (-24.0, 24.0, 0.1);
    initKnob (*this, length_.slider,    length_.label,    "Export Len",
              "Length of the WAV written by Export, in seconds. The loop is tiled seamlessly to this length.");
    length_.slider.setRange (0.5, 30.0, 0.1);
    length_.slider.setTextValueSuffix (" s");

    threshold_.icon = IcSparkle; speech_.icon    = IcDialog;  minFill_.icon  = IcWave;
    flatness_.icon  = IcSine;    fragment_.icon  = IcTarget;  blend_.icon    = IcCross;
    variation_.icon = IcShuffle; gain_.icon      = IcSliders; length_.icon   = IcClock;

    thA_ = std::make_unique<SA> (apvts, IDs::threshold,      threshold_.slider);
    spA_ = std::make_unique<SA> (apvts, IDs::speechReject,   speech_.slider);
    frA_ = std::make_unique<SA> (apvts, IDs::fragment,       fragment_.slider);
    blA_ = std::make_unique<SA> (apvts, IDs::blend,          blend_.slider);
    vaA_ = std::make_unique<SA> (apvts, IDs::randomness,     variation_.slider);
    mfA_ = std::make_unique<SA> (apvts, IDs::minFill,        minFill_.slider);
    flA_ = std::make_unique<SA> (apvts, IDs::flatness,       flatness_.slider);
    gaA_ = std::make_unique<SA> (apvts, IDs::outputGain,     gain_.slider);
    leA_ = std::make_unique<SA> (apvts, IDs::renderLength,   length_.slider);

    // Learn-source mode: Auto (plugin finds clean ambience) vs Manual (user selects regions).
    for (auto* b : { &autoBtn_, &manualBtn_ })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (200);
        addAndMakeVisible (*b);
    }
    autoBtn_.setToggleState (true, juce::dontSendNotification);
    autoBtn_.setTooltip ("Plugin finds clean room tone automatically.");
    manualBtn_.setTooltip ("You pick the room-tone regions yourself: drag on the waveform below to add, click a region to remove.");
    autoBtn_.onClick   = [this] { manualMode_ = false; processor_.sessionState().manualMode.store (false); repaint(); };
    manualBtn_.onClick = [this] { manualMode_ = true;  processor_.sessionState().manualMode.store (true);  repaint(); };

    wholeBtn_.setClickingTogglesState (true);
    wholeBtn_.setTooltip ("Analyze the WHOLE item to find clean room tone scattered across a long take "
                          "(slower, more memory). Off = just the first 4 minutes.");
    addAndMakeVisible (wholeBtn_);
    wfA_ = std::make_unique<BA> (apvts, IDs::wholeFile, wholeBtn_);

    waveBtn_.setTooltip ("Open a large waveform view with a timecode ruler, zoom and scroll for precise selecting.");
    waveBtn_.onClick = [this] { openWaveformWindow(); };
    addAndMakeVisible (waveBtn_);

    enhanceBtn_.setClickingTogglesState (true);
    enhanceBtn_.setTooltip ("Extra smoothing resynthesis for when the selected fragments won't blend into a clean bed. "
                            "Off = real-audio bed (Chunk Size / Crossfade). On = enables Smoothness and Mix.");
    addAndMakeVisible (enhanceBtn_);
    enA_ = std::make_unique<BA> (apvts, IDs::paulStretch, enhanceBtn_);

    regenBtn_.setTooltip ("New random variation of the fill (same settings).");
    regenBtn_.onClick = [this] { auto& ss = processor_.sessionState(); ss.seed.fetch_add (0x9E3779B97F4A7C15ULL); ss.generation.fetch_add (1); };
    addAndMakeVisible (regenBtn_);
    exportBtn_.getProperties().set ("accent", true);
    exportBtn_.setTooltip ("Write the fill to a WAV file. Length is set by Export Len.");
    exportBtn_.onClick = [this] { exportWav(); };
    addAndMakeVisible (exportBtn_);

    // Loudness normalize row.
    normBtn_.setClickingTogglesState (true);
    normBtn_.setTooltip ("Bake the output to a fixed loudness target. When on, Output is bypassed and "
                         "the render (and Export) come out at the value below.");
    addAndMakeVisible (normBtn_);
    neA_ = std::make_unique<BA> (apvts, IDs::normEnabled, normBtn_);

    normTarget_.setSliderStyle (juce::Slider::IncDecButtons);
    normTarget_.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 20);
    normTarget_.setRange (-60.0, 0.0, 0.1);
    normTarget_.setTooltip ("Target level to normalize to (type a value). Peak dBFS, or integrated LUFS.");
    addAndMakeVisible (normTarget_);
    ntA_ = std::make_unique<SA> (apvts, IDs::normTarget, normTarget_);

    normUnit_.addItem ("dBFS", 1);
    normUnit_.addItem ("LUFS", 2);
    normUnit_.setTooltip ("dBFS = normalize the peak. LUFS = normalize integrated loudness (EBU R128).");
    addAndMakeVisible (normUnit_);
    nuA_ = std::make_unique<CBA> (apvts, IDs::normUnit, normUnit_);

    normReadout_.setFont (juce::Font (11.0f));
    normReadout_.setColour (juce::Label::textColourId, ToneFillLookAndFeel::muted());
    normReadout_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (normReadout_);

    startTimerHz (15);
}

MainView::~MainView() { stopTimer(); setLookAndFeel (nullptr); }

void MainView::openWaveformWindow()
{
    if (waveWin_ == nullptr)
    {
        waveWin_ = std::make_unique<WaveformWindow> (processor_.sessionState());
        waveWin_->onClose = [this] { waveWin_.reset(); };
    }
    else
        waveWin_->toFront (true);
}

void MainView::updateEmphasis()
{
    auto set = [] (Knob& k, bool on) { const float a = on ? 1.0f : 0.36f; k.slider.setAlpha (a); k.label.setAlpha (a); };
    const bool enhance = processor_.parameters().apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f;
    set (threshold_, true); set (speech_, true);
    set (minFill_, true);   set (flatness_, true);        // stability selection (always)
    set (fragment_, ! enhance); set (blend_, ! enhance);  // grain-cloud shaping (Enhance OFF)
    set (variation_, enhance);                            // Smoothness (Enhance ON)
    set (gain_, true); set (length_, true);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (ToneFillLookAndFeel::bg());

    // Knob cards: rounded panel + border + top-left icon + inset value box. Child sliders/labels
    // paint on top of these.
    for (const auto& cd : cards_)
    {
        const auto cf = cd.card.toFloat();
        g.setColour (ToneFillLookAndFeel::panel());
        g.fillRoundedRectangle (cf, 10.0f);
        g.setColour (ToneFillLookAndFeel::line());
        g.drawRoundedRectangle (cf, 10.0f, 1.0f);
        drawKnobIcon (g, { cf.getX() + 12.0f, cf.getY() + 11.0f, 14.0f, 14.0f }, cd.icon,
                      ToneFillLookAndFeel::dust());
        g.setColour (ToneFillLookAndFeel::panelHi());
        g.fillRoundedRectangle (cd.value.toFloat(), 6.0f);
    }

    // Waveform — ALWAYS the full source clip. Auto: green = selected clean ambience, gray =
    // rejected. Manual: bars under your selected regions light up green; the rest stay gray.
    g.setColour (juce::Colour (0xff16181c));
    g.fillRoundedRectangle (waveArea_.toFloat(), 6.0f);

    const int srcN = processor_.sessionState().sourceSamples.load();
    auto selectedBin = [&] (int i, int nBins) -> bool
    {
        if (srcN <= 0) return false;
        const int s0 = (int) ((long long) i * srcN / nBins);
        const int s1 = (int) ((long long) (i + 1) * srcN / nBins);
        for (const auto& sel : selections_)
            if (sel.first < s1 && sel.second > s0) return true;
        if (dragging_)
        {
            const int a = juce::jmin (dragStartSample_, dragCurSample_);
            const int b = juce::jmax (dragStartSample_, dragCurSample_);
            if (a < s1 && b > s0) return true;
        }
        return false;
    };

    if (! wave_.peak.empty())
    {
        const int   n     = (int) wave_.peak.size();
        const float w     = (float) waveArea_.getWidth();
        const float midY  = (float) waveArea_.getCentreY();
        const float halfH = ((float) waveArea_.getHeight() - 8.0f) * 0.5f;
        const float bw    = w / (float) n;

        // Faint centre line so the symmetric waveform reads as audio, not a meter.
        g.setColour (ToneFillLookAndFeel::line().withAlpha (0.35f));
        g.fillRect ((float) waveArea_.getX(), midY - 0.5f, w, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            // sqrt makes quiet room tone visible without saturating louder moments.
            const float amp = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, wave_.peak[(std::size_t) i])));
            const float h   = juce::jmax (1.0f, amp * halfH);
            const bool  lit = manualMode_ ? selectedBin (i, n)
                                          : (i < (int) wave_.clean.size() && wave_.clean[(std::size_t) i]);
            g.setColour (lit ? ToneFillLookAndFeel::accent() : ToneFillLookAndFeel::line());
            const float x = (float) waveArea_.getX() + (float) i * bw;
            g.fillRect (x, midY - h, juce::jmax (1.0f, bw + 0.5f), 2.0f * h); // mirrored, gapless = filled wave
        }
    }

    // Manual mode: edge outline on each selection + always-on hint so the context never vanishes.
    if (manualMode_)
    {
        auto edge = [&] (int s0, int s1, juce::Colour c)
        {
            const float x0 = sampleToX (juce::jmin (s0, s1));
            const float x1 = sampleToX (juce::jmax (s0, s1));
            g.setColour (c);
            g.drawRect (juce::Rectangle<float> { x0, (float) waveArea_.getY(),
                                                 juce::jmax (2.0f, x1 - x0), (float) waveArea_.getHeight() }, 1.0f);
        };
        for (const auto& sel : selections_) edge (sel.first, sel.second, ToneFillLookAndFeel::accent().withAlpha (0.9f));
        if (dragging_) edge (dragStartSample_, dragCurSample_, ToneFillLookAndFeel::accent());

        g.setColour (ToneFillLookAndFeel::muted());
        g.setFont (juce::Font (10.0f));
        const juce::String hint = selections_.empty()
            ? "drag across the clip to add room-tone regions"
            : juce::String (selections_.size()) + " region(s) · drag to add · click one to remove";
        g.drawText (hint, waveArea_.reduced (6, 0).removeFromTop (13), juce::Justification::centredLeft, false);
    }

    // Timecode ruler under the waveform (0:00 -> end of the analysed source, scaled to length).
    {
        const int srcN = processor_.sessionState().sourceSamples.load();
        const double sr = processor_.sessionState().sourceSampleRate.load();
        const double totalSec = (srcN > 0 && sr > 0) ? srcN / sr : 0.0;
        g.setColour (ToneFillLookAndFeel::muted());
        g.setFont (juce::Font (9.5f));
        if (totalSec > 0.0)
        {
            const int nLab = 6;
            for (int i = 0; i <= nLab; ++i)
            {
                const float x = waveRuler_.getX() + (float) i / nLab * waveRuler_.getWidth();
                g.setColour (ToneFillLookAndFeel::line());
                g.fillRect (x, (float) waveRuler_.getY(), 1.0f, 4.0f);
                g.setColour (ToneFillLookAndFeel::muted());
                const auto just = i == nLab ? juce::Justification::centredRight : juce::Justification::centredLeft;
                g.drawText (formatClock (totalSec * i / nLab),
                            juce::Rectangle<int> ((int) x - (i == nLab ? 60 : -2), waveRuler_.getY() + 3, 60, 10), just, false);
            }
        }
    }

    // Output meter.
    g.setColour (ToneFillLookAndFeel::panel());
    g.fillRoundedRectangle (meterArea_.toFloat(), 5.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb_ + 60.0f) / 60.0f);
    g.setColour (meterDb_ > -3.0f ? juce::Colours::orange : ToneFillLookAndFeel::accent());
    g.fillRoundedRectangle (meterArea_.toFloat().withWidth (meterArea_.getWidth() * norm), 5.0f);

    // Status line under the meter.
    auto& ss = processor_.sessionState();
    const int phase = ss.phase.load();
    const juce::String ph = phase == 0 ? "idle" : (phase == 1 ? "analyzing" : "ready");
    g.setColour (ToneFillLookAndFeel::muted());
    g.setFont (juce::Font (11.0f));
    g.drawText ("out " + juce::String (meterDb_, 1) + " dB", meterArea_.translated (meterArea_.getWidth() + 8, 0).withWidth (90),
                juce::Justification::centredLeft, false);
    const float seam = ss.seamRiskDb.load();
    const juce::String seamStr = seam > 0.05f
        ? "   ·   seam " + juce::String (seam, 1) + " dB" + (seam > 4.0f ? " ⚠" : "")
        : juce::String();
    g.drawText (ph + "   ·   used " + juce::String (ss.learnSeconds.load(), 1) + " s"
                + " (" + juce::String (ss.cleanChunks.load()) + " chunks)"
                + "   ·   avail " + juce::String (ss.availSeconds.load(), 1) + " s"
                + seamStr
                + "   ·   in " + juce::String (ss.levelDb.load(), 0) + " dB",
                getLocalBounds().removeFromBottom (20).reduced (16, 2), juce::Justification::centredLeft, false);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto head = r.removeFromTop (40);
    {
        auto toggle = head.removeFromRight (130).withSizeKeepingCentre (130, 24);
        autoBtn_.setBounds (toggle.removeFromLeft (65));
        manualBtn_.setBounds (toggle);
        head.removeFromRight (8);
        enhanceBtn_.setBounds (head.removeFromRight (74).withSizeKeepingCentre (74, 24));
        head.removeFromRight (8);
        wholeBtn_.setBounds (head.removeFromRight (48).withSizeKeepingCentre (48, 24));
        head.removeFromRight (8);
        waveBtn_.setBounds (head.removeFromRight (84).withSizeKeepingCentre (84, 24));
    }
    titleLbl_.setBounds (head.removeFromTop (22));
    subLbl_.setBounds (head);
    r.removeFromTop (4);

    waveArea_ = r.removeFromTop (46);
    waveRuler_ = r.removeFromTop (12);
    r.removeFromTop (8);

    cards_.clear();
    auto placeKnob = [this] (Knob* k, juce::Rectangle<int> cell)
    {
        auto inner = cell.reduced (12, 10);
        auto labelStrip = inner.removeFromTop (16);
        labelStrip.removeFromLeft (20);                          // room for the icon
        k->label.setBounds (labelStrip);
        inner.removeFromTop (2);
        const auto valueBox = inner.withTop (inner.getBottom() - 20);
        k->slider.setBounds (inner);                            // rotary + its text box
        cards_.push_back ({ cell, valueBox, k->icon });
    };
    auto rowCells = [&r] (int count)
    {
        const int gap = 10;
        auto row = r.removeFromTop (124);
        const int cw = (row.getWidth() - gap * (count - 1)) / count;
        std::vector<juce::Rectangle<int>> cells;
        for (int i = 0; i < count; ++i) { cells.push_back (row.removeFromLeft (cw)); if (i < count - 1) row.removeFromLeft (gap); }
        r.removeFromTop (10);
        return cells;
    };

    { auto c = rowCells (3); placeKnob (&threshold_, c[0]); placeKnob (&speech_,   c[1]); placeKnob (&minFill_, c[2]); }
    { auto c = rowCells (3); placeKnob (&flatness_,  c[0]); placeKnob (&fragment_, c[1]); placeKnob (&blend_,   c[2]); }
    { auto c = rowCells (3); placeKnob (&variation_, c[0]); placeKnob (&gain_,     c[1]); placeKnob (&length_,  c[2]); }

    r.removeFromTop (2);
    auto normRow = r.removeFromTop (24);
    normBtn_.setBounds (normRow.removeFromLeft (110));
    normRow.removeFromLeft (8);
    normTarget_.setBounds (normRow.removeFromLeft (120));
    normRow.removeFromLeft (8);
    normUnit_.setBounds (normRow.removeFromLeft (74));
    normRow.removeFromLeft (10);
    normReadout_.setBounds (normRow);
    r.removeFromTop (6);

    auto foot = r.removeFromTop (30);
    regenBtn_.setBounds (foot.removeFromLeft (120));
    foot.removeFromLeft (8);
    exportBtn_.setBounds (foot.removeFromLeft (120));
    foot.removeFromLeft (16);
    meterArea_ = foot.removeFromLeft (juce::jmax (60, foot.getWidth() - 96)).withSizeKeepingCentre (juce::jmax (60, foot.getWidth() - 96), 12);
}

void MainView::timerCallback()
{
    auto& apvts = processor_.parameters().apvts;
    auto& ss = processor_.sessionState();

    bool changed = false;
    auto mirror = [&changed] (std::atomic<float>& dst, float v) { if (std::abs (dst.load() - v) > 1.0e-4f) { dst.store (v); changed = true; } };
    auto mirrorB = [&changed] (std::atomic<bool>& dst, bool v) { if (dst.load() != v) { dst.store (v); changed = true; } };
    mirror (ss.threshold,      apvts.getRawParameterValue (IDs::threshold)->load());
    mirror (ss.speechReject,   apvts.getRawParameterValue (IDs::speechReject)->load());
    mirror (ss.fragment,       apvts.getRawParameterValue (IDs::fragment)->load());
    mirror (ss.blend,          apvts.getRawParameterValue (IDs::blend)->load());
    mirror (ss.randomness,     apvts.getRawParameterValue (IDs::randomness)->load());
    mirror (ss.minFill,        apvts.getRawParameterValue (IDs::minFill)->load());
    mirror (ss.flatness,       apvts.getRawParameterValue (IDs::flatness)->load());
    mirrorB (ss.paulStretch,   apvts.getRawParameterValue (IDs::paulStretch)->load() > 0.5f);
    if (changed) ss.generation.fetch_add (1);
    ss.outputGain.store (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (IDs::outputGain)->load()));
    ss.renderLength.store (apvts.getRawParameterValue (IDs::renderLength)->load());

    // Normalize params: mirrored WITHOUT bumping generation (the worker re-scales the existing
    // fill instead of re-rendering it).
    const bool  normOn  = apvts.getRawParameterValue (IDs::normEnabled)->load() > 0.5f;
    const bool  normLufs = apvts.getRawParameterValue (IDs::normUnit)->load() > 0.5f; // 1 = LUFS
    ss.normalizeEnabled.store (normOn);
    ss.normalizeTarget.store (apvts.getRawParameterValue (IDs::normTarget)->load());
    ss.normalizeLufs.store (normLufs);
    ss.wholeFile.store (apvts.getRawParameterValue (IDs::wholeFile)->load() > 0.5f);

    // When normalizing, Output is bypassed; show that by disabling the knob and the normalize
    // controls follow the toggle.
    gain_.slider.setEnabled (! normOn);
    normTarget_.setEnabled (normOn);
    normUnit_.setEnabled (normOn);
    normTarget_.setTextValueSuffix (normLufs ? " LUFS" : " dBFS");
    const float meas = normLufs ? ss.measuredLufs.load() : ss.measuredPeakDb.load();
    normReadout_.setText (meas > -119.0f ? "now " + juce::String (meas, 1) + (normLufs ? " LUFS" : " dBFS") : "",
                          juce::dontSendNotification);

    updateEmphasis();
    if (normOn) { gain_.slider.setAlpha (0.36f); gain_.label.setAlpha (0.36f); } // bypassed by Normalize

    wave_ = ss.getWave();
    if (! dragging_) selections_ = ss.getManualRanges(); // reflect edits made in the Waveform window
    meterDb_ = meterDb_ * 0.7f + ss.outMeterDb.load() * 0.3f;
    repaint();
}

void MainView::exportWav()
{
    double sr = 48000.0;
    auto fill = processor_.sessionState().getExportFill (sr);
    if (fill == nullptr || fill->empty() || (*fill)[0].empty())
        return;

    // Render exactly the requested length by tiling the seamless loop (no click at the wrap).
    const float lengthSec = processor_.sessionState().renderLength.load();
    const int loopLen = (int) (*fill)[0].size();
    const int outLen = juce::jmax (1, (int) std::lround (lengthSec * sr));

    // S7.3: loudness is measured on the ACTUAL deliverable. The stored fill was normalized on the
    // internal loop; a short Export Len crop can drift from that, so we re-measure and correct on
    // the exact export buffer below. Capture the current Normalize settings for that pass.
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

        // Build the tiled output buffer.
        std::vector<std::vector<float>> tiled ((std::size_t) numCh, std::vector<float> ((std::size_t) outLen));
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto& src = (*fill)[(std::size_t) ch];
            auto& dst = tiled[(std::size_t) ch];
            for (int i = 0; i < outLen; ++i) dst[(std::size_t) i] = src[(std::size_t) (i % loopLen)];
        }

        // S7.3: measure loudness on this exact export buffer and correct to target, so any Export Len
        // hits the requested LUFS/peak precisely (idempotent for a full-loop-length export).
        if (normOn)
        {
            const float meas = normLufs ? tonefill::dsp::integratedLufs (tiled, sr)
                                        : tonefill::dsp::peakDbfs (tiled);
            if (meas > -119.0f)
            {
                float gainDb = normTarget - meas;
                if (normLufs) // keep true peak under -1 dBFS
                {
                    const float peakDb = tonefill::dsp::peakDbfs (tiled);
                    const float newPeak = peakDb + gainDb;
                    if (newPeak > -1.0f) gainDb -= (newPeak + 1.0f);
                }
                const float g = std::pow (10.0f, gainDb / 20.0f);
                if (std::fabs (g - 1.0f) > 1.0e-4f)
                    for (auto& c : tiled) for (auto& smp : c) smp *= g;
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
    // Merge overlapping/touching spans, then publish to the worker.
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

void MainView::drawKnobIcon (juce::Graphics& g, juce::Rectangle<float> b, int icon, juce::Colour col) const
{
    g.setColour (col);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float w = b.getWidth(), h = b.getHeight();
    const juce::PathStrokeType st (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    switch (icon)
    {
        case IcSparkle:
        {
            juce::Path p; // 4-point star
            p.startNewSubPath (cx, b.getY());
            p.lineTo (cx + w * 0.13f, cy - h * 0.13f);
            p.lineTo (b.getRight(), cy);
            p.lineTo (cx + w * 0.13f, cy + h * 0.13f);
            p.lineTo (cx, b.getBottom());
            p.lineTo (cx - w * 0.13f, cy + h * 0.13f);
            p.lineTo (b.getX(), cy);
            p.lineTo (cx - w * 0.13f, cy - h * 0.13f);
            p.closeSubPath();
            g.fillPath (p);
            break;
        }
        case IcDialog:
        {
            const float xs[4] = { 0.18f, 0.40f, 0.62f, 0.84f };
            const float hs[4] = { 0.45f, 0.85f, 0.6f, 0.3f };
            for (int i = 0; i < 4; ++i)
            {
                const float x = b.getX() + w * xs[i];
                g.drawLine (x, cy - h * hs[i] * 0.5f, x, cy + h * hs[i] * 0.5f, 1.3f);
            }
            g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getY(), 1.3f); // slash
            break;
        }
        case IcTarget:
        {
            const float l = w * 0.28f;
            for (int q = 0; q < 4; ++q)
            {
                const float ox = (q & 1) ? b.getRight() : b.getX();
                const float oy = (q & 2) ? b.getBottom() : b.getY();
                const float sx = (q & 1) ? -1.0f : 1.0f;
                const float sy = (q & 2) ? -1.0f : 1.0f;
                g.drawLine (ox, oy, ox + sx * l, oy, 1.3f);
                g.drawLine (ox, oy, ox, oy + sy * l, 1.3f);
            }
            g.fillEllipse (cx - 1.4f, cy - 1.4f, 2.8f, 2.8f);
            break;
        }
        case IcCross: // two overlapping circles
        {
            const float r = h * 0.32f;
            g.drawEllipse (cx - r - w * 0.12f, cy - r, r * 2.0f, r * 2.0f, 1.3f);
            g.drawEllipse (cx - r + w * 0.12f, cy - r, r * 2.0f, r * 2.0f, 1.3f);
            break;
        }
        case IcShuffle:
        {
            juce::Path p;
            p.startNewSubPath (b.getX(), b.getY() + h * 0.25f);
            p.lineTo (b.getRight(), b.getBottom() - h * 0.25f);
            p.startNewSubPath (b.getX(), b.getBottom() - h * 0.25f);
            p.lineTo (b.getRight(), b.getY() + h * 0.25f);
            g.strokePath (p, st);
            g.fillEllipse (b.getRight() - 2.0f, b.getY() + h * 0.25f - 1.0f, 2.4f, 2.4f);
            g.fillEllipse (b.getRight() - 2.0f, b.getBottom() - h * 0.25f - 1.4f, 2.4f, 2.4f);
            break;
        }
        case IcSine:
        {
            juce::Path p;
            const int N = 18;
            for (int i = 0; i <= N; ++i)
            {
                const float t = (float) i / (float) N;
                const float x = b.getX() + t * w;
                const float y = cy - std::sin (t * juce::MathConstants<float>::twoPi) * h * 0.35f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, st);
            break;
        }
        case IcWave:
        {
            const float ys[6] = { 0.3f, 0.7f, 0.45f, 0.9f, 0.55f, 0.35f };
            juce::Path p;
            for (int i = 0; i < 6; ++i)
            {
                const float x = b.getX() + w * (float) i / 5.0f;
                const float y = b.getBottom() - h * ys[i];
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, st);
            break;
        }
        case IcSliders:
        {
            for (int i = 0; i < 3; ++i)
            {
                const float x = b.getX() + w * (0.2f + 0.3f * i);
                g.drawLine (x, b.getY(), x, b.getBottom(), 1.2f);
                const float ky = b.getY() + h * (i == 1 ? 0.65f : 0.35f);
                g.fillEllipse (x - 1.8f, ky - 1.8f, 3.6f, 3.6f);
            }
            break;
        }
        case IcClock:
        {
            const float r = h * 0.45f;
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.3f);
            g.drawLine (cx, cy, cx, cy - r * 0.6f, 1.2f);
            g.drawLine (cx, cy, cx + r * 0.5f, cy, 1.2f);
            break;
        }
        default: break;
    }
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (! manualMode_ || ! waveArea_.contains (e.getPosition()) || processor_.sessionState().sourceSamples.load() <= 0)
        return;
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

    const int a = juce::jmin (dragStartSample_, dragCurSample_);
    const int b = juce::jmax (dragStartSample_, dragCurSample_);
    const float px = sampleToX (b) - sampleToX (a);

    if (px < 4.0f)
    {
        // Click (not a drag): remove a selection under the cursor, if any.
        const int s = xToSample (e.x);
        const auto before = selections_.size();
        selections_.erase (std::remove_if (selections_.begin(), selections_.end(),
                                           [s] (const std::pair<int, int>& r) { return s >= r.first && s <= r.second; }),
                           selections_.end());
        if (selections_.size() != before) pushSelections();
    }
    else
    {
        selections_.emplace_back (a, b);
        pushSelections();
    }
    repaint();
}
} // namespace tonefill::plugin::ui
