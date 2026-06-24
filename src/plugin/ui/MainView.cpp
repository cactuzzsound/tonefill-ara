#include "plugin/ui/MainView.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ParameterState.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace tonefill::plugin::ui
{
using IDs = ParameterState::IDs;
constexpr int MainView::tabMode_[4];

namespace
{
void initKnob (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
    s.setRange (0.0, 1.0, 0.001);
    parent.addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (12.0f));
    l.setColour (juce::Label::textColourId, ToneFillLookAndFeel::text());
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

    const char* names[4] = { "Ambience", "Static", "Hybrid", "Complex" };
    for (int i = 0; i < 4; ++i)
    {
        tabs_[(std::size_t) i].setButtonText (names[i]);
        tabs_[(std::size_t) i].setClickingTogglesState (true);
        tabs_[(std::size_t) i].setRadioGroupId (100);
        tabs_[(std::size_t) i].onClick = [this, i] { setMode (tabMode_[i]); };
        addAndMakeVisible (tabs_[(std::size_t) i]);
    }

    initKnob (*this, threshold_.slider, threshold_.label, "Threshold");
    initKnob (*this, fragment_.slider,  fragment_.label,  "Fragment");
    initKnob (*this, blend_.slider,     blend_.label,     "Blend");
    initKnob (*this, tonal_.slider,     tonal_.label,     "Tonal");
    initKnob (*this, movement_.slider,  movement_.label,  "Movement");
    initKnob (*this, gain_.slider,      gain_.label,      "Gain");
    gain_.slider.setRange (-24.0, 24.0, 0.1);

    thA_ = std::make_unique<SA> (apvts, IDs::threshold,      threshold_.slider);
    frA_ = std::make_unique<SA> (apvts, IDs::fragment,       fragment_.slider);
    blA_ = std::make_unique<SA> (apvts, IDs::blend,          blend_.slider);
    toA_ = std::make_unique<SA> (apvts, IDs::tonalRetention, tonal_.slider);
    moA_ = std::make_unique<SA> (apvts, IDs::movement,       movement_.slider);
    gaA_ = std::make_unique<SA> (apvts, IDs::outputGain,     gain_.slider);

    regenBtn_.onClick = [] { auto& ss = SessionState::get(); ss.seed.fetch_add (0x9E3779B97F4A7C15ULL); ss.generation.fetch_add (1); };
    addAndMakeVisible (regenBtn_);
    exportBtn_.getProperties().set ("accent", true);
    exportBtn_.onClick = [this] { exportWav(); };
    addAndMakeVisible (exportBtn_);

    startTimerHz (15);
}

MainView::~MainView() { stopTimer(); setLookAndFeel (nullptr); }

void MainView::setMode (int modeIndex)
{
    if (auto* p = processor_.parameters().apvts.getParameter (IDs::mode))
        p->setValueNotifyingHost ((float) modeIndex / 3.0f);
    updateEmphasis (modeIndex);
}

void MainView::updateEmphasis (int mode)
{
    auto set = [] (Knob& k, bool on) { const float a = on ? 1.0f : 0.36f; k.slider.setAlpha (a); k.label.setAlpha (a); };
    const bool realFrag = (mode == 3 || mode == 1 || mode == 2); // Ambience/Hybrid/Complex
    const bool synthTonal = (mode == 0 || mode == 1 || mode == 2); // Static/Hybrid/Complex
    const bool moves = (mode == 0 || mode == 2);                   // Static/Complex
    set (threshold_, true);
    set (fragment_, realFrag); set (blend_, realFrag);
    set (tonal_, synthTonal); set (movement_, moves);
    set (gain_, true);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (ToneFillLookAndFeel::bg());

    // Waveform: green bars = selected clean ambience, gray = rejected (loud / voice).
    g.setColour (juce::Colour (0xff16181c));
    g.fillRoundedRectangle (waveArea_.toFloat(), 6.0f);
    if (! wave_.peak.empty())
    {
        const int n = (int) wave_.peak.size();
        const float bw = (float) waveArea_.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float h = juce::jlimit (1.0f, (float) waveArea_.getHeight() - 4.0f,
                                          wave_.peak[(std::size_t) i] * (waveArea_.getHeight() - 4.0f) * 3.0f);
            const bool clean = i < (int) wave_.clean.size() && wave_.clean[(std::size_t) i];
            g.setColour (clean ? ToneFillLookAndFeel::accent() : ToneFillLookAndFeel::line());
            const float x = waveArea_.getX() + i * bw;
            g.fillRect (x, waveArea_.getBottom() - h - 2.0f, juce::jmax (1.0f, bw - 1.0f), h);
        }
    }

    // Output meter.
    g.setColour (ToneFillLookAndFeel::panel());
    g.fillRoundedRectangle (meterArea_.toFloat(), 5.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb_ + 60.0f) / 60.0f);
    g.setColour (meterDb_ > -3.0f ? juce::Colours::orange : ToneFillLookAndFeel::accent());
    g.fillRoundedRectangle (meterArea_.toFloat().withWidth (meterArea_.getWidth() * norm), 5.0f);

    // Status line under the meter.
    auto& ss = SessionState::get();
    const int phase = ss.phase.load();
    const juce::String ph = phase == 0 ? "idle" : (phase == 1 ? "analyzing" : "ready");
    g.setColour (ToneFillLookAndFeel::muted());
    g.setFont (juce::Font (11.0f));
    g.drawText ("out " + juce::String (meterDb_, 1) + " dB", meterArea_.translated (meterArea_.getWidth() + 8, 0).withWidth (90),
                juce::Justification::centredLeft, false);
    g.drawText (ph + "   ·   partials " + juce::String (ss.numPartials.load())
                + "   ·   clean " + juce::String (ss.learnSeconds.load(), 1) + " s"
                + "   ·   in " + juce::String (ss.levelDb.load(), 0) + " dB",
                getLocalBounds().removeFromBottom (20).reduced (16, 2), juce::Justification::centredLeft, false);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto head = r.removeFromTop (40);
    titleLbl_.setBounds (head.removeFromTop (22));
    subLbl_.setBounds (head);
    r.removeFromTop (4);

    waveArea_ = r.removeFromTop (52);
    r.removeFromTop (12);

    auto tabRow = r.removeFromTop (28);
    const int tw = (tabRow.getWidth() - 24) / 4;
    for (int i = 0; i < 4; ++i) { tabs_[(std::size_t) i].setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (8); }
    r.removeFromTop (14);

    auto knobRow = [&r] (std::initializer_list<MainView::Knob*> ks)
    {
        auto row = r.removeFromTop (94);
        const int cw = row.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto cell = row.removeFromLeft (cw);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell.reduced (4, 0));
        }
        r.removeFromTop (6);
    };
    knobRow ({ &threshold_, &fragment_, &blend_ });
    knobRow ({ &tonal_, &movement_, &gain_ });

    r.removeFromTop (4);
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
    auto& ss = SessionState::get();

    bool changed = false;
    const int mode = (int) apvts.getRawParameterValue (IDs::mode)->load();
    if (ss.mode.load() != mode) { ss.mode.store (mode); changed = true; }
    auto mirror = [&changed] (std::atomic<float>& dst, float v) { if (std::abs (dst.load() - v) > 1.0e-4f) { dst.store (v); changed = true; } };
    mirror (ss.threshold,      apvts.getRawParameterValue (IDs::threshold)->load());
    mirror (ss.fragment,       apvts.getRawParameterValue (IDs::fragment)->load());
    mirror (ss.blend,          apvts.getRawParameterValue (IDs::blend)->load());
    mirror (ss.tonalRetention, apvts.getRawParameterValue (IDs::tonalRetention)->load());
    mirror (ss.movement,       apvts.getRawParameterValue (IDs::movement)->load());
    if (changed) ss.generation.fetch_add (1);
    ss.outputGain.store (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (IDs::outputGain)->load()));

    // Reflect mode in the tabs + emphasis.
    for (int i = 0; i < 4; ++i) tabs_[(std::size_t) i].setToggleState (tabMode_[i] == mode, juce::dontSendNotification);
    updateEmphasis (mode);

    wave_ = ss.getWave();
    meterDb_ = meterDb_ * 0.7f + ss.outMeterDb.load() * 0.3f;
    repaint();
}

void MainView::exportWav()
{
    double sr = 48000.0;
    auto fill = SessionState::get().getExportFill (sr);
    if (fill == nullptr || fill->empty() || (*fill)[0].empty())
        return;

    chooser_ = std::make_unique<juce::FileChooser> ("Export ToneFill ambience", juce::File(), "*.wav");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                           [fill, sr] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.withFileExtension ("wav").createOutputStream());
        if (stream == nullptr) return;
        const int numCh = (int) fill->size();
        const int numSamples = (int) (*fill)[0].size();
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, (unsigned int) numCh, 24, {}, 0));
        if (writer == nullptr) return;
        stream.release();
        std::vector<const float*> ptrs;
        for (const auto& c : *fill) ptrs.push_back (c.data());
        writer->writeFromFloatArrays (ptrs.data(), numCh, numSamples);
    });
}
} // namespace tonefill::plugin::ui
