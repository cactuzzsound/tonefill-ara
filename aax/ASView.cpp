#include "ASView.h"
#include "ToneFillAS_Defs.h"

#include "BinaryData.h"

#include <array>
#include <cmath>

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
        case KSeed:    return juce::String ((int) std::lround (1.0 + v * 99.0));
        case KNormTgt: return juce::String (-60.0 + v * 60.0, 1);
        case KPct:
        default:       return juce::String ((int) std::lround (v * 100.0)) + " %";
    }
}

ASView::Knob& ASView::addKnob (const char* id, const juce::String& name, Kind kind, juce::Colour arc)
{
    auto k = std::make_unique<Knob>();
    k->id = id; k->kind = kind;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k->slider.setRange (0.0, 1.0, 0.001);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, arc);
    k->slider.setColour (juce::Slider::textBoxTextColourId, LNF::navy());
    k->slider.textFromValueFunction = [kind] (double v) { return format (kind, v); };
    k->slider.valueFromTextFunction = [] (const juce::String&) { return 0.0; };
    const char* pid = id;
    k->slider.onValueChange = [this, pid, s = &k->slider] { bridge_.setNorm (pid, s->getValue()); };
    addAndMakeVisible (k->slider);
    k->label.setText (name.toUpperCase(), juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::Font (10.5f));
    k->label.setColour (juce::Label::textColourId, LNF::label());
    addAndMakeVisible (k->label);
    knobs_.push_back (std::move (k));
    return *knobs_.back();
}

ASView::Toggle& ASView::addToggle (const char* id, const juce::String& name)
{
    auto t = std::make_unique<Toggle>();
    t->id = id;
    t->btn.setButtonText (name);
    t->btn.setClickingTogglesState (true);
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
    // Detection / Structure / Texture (3 x 3), matching the plugin's grouping.
    addKnob (kParamClean,    "Clean Level",  KPct,     orange);
    addKnob (kParamVoice,    "Voice Reject", KPct,     orange);
    addKnob (kParamMinFill,  "Min Fill",     KMinFill, orange);
    addKnob (kParamFlatness, "Flatness",     KPct,     purple);
    addKnob (kParamChunk,    "Chunk Size",   KPct,     purple);
    addKnob (kParamXfade,    "Crossfade",    KPct,     purple);
    addKnob (kParamSmooth,   "Smoothness",   KPct,     purple);
    addKnob (kParamGain,     "Output",       KGain,    orange);
    addKnob (kParamSeed,     "Seed",         KSeed,    purple);
    // Extra panel knobs.
    addKnob (kParamHissFreq, "Hiss Freq",    KFreq,    purple);
    addKnob (kParamHissQ,    "Hiss Q",       KQ,       purple);
    addKnob (kParamNormTarget, "Target",     KNormTgt, orange);

    addToggle (kParamEnhance, "Enhance");
    addToggle (kParamExperim, "Experimental");
    addToggle (kParamHissOn,  "Hiss Filter");
    addToggle (kParamNormOn,  "Normalize");
    addToggle (kParamNormLufs, "LUFS");

    setSize (660, 384);
    startTimerHz (10);
    timerCallback();
}

ASView::~ASView() { stopTimer(); setLookAndFeel (nullptr); }

void ASView::timerCallback()
{
    // Pull param values into the controls unless the user is dragging that control.
    for (auto& k : knobs_)
        if (! k->slider.isMouseButtonDown())
            k->slider.setValue (bridge_.getNorm (k->id), juce::dontSendNotification);
    for (auto& t : toggles_)
        t->btn.setToggleState (bridge_.getNorm (t->id) > 0.5, juce::dontSendNotification);

    // Enhance gates the hiss controls; Normalize owns the target/unit + bypasses Output.
    const bool enh  = bridge_.getNorm (kParamEnhance) > 0.5;
    const bool norm = bridge_.getNorm (kParamNormOn)  > 0.5;
    auto setEnabled = [] (juce::Component& c, bool on) { c.setEnabled (on); c.setAlpha (on ? 1.0f : 0.4f); };
    for (auto& k : knobs_)
    {
        if (k->id == kParamSmooth)                          setEnabled (k->slider, enh);
        if (k->id == kParamHissFreq || k->id == kParamHissQ) setEnabled (k->slider, enh && bridge_.getNorm (kParamHissOn) > 0.5);
        if (k->id == kParamGain)                             setEnabled (k->slider, ! norm);
        if (k->id == kParamNormTarget)                       setEnabled (k->slider, norm);
    }
    for (auto& t : toggles_)
    {
        if (t->id == kParamHissOn) setEnabled (t->btn, enh);
        if (t->id == kParamNormLufs) setEnabled (t->btn, norm);
    }
}

void ASView::paint (juce::Graphics& g)
{
    g.fillAll (LNF::bg());

    // Logo (embedded PNG, trimmed to its content bbox), scaled to fit the header.
    {
        static const juce::Image logo = []
        {
            auto full = juce::ImageCache::getFromMemory (BinaryData::tonefilllogo_png, BinaryData::tonefilllogo_pngSize);
            return (full.isValid() && full.getWidth() >= 1652) ? full.getClippedImage ({ 331, 462, 1321, 280 }) : full;
        }();
        if (logo.isValid())
            g.drawImageWithin (logo, 14, 10, 230, 38,
                               juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, false);
    }

    const char* titles[3] = { "DETECTION", "STRUCTURE", "TEXTURE" };
    for (int i = 0; i < 3; ++i)
    {
        const auto cf = groupCard_[(std::size_t) i].toFloat();
        g.setColour (LNF::panel()); g.fillRoundedRectangle (cf, 12.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (cf, 12.0f, 1.0f);
        g.setColour (LNF::label());
        g.setFont (juce::Font (11.0f));
        g.drawText (titles[i], (int) cf.getX(), (int) cf.getY() + 10, (int) cf.getWidth(), 14, juce::Justification::centred, false);
        g.setColour (LNF::lineSoft());
        g.fillRect (cf.getX() + 14.0f, cf.getY() + 26.0f, cf.getWidth() - 28.0f, 1.0f);
    }
    for (const auto& pill : valuePills_)
    {
        g.setColour (LNF::panel()); g.fillRoundedRectangle (pill.toFloat(), 6.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (pill.toFloat(), 6.0f, 1.0f);
    }
    for (auto* r : { &toggleCard_, &extraCard_ })
    {
        g.setColour (LNF::panel()); g.fillRoundedRectangle (r->toFloat(), 12.0f);
        g.setColour (LNF::line());  g.drawRoundedRectangle (r->toFloat(), 12.0f, 1.0f);
    }
}

void ASView::resized()
{
    valuePills_.clear();
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (40); // header/logo

    auto placeCard = [this] (juce::Rectangle<int> card, int i0, int count)
    {
        auto body = card.reduced (8, 0).withTrimmedTop (32);
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
    for (int i = 0; i < 3; ++i)
    {
        groupCard_[(std::size_t) i] = row.removeFromLeft (i < 2 ? cardW : row.getWidth());
        if (i < 2) row.removeFromLeft (gap);
        placeCard (groupCard_[(std::size_t) i], i * 3, 3);
    }
    r.removeFromTop (12);

    // Toggle card (Enhance / Experimental / Hiss / Normalize) + LUFS.
    toggleCard_ = r.removeFromTop (56);
    {
        auto tr = toggleCard_.reduced (12, 0);
        for (auto& t : toggles_)
        {
            const bool wide = (t->id == kParamExperim);
            t->btn.setBounds (tr.removeFromLeft (wide ? 116 : 92).withSizeKeepingCentre (wide ? 116 : 92, 28));
            tr.removeFromLeft (8);
        }
    }
    r.removeFromTop (12);

    // Extra card: Hiss Freq / Q + Normalize Target.
    extraCard_ = r;
    {
        auto er = extraCard_.reduced (10, 4).withTrimmedTop (6);
        const int kw = er.getWidth() / 3;
        auto place = [&] (Knob& k, juce::Rectangle<int> cell)
        {
            k.label.setBounds (cell.removeFromTop (13));
            k.slider.setBounds (cell);
        };
        // knobs_ indices: 9 = Hiss Freq, 10 = Hiss Q, 11 = Target
        place (*knobs_[9],  er.removeFromLeft (kw));
        place (*knobs_[10], er.removeFromLeft (kw));
        place (*knobs_[11], er);
    }
}
