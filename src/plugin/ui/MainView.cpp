#include "plugin/ui/MainView.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ParameterState.h"
#include "plugin/SessionState.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace tonefill::plugin::ui
{
using IDs = ParameterState::IDs;

namespace
{
void initKnob (juce::Component& parent, juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    parent.addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (12.0f));
    parent.addAndMakeVisible (l);
}
} // namespace

MainView::MainView (PluginProcessor& processor) : processor_ (processor)
{
    auto& apvts = processor_.parameters().apvts;

    titleLbl_.setText ("ToneFill  -  dialogue room tone fill", juce::dontSendNotification);
    addAndMakeVisible (titleLbl_);
    statusLbl_.setJustificationType (juce::Justification::topLeft);
    statusLbl_.setFont (juce::Font (12.0f));
    addAndMakeVisible (statusLbl_);

    modeLbl_.setText ("Mode", juce::dontSendNotification);
    addAndMakeVisible (modeLbl_);
    modeBox_.addItem ("Static", 1);  modeBox_.addItem ("Hybrid", 2);
    modeBox_.addItem ("Complex", 3); modeBox_.addItem ("Ambience", 4);
    addAndMakeVisible (modeBox_);
    modeAtt_ = std::make_unique<CA> (apvts, IDs::mode, modeBox_);

    addAndMakeVisible (regenButton_);
    regenButton_.onClick = [] { auto& ss = SessionState::get(); ss.seed.fetch_add (0x9E3779B97F4A7C15ULL); ss.generation.fetch_add (1); };
    addAndMakeVisible (exportButton_);
    exportButton_.onClick = [this] { exportWav(); };

    auto section = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    section (ambSecLbl_,   "Ambience (real fragments)");
    section (synthSecLbl_, "Synthesis (Static / Hybrid / Complex)");
    section (outSecLbl_,   "Output");

    initKnob (*this, threshold_.slider, threshold_.label, "Threshold");
    initKnob (*this, fragment_.slider,  fragment_.label,  "Fragment");
    initKnob (*this, blend_.slider,     blend_.label,     "Blend");
    initKnob (*this, tonal_.slider,     tonal_.label,     "Tonal");
    initKnob (*this, texture_.slider,   texture_.label,   "Texture");
    initKnob (*this, movement_.slider,  movement_.label,  "Movement");
    initKnob (*this, outGain_.slider,   outGain_.label,   "Gain");

    thresholdAtt_ = std::make_unique<SA> (apvts, IDs::threshold,      threshold_.slider);
    fragmentAtt_  = std::make_unique<SA> (apvts, IDs::fragment,       fragment_.slider);
    blendAtt_     = std::make_unique<SA> (apvts, IDs::blend,          blend_.slider);
    tonalAtt_     = std::make_unique<SA> (apvts, IDs::tonalRetention, tonal_.slider);
    textureAtt_   = std::make_unique<SA> (apvts, IDs::textureAmount,  texture_.slider);
    movementAtt_  = std::make_unique<SA> (apvts, IDs::movement,       movement_.slider);
    outGainAtt_   = std::make_unique<SA> (apvts, IDs::outputGain,     outGain_.slider);

    startTimerHz (15);
}

MainView::~MainView() { stopTimer(); }

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));

    // Output meter bar (drawn in the area reserved by resized()).
    const auto m = getLocalBounds().reduced (12);
    const int barY = getHeight() - 30;
    juce::Rectangle<int> bar (m.getX(), barY, m.getWidth() - 170, 14);
    g.setColour (juce::Colour (0xff2a2e38));
    g.fillRect (bar);
    const float norm = juce::jlimit (0.0f, 1.0f, (meterDb_ + 60.0f) / 60.0f); // -60..0 dB
    g.setColour (meterDb_ > -3.0f ? juce::Colours::orange : juce::Colour (0xff5fd08a));
    g.fillRect (bar.withWidth ((int) (bar.getWidth() * norm)));
    g.setColour (juce::Colours::grey);
    g.drawText ("out " + juce::String (meterDb_, 1) + " dB", bar.translated (bar.getWidth() + 8, 0).withWidth (160),
                juce::Justification::centredLeft);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (12);
    titleLbl_.setBounds (r.removeFromTop (24));
    statusLbl_.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);

    auto top = r.removeFromTop (26);
    modeLbl_.setBounds (top.removeFromLeft (44));
    modeBox_.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (12);
    regenButton_.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    exportButton_.setBounds (top.removeFromLeft (110));
    r.removeFromTop (10);

    auto knobRow = [&r] (juce::Label& sec, std::initializer_list<MainView::Knob*> knobs)
    {
        sec.setBounds (r.removeFromTop (18));
        auto row = r.removeFromTop (92);
        int x = row.getX();
        for (auto* k : knobs)
        {
            juce::Rectangle<int> cell (x, row.getY(), 86, 92);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
            x += 92;
        }
        r.removeFromTop (8);
    };
    knobRow (ambSecLbl_,   { &threshold_, &fragment_, &blend_ });
    knobRow (synthSecLbl_, { &tonal_, &texture_, &movement_ });

    outSecLbl_.setBounds (r.removeFromTop (18));
    outGain_.label.setBounds (r.getX(), r.getY(), 86, 16);
    outGain_.slider.setBounds (r.getX(), r.getY() + 16, 86, 76);
}

void MainView::timerCallback()
{
    auto& apvts = processor_.parameters().apvts;
    auto& ss = SessionState::get();

    bool changed = false;
    const int mode = (int) apvts.getRawParameterValue (IDs::mode)->load();
    if (ss.mode.load() != mode) { ss.mode.store (mode); changed = true; }
    auto mirror = [&changed] (std::atomic<float>& dst, float v)
    {
        if (std::abs (dst.load() - v) > 1.0e-4f) { dst.store (v); changed = true; }
    };
    mirror (ss.threshold,      apvts.getRawParameterValue (IDs::threshold)->load());
    mirror (ss.fragment,       apvts.getRawParameterValue (IDs::fragment)->load());
    mirror (ss.blend,          apvts.getRawParameterValue (IDs::blend)->load());
    mirror (ss.tonalRetention, apvts.getRawParameterValue (IDs::tonalRetention)->load());
    mirror (ss.textureAmount,  apvts.getRawParameterValue (IDs::textureAmount)->load());
    mirror (ss.movement,       apvts.getRawParameterValue (IDs::movement)->load());
    if (changed) ss.generation.fetch_add (1);

    // Output gain is read on the audio thread; mirror it (linear).
    ss.outputGain.store (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (IDs::outputGain)->load()));

    const int phase = ss.phase.load();
    const juce::String ph = phase == 0 ? "idle" : (phase == 1 ? "analyzing..." : "ready");
    statusLbl_.setText ("Status: " + ph
                        + "    Partials: " + juce::String (ss.numPartials.load())
                        + "    Clean: " + juce::String (ss.learnSeconds.load(), 1) + " s"
                        + "    In: " + juce::String (ss.levelDb.load(), 1) + " dB",
                        juce::dontSendNotification);

    meterDb_ = meterDb_ * 0.7f + ss.outMeterDb.load() * 0.3f; // smooth
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
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sr, (unsigned int) numCh, 24, {}, 0));
        if (writer == nullptr) return;
        stream.release();

        std::vector<const float*> ptrs;
        for (const auto& c : *fill) ptrs.push_back (c.data());
        writer->writeFromFloatArrays (ptrs.data(), numCh, numSamples);
    });
}
} // namespace tonefill::plugin::ui
