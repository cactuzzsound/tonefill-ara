#include "ASView.h"
#include "ToneFillAS_Defs.h"
#include "ASShared.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace tonefill_aax;
using LNF = tonefill::plugin::ui::ToneFillLookAndFeel;

juce::String ASView::format (Kind k, double v) // v = normalized [0,1]
{
    switch (k)
    {
        case KMinFill: return juce::String (0.2 + v * 4.8, 2) + " s";
        case KGain:    return juce::String (-24.0 + v * 48.0, 1) + " dB";
        case KFreq:    return juce::String ((int) std::lround (3000.0 + v * 12000.0)) + " Hz";
        case KQ:       return juce::String (0.3 + v * 1.7, 2);
        case KNormTgt: return juce::String (-60.0 + v * 60.0, 1);
        case KBands:   return juce::String ((int) std::lround (3.0 + v * 9.0));
        case KPct:
        default:       return juce::String ((int) std::lround (v * 100.0)) + " %";
    }
}

ASView::Knob& ASView::addKnob (const char* id, const juce::String& name, Kind kind, juce::Colour arc, const juce::String& tip)
{
    auto k = std::make_unique<Knob>();
    k->id = id; k->kind = kind;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k->slider.setRange (0.0, 1.0, 0.001);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, arc);
    k->slider.setColour (juce::Slider::textBoxTextColourId, LNF::navy());
    k->slider.textFromValueFunction = [kind] (double v) { return format (kind, v); };
    k->slider.setTooltip (tip);
    const char* pid = id;
    k->slider.onValueChange = [this, pid, s = &k->slider] { bridge_.setNorm (pid, s->getValue()); };
    addAndMakeVisible (k->slider);
    k->label.setText (name.toUpperCase(), juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::Font (10.5f));
    k->label.setColour (juce::Label::textColourId, LNF::label());
    k->label.setTooltip (tip);
    addAndMakeVisible (k->label);
    knobs_.push_back (std::move (k));
    return *knobs_.back();
}

ASView::Toggle& ASView::addToggle (const char* id, const juce::String& name, const juce::String& tip)
{
    auto t = std::make_unique<Toggle>();
    t->id = id;
    t->btn.setButtonText (name);
    t->btn.setClickingTogglesState (true);
    t->btn.setTooltip (tip);
    const char* pid = id;
    t->btn.onClick = [this, pid, b = &t->btn] { bridge_.setNorm (pid, b->getToggleState() ? 1.0 : 0.0); };
    addAndMakeVisible (t->btn);
    toggles_.push_back (std::move (t));
    return *toggles_.back();
}

ASView::ASView (Bridge bridge) : bridge_ (std::move (bridge))
{
    setLookAndFeel (&lnf_);
    const auto orange = LNF::accent(), purple = LNF::purple();

    // Detection / Structure / Texture (matches the plugin grouping).
    addKnob (kParamClean,    "Clean Level",  KPct,     orange, "How quiet a moment must be to count as room tone. Lower = stricter, rejects claps, clicks and loud bits.");
    addKnob (kParamVoice,    "Voice Reject", KPct,     orange, "How hard to exclude voice / breaths / mouth noise. Higher = cleaner room tone.");
    addKnob (kParamMinFill,  "Min Fill",     KMinFill, orange, "Minimum length a stable fragment must have to be used. Longer = fewer, more consistent chunks and fewer audible joins.");
    addKnob (kParamFlatness, "Flatness",     KPct,     purple, "How strict the stationarity requirement is. Higher = only very steady stretches, so the fill avoids audible jumps.");
    addKnob (kParamBands,    "Bands",        KBands,   purple, "Spectral only: how many frequency bands the source is split into. Fewer = wider bands, more = narrower.");
    addKnob (kParamXfade,    "Crossfade",    KPct,     purple, "How much neighbouring chunks overlap and blend. Higher = smoother joins.");
    addKnob (kParamSmooth,   "Smoothness",   KPct,     purple, "Enhance only: resynthesis window size. Higher = smoother, more diffuse; lower keeps more fine texture.");
    addKnob (kParamGain,     "Output",       KGain,    orange, "Output volume, in dB. Bypassed while Normalize is on.");
    // Normalize + Hiss panels.
    addKnob (kParamNormTarget, "Target",     KNormTgt, orange, "Loudness target the render is corrected to (dBFS peak or integrated LUFS).");
    addKnob (kParamHissFreq,   "Freq",       KFreq,    purple, "De-hiss corner frequency (3-15 kHz). Lower = more aggressive HF cut.");
    addKnob (kParamHissQ,      "Q",          KQ,       purple, "De-hiss filter Q (slope / resonance at the corner).");

    addToggle (kParamEnhance, "Enhance", "PaulStretch resynthesis for when fragments won't blend into a clean bed. Enables Smoothness and the Hiss Filter.");
    addToggle (kParamHissOn,  "Hiss Filter", "Enhance only: a low-pass that removes the HF hiss PaulStretch can add. Baked into the render.");
    addToggle (kParamNormOn,  "Normalize", "Bake the output to a fixed loudness target, measured on the actual rendered length.");
    addToggle (kParamNormLufs, "LUFS", "LUFS = integrated loudness (EBU R128). Off = dBFS peak.");

    // Classic / Experimental / Spectral mode group (mutually exclusive; drives the statistical +
    // spectral params). Classic/Experimental pick the selection engine; Spectral is the per-band mosaic.
    for (auto* b : { &classicBtn_, &expBtn_, &spectralBtn_ }) { b->setClickingTogglesState (false); addAndMakeVisible (*b); }
    classicBtn_.setTooltip ("Classic selection: the tuned gate stack (default).");
    expBtn_.setTooltip ("Experimental selection: weighted per-frame scoring. A/B against Classic by ear.");
    spectralBtn_.setTooltip ("Spectral: split into frequency bands, find clean room tone per band and recombine. The Bands knob sets how many.");
    classicBtn_.onClick  = [this] { bridge_.setNorm (kParamExperim, 0.0); bridge_.setNorm (kParamSpectral, 0.0); };
    expBtn_.onClick      = [this] { bridge_.setNorm (kParamExperim, 1.0); bridge_.setNorm (kParamSpectral, 0.0); };
    spectralBtn_.onClick = [this] { bridge_.setNorm (kParamExperim, 0.0); bridge_.setNorm (kParamSpectral, 1.0); };

    // Auto / Manual segmented pair (Manual = learn only from the regions dragged on the waveform).
    for (auto* b : { &autoBtn_, &manualBtn_ }) { b->setClickingTogglesState (false); addAndMakeVisible (*b); }
    autoBtn_.setTooltip ("Auto: find clean room tone automatically in the selection.");
    manualBtn_.setTooltip ("Manual: drag on the waveform below to pick the room-tone regions; click one to remove.");
    autoBtn_.onClick   = [this] { bridge_.setNorm (kParamManual, 0.0); };
    manualBtn_.onClick = [this] { bridge_.setNorm (kParamManual, 1.0); };

    regenBtn_.setButtonText ("Regenerate");
    regenBtn_.setTooltip ("New random variation of the fill (same settings). Nudges the Seed so the next Render/Preview differs.");
    regenBtn_.onClick = [this] { bridge_.setNorm (kParamSeed, rng_.nextDouble()); };
    addAndMakeVisible (regenBtn_);

    tips_ = {
        "Push Voice Reject up to strip breaths and mouth noise from the bed.",
        "Keep Clean Level low - it rejects claps and loud bits harder.",
        "Raise Min Fill for longer, more consistent chunks and fewer joins.",
        "Turn on Enhance when the fragments won't blend into a smooth bed.",
        "Flatness high = only very steady stretches, so fewer audible jumps.",
        "Use WHOLE FILE (Pro Tools) to scan the whole clip for clean tone.",
        "Normalize bakes the output to a fixed LUFS or dBFS target on Render.",
        "Regenerate rolls a new random variation with the same settings."
    };
    tipLbl_.setText (tips_[0], juce::dontSendNotification);
    tipLbl_.setFont (juce::Font (12.0f));
    tipLbl_.setColour (juce::Label::textColourId, juce::Colour (0xff4a4d55));
    tipLbl_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tipLbl_);

    setSize (720, 548);
    startTimerHz (12);
    timerCallback();
}

ASView::~ASView() { stopTimer(); setLookAndFeel (nullptr); }

void ASView::timerCallback()
{
    for (auto& k : knobs_)
        if (! k->slider.isMouseButtonDown())
            k->slider.setValue (bridge_.getNorm (k->id), juce::dontSendNotification);
    for (auto& t : toggles_)
        t->btn.setToggleState (bridge_.getNorm (t->id) > 0.5, juce::dontSendNotification);

    const bool exp  = bridge_.getNorm (kParamExperim) > 0.5;
    const bool spec = bridge_.getNorm (kParamSpectral) > 0.5;
    classicBtn_.setToggleState  (! exp && ! spec, juce::dontSendNotification);
    expBtn_.setToggleState      (exp && ! spec,   juce::dontSendNotification);
    spectralBtn_.setToggleState (spec,            juce::dontSendNotification);
    for (auto& k : knobs_)
        if (k->id == kParamBands)
        { k->slider.setEnabled (spec); k->slider.setAlpha (spec ? 1.0f : 0.4f); k->label.setAlpha (spec ? 1.0f : 0.4f); }

    manualMode_ = bridge_.getNorm (kParamManual) > 0.5;
    autoBtn_.setToggleState (! manualMode_, juce::dontSendNotification);
    manualBtn_.setToggleState (manualMode_, juce::dontSendNotification);

    // Pull the analysed source length (for x mapping) + the manual selection from shared state.
    if (bridge_.shared != nullptr && ! dragging_)
    {
        const juce::SpinLock::ScopedTryLockType l (bridge_.shared->lock);
        if (l.isLocked())
        {
            waveSamples_ = bridge_.shared->sourceSamples;
            selections_  = bridge_.shared->manualRanges;
        }
    }

    const bool enh  = bridge_.getNorm (kParamEnhance) > 0.5;
    const bool norm = bridge_.getNorm (kParamNormOn)  > 0.5;
    const bool hiss = bridge_.getNorm (kParamHissOn)  > 0.5;
    auto en = [] (juce::Component& c, bool on) { c.setEnabled (on); c.setAlpha (on ? 1.0f : 0.4f); };
    for (auto& k : knobs_)
    {
        if (k->id == kParamSmooth)                             en (k->slider, enh);
        if (k->id == kParamHissFreq || k->id == kParamHissQ)   en (k->slider, enh && hiss);
        if (k->id == kParamGain)                               en (k->slider, ! norm);
        if (k->id == kParamNormTarget)                         en (k->slider, norm);
    }
    for (auto& t : toggles_)
    {
        if (t->id == kParamHissOn)   en (t->btn, enh);
        if (t->id == kParamNormLufs) en (t->btn, norm);
    }

    if (++tipTick_ >= 300) { tipTick_ = 0; tipIdx_ = (tipIdx_ + 1) % (int) tips_.size(); tipLbl_.setText (tips_[(std::size_t) tipIdx_], juce::dontSendNotification); }
}

void ASView::paint (juce::Graphics& g)
{
    g.fillAll (LNF::bg());

    // Logo.
    {
        static const juce::Image logo = []
        {
            auto full = juce::ImageCache::getFromMemory (BinaryData::tonefilllogo_png, BinaryData::tonefilllogo_pngSize);
            return (full.isValid() && full.getWidth() >= 1652) ? full.getClippedImage ({ 331, 462, 1321, 280 }) : full;
        }();
        if (logo.isValid())
            g.drawImageWithin (logo, 14, 10, 220, 36, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, false);
    }

    // Grouped header background behind Classic|Experimental|Spectral.
    {
        auto u = classicBtn_.getBounds().getUnion (spectralBtn_.getBounds()).toFloat().expanded (4.0f);
        g.setColour (LNF::panelHi()); g.fillRoundedRectangle (u, 9.0f);
        g.setColour (LNF::line());    g.drawRoundedRectangle (u, 9.0f, 1.0f);
    }

    const char* titles[3] = { "DETECTION", "STRUCTURE", "TEXTURE" };
    for (int i = 0; i < 3; ++i)
    {
        const auto cf = groupCard_[(std::size_t) i].toFloat();
        g.setColour (LNF::panel()); g.fillRoundedRectangle (cf, 12.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (cf, 12.0f, 1.0f);
        g.setColour (LNF::label());  g.setFont (juce::Font (11.0f));
        g.drawText (titles[i], (int) cf.getX(), (int) cf.getY() + 10, (int) cf.getWidth(), 14, juce::Justification::centred, false);
        g.setColour (LNF::lineSoft());
        g.fillRect (cf.getX() + 14.0f, cf.getY() + 26.0f, cf.getWidth() - 28.0f, 1.0f);
    }
    for (const auto& pill : valuePills_)
    {
        g.setColour (LNF::panel()); g.fillRoundedRectangle (pill.toFloat(), 6.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (pill.toFloat(), 6.0f, 1.0f);
    }
    auto card = [&] (juce::Rectangle<int> r, const char* label, juce::Colour labelCol)
    {
        g.setColour (LNF::panel()); g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (r.toFloat(), 12.0f, 1.0f);
        if (label) { g.setColour (labelCol); g.setFont (juce::Font (11.0f, juce::Font::bold));
                     g.drawText (label, r.getX() + 14, r.getY() + 6, 160, 12, juce::Justification::centredLeft, false); }
    };
    card (normCard_, nullptr, {});
    const bool enh = bridge_.getNorm (kParamEnhance) > 0.5;
    card (hissCard_, "HISS FILTER", enh ? LNF::coral() : LNF::muted());
    card (tipCard_, "TIP", LNF::accent());

    // Header grouped background behind Auto|Manual.
    {
        auto u = autoBtn_.getBounds().getUnion (manualBtn_.getBounds()).toFloat().expanded (4.0f);
        g.setColour (LNF::panelHi()); g.fillRoundedRectangle (u, 9.0f);
        g.setColour (LNF::line());    g.drawRoundedRectangle (u, 9.0f, 1.0f);
    }

    // Waveform card.
    card (waveCard_, nullptr, {});
    g.setColour (LNF::panelHi());
    g.fillRoundedRectangle (waveArea_.toFloat(), 6.0f);

    std::vector<float> peak; std::vector<char> clean; float usedSec = 0, availSec = 0, seamDb = 0, levelDb = -120.0f; int chunks = 0; bool ready = false; double sr = 48000.0;
    if (bridge_.shared != nullptr)
    {
        const juce::SpinLock::ScopedTryLockType l (bridge_.shared->lock);
        if (l.isLocked())
        {
            peak = bridge_.shared->peak; clean = bridge_.shared->clean;
            usedSec = bridge_.shared->usedSec; availSec = bridge_.shared->availSec; seamDb = bridge_.shared->seamDb;
            levelDb = bridge_.shared->levelDb; chunks = bridge_.shared->chunks; ready = bridge_.shared->ready;
            sr = bridge_.shared->sampleRate;
        }
    }

    if (! peak.empty())
    {
        const int n = (int) peak.size();
        const float w = (float) waveArea_.getWidth(), midY = (float) waveArea_.getCentreY();
        const float halfH = (float) waveArea_.getHeight() * 0.5f - 4.0f, bw = w / (float) n;
        g.setColour (LNF::line());
        g.fillRect ((float) waveArea_.getX(), midY - 0.5f, w, 1.0f);
        auto selBin = [&] (int i) -> bool
        {
            if (waveSamples_ <= 0) return false;
            const int s0 = (int) ((long long) i * waveSamples_ / n), s1 = (int) ((long long) (i + 1) * waveSamples_ / n);
            for (const auto& s : selections_) if (s.first < s1 && s.second > s0) return true;
            if (dragging_) { const int a = juce::jmin (dragStart_, dragCur_), b = juce::jmax (dragStart_, dragCur_); if (a < s1 && b > s0) return true; }
            return false;
        };
        for (int i = 0; i < n; ++i)
        {
            const float amp = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, peak[(std::size_t) i])) * 1.4f);
            const float hh = juce::jmax (1.0f, amp * halfH);
            const bool lit = manualMode_ ? selBin (i) : (i < (int) clean.size() && clean[(std::size_t) i]);
            g.setColour (lit ? LNF::accent() : juce::Colour (0xffc9c4b8));
            g.fillRect ((float) waveArea_.getX() + (float) i * bw, midY - hh, juce::jmax (1.0f, bw + 0.5f), 2.0f * hh);
        }
        // manual selection edges
        if (manualMode_)
        {
            auto edge = [&] (int s0, int s1, juce::Colour c)
            { const float x0 = sampleToX (juce::jmin (s0, s1)), x1 = sampleToX (juce::jmax (s0, s1));
              g.setColour (c); g.drawRect (juce::Rectangle<float> { x0, (float) waveArea_.getY(), juce::jmax (2.0f, x1 - x0), (float) waveArea_.getHeight() }, 1.0f); };
            for (const auto& s : selections_) edge (s.first, s.second, LNF::accent().withAlpha (0.9f));
            if (dragging_) edge (dragStart_, dragCur_, LNF::accent());
        }
        // timecode ruler
        const double totalSec = (waveSamples_ > 0 && sr > 0) ? waveSamples_ / sr : 0.0;
        g.setFont (juce::Font (9.5f));
        if (totalSec > 0.0)
            for (int i = 0; i <= 6; ++i)
            {
                const float x = waveRuler_.getX() + (float) i / 6.0f * waveRuler_.getWidth();
                g.setColour (LNF::line());  g.fillRect (x, (float) waveRuler_.getY(), 1.0f, 4.0f);
                g.setColour (LNF::muted());
                g.drawText (juce::String (totalSec * i / 6.0, 1) + "s",
                            (int) x + 2, waveRuler_.getY() + 3, 54, 10, juce::Justification::centredLeft, false);
            }
    }
    else
    {
        g.setColour (LNF::muted());
        g.setFont (juce::Font (12.0f));
        g.drawText ("Press Preview (or Render) once to analyse the selection.", waveArea_, juce::Justification::centred, false);
    }

    // Status line (used / chunks / avail / seam / in), like the plugin.
    g.setColour (LNF::navy());
    g.setFont (juce::Font (12.0f));
    const juce::String seamStr = (ready && seamDb > 0.05f) ? "   \xc2\xb7   seam " + juce::String (seamDb, 1) + " dB" + (seamDb > 4.0f ? " !" : "") : juce::String();
    const juce::String txt = ready
        ? "used " + juce::String (usedSec, 1) + " s (" + juce::String (chunks) + " chunks)   \xc2\xb7   avail "
              + juce::String (availSec, 1) + " s" + seamStr + "   \xc2\xb7   in " + juce::String (levelDb, 0) + " dB"
        : (manualMode_ ? "Manual: drag on the waveform to add room-tone regions." : "");
    g.drawText (txt, dataArea_, juce::Justification::centredLeft, false);
}

void ASView::resized()
{
    valuePills_.clear();
    auto r = getLocalBounds().reduced (14);

    auto head = r.removeFromTop (40);
    head.removeFromLeft (230); // logo
    {
        auto seg = [] (juce::Rectangle<int> box, juce::TextButton& a, juce::TextButton& b)
        { const int w = (box.getWidth() - 4) / 2; a.setBounds (box.removeFromLeft (w)); box.removeFromLeft (4); b.setBounds (box); };
        { auto box = head.removeFromRight (264).withSizeKeepingCentre (264, 26);
          const int w = (box.getWidth() - 8) / 3;
          classicBtn_.setBounds (box.removeFromLeft (w)); box.removeFromLeft (4);
          expBtn_.setBounds (box.removeFromLeft (w));     box.removeFromLeft (4);
          spectralBtn_.setBounds (box); }
        head.removeFromRight (8);
        (*std::find_if (toggles_.begin(), toggles_.end(), [] (auto& t) { return t->id == kParamEnhance; }))
            ->btn.setBounds (head.removeFromRight (80).withSizeKeepingCentre (80, 26));
        head.removeFromRight (8);
        seg (head.removeFromRight (120).withSizeKeepingCentre (120, 26), autoBtn_, manualBtn_);
    }
    r.removeFromTop (10);

    auto placeCard = [this] (juce::Rectangle<int> cardR, int i0, int count)
    {
        auto body = cardR.reduced (8, 0).withTrimmedTop (32);
        const int kw = body.getWidth() / count;
        for (int j = 0; j < count; ++j)
        {
            auto cell = body.removeFromLeft (j < count - 1 ? kw : body.getWidth());
            auto inner = cell.reduced (4, 6);
            knobs_[(std::size_t) (i0 + j)]->label.setBounds (inner.removeFromTop (14));
            knobs_[(std::size_t) (i0 + j)]->slider.setBounds (inner);
            valuePills_.push_back (inner.removeFromBottom (20).reduced (8, 1));
        }
    };
    auto row = r.removeFromTop (150);
    const int gap = 12, cardW = (row.getWidth() - 2 * gap) / 3;
    groupCard_[0] = row.removeFromLeft (cardW); row.removeFromLeft (gap); placeCard (groupCard_[0], 0, 3);
    groupCard_[1] = row.removeFromLeft (cardW); row.removeFromLeft (gap); placeCard (groupCard_[1], 3, 3);
    groupCard_[2] = row;                                                  placeCard (groupCard_[2], 6, 2); // Smoothness, Output
    r.removeFromTop (12);

    // Normalize + Hiss cards side by side.
    auto midRow = r.removeFromTop (62);
    normCard_ = midRow.removeFromLeft ((midRow.getWidth() - 12) / 2);
    midRow.removeFromLeft (12);
    hissCard_ = midRow;
    auto findT = [this] (const char* id) -> juce::TextButton& { return (*std::find_if (toggles_.begin(), toggles_.end(), [id] (auto& t) { return t->id == id; }))->btn; };
    {
        auto nr = normCard_.reduced (12, 8);
        findT (kParamNormOn).setBounds (nr.removeFromLeft (92).withSizeKeepingCentre (92, 26));
        nr.removeFromLeft (8);
        findT (kParamNormLufs).setBounds (nr.removeFromRight (66).withSizeKeepingCentre (66, 26));
        nr.removeFromRight (8);
        knobs_[8]->label.setVisible (false);
        knobs_[8]->slider.setBounds (nr); // Target
    }
    {
        auto hr = hissCard_.reduced (12, 0).withTrimmedTop (20);
        findT (kParamHissOn).setBounds (hr.removeFromLeft (88).withSizeKeepingCentre (88, 24));
        hr.removeFromLeft (8);
        const int kw = hr.getWidth() / 2;
        auto place = [] (Knob& k, juce::Rectangle<int> cell) { k.label.setBounds (cell.removeFromTop (12)); k.slider.setBounds (cell); };
        place (*knobs_[9],  hr.removeFromLeft (kw));  // Hiss Freq
        place (*knobs_[10], hr);                      // Hiss Q
    }
    r.removeFromTop (12);

    // Footer: Tip + Regenerate.
    auto foot = r.removeFromTop (44);
    regenBtn_.setBounds (foot.removeFromRight (130).withSizeKeepingCentre (130, 36));
    foot.removeFromRight (12);
    tipCard_ = foot;
    tipLbl_.setBounds (tipCard_.reduced (14, 0).withTrimmedLeft (34));
    r.removeFromTop (12);

    // Waveform card (source overlay + status), like the plugin's bottom panel.
    waveCard_ = r;
    auto wc = waveCard_.reduced (12, 10);
    waveArea_  = wc.removeFromTop (juce::jmax (40, wc.getHeight() - 34));
    waveRuler_ = wc.removeFromTop (12);
    wc.removeFromTop (4);
    dataArea_ = wc.removeFromTop (16);
}

int ASView::xToSample (int x) const
{
    if (waveSamples_ <= 0 || waveArea_.getWidth() <= 0) return 0;
    const float f = (float) (x - waveArea_.getX()) / (float) waveArea_.getWidth();
    return juce::jlimit (0, waveSamples_, (int) std::lround (f * (float) waveSamples_));
}

float ASView::sampleToX (int sample) const
{
    if (waveSamples_ <= 0) return (float) waveArea_.getX();
    return (float) waveArea_.getX() + (float) juce::jlimit (0, waveSamples_, sample) / (float) waveSamples_ * (float) waveArea_.getWidth();
}

void ASView::pushSelections()
{
    std::sort (selections_.begin(), selections_.end());
    std::vector<std::pair<int, int>> merged;
    for (const auto& s : selections_)
        if (! merged.empty() && s.first <= merged.back().second) merged.back().second = juce::jmax (merged.back().second, s.second);
        else merged.push_back (s);
    selections_ = merged;
    if (bridge_.shared != nullptr)
    {
        const juce::SpinLock::ScopedLockType l (bridge_.shared->lock);
        bridge_.shared->manualRanges = selections_;
    }
    // Bump Seed a hair so the render signature changes and PT re-analyses on the next pass.
    bridge_.setNorm (kParamSeed, rng_.nextDouble());
}

void ASView::mouseDown (const juce::MouseEvent& e)
{
    if (! manualMode_ || ! waveArea_.contains (e.getPosition()) || waveSamples_ <= 0) return;
    dragging_ = true;
    dragStart_ = dragCur_ = xToSample (e.x);
    repaint();
}

void ASView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    dragCur_ = xToSample (e.x);
    repaint();
}

void ASView::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    dragging_ = false;
    const int a = juce::jmin (dragStart_, dragCur_), b = juce::jmax (dragStart_, dragCur_);
    if (std::abs (sampleToX (b) - sampleToX (a)) < 4.0f)
    {
        const int s = xToSample (e.x);
        const auto before = selections_.size();
        selections_.erase (std::remove_if (selections_.begin(), selections_.end(),
                           [s] (const std::pair<int, int>& r) { return s >= r.first && s <= r.second; }), selections_.end());
        if (selections_.size() != before) pushSelections();
    }
    else { selections_.emplace_back (a, b); pushSelections(); }
    repaint();
}
