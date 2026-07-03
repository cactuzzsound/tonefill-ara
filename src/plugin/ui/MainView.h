#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"
#include "plugin/SessionState.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace tonefill::plugin { class PluginProcessor; }

namespace tonefill::plugin::ui
{
class WaveformWindow;

class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (PluginProcessor& processor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void exportWav();
    void updateEmphasis();
    void pushSelections();                 // selections_ -> SessionState (source-sample ranges)
    int  xToSample (int x) const;          // waveform x -> source sample
    float sampleToX (int sample) const;    // source sample -> waveform x
    void drawKnobIcon (juce::Graphics&, juce::Rectangle<float> box, int icon, juce::Colour) const;

    PluginProcessor& processor_;
    ToneFillLookAndFeel lnf_;
    juce::TooltipWindow tooltip_ { this, 600 }; // shows hover help for every control

    juce::Label titleLbl_, subLbl_;

    // Per-knob glyph drawn at the card's top-left (matches the reference UI).
    enum Icon { IcSparkle, IcDialog, IcTarget, IcCross, IcShuffle, IcSine, IcWave, IcSliders, IcClock };
    struct Knob { juce::Slider slider; juce::Label label; int icon = 0; };
    Knob threshold_, speech_, fragment_, blend_, variation_, minFill_, flatness_, gain_, length_;

    // Card + value-box rectangles for the knobs, rebuilt in resized(), drawn in paint().
    struct CardLayout { juce::Rectangle<int> card, value; int icon; };
    std::vector<CardLayout> cards_;

    juce::TextButton regenBtn_ { "Regenerate" }, exportBtn_ { "Export WAV" };
    juce::TextButton autoBtn_ { "Auto" }, manualBtn_ { "Manual" }; // learn-source mode toggle
    juce::TextButton enhanceBtn_ { "Enhance" };                    // PaulStretch resynthesis on/off
    juce::TextButton wholeBtn_ { "Full" };                         // analyze whole item vs first 4 min
    juce::TextButton waveBtn_ { "Waveform" };                      // opens the large waveform window

    // Loudness normalize: bake the fill to a dBFS-peak or LUFS target (disables the Output knob).
    juce::TextButton normBtn_ { "Normalize" };
    juce::Slider     normTarget_;
    juce::ComboBox   normUnit_;
    juce::Label      normReadout_;

    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> thA_, spA_, frA_, blA_, vaA_, mfA_, flA_, gaA_, leA_, ntA_;
    std::unique_ptr<BA>  neA_, wfA_, enA_;
    std::unique_ptr<CBA> nuA_;

    void openWaveformWindow();
    std::unique_ptr<WaveformWindow> waveWin_;

    std::unique_ptr<juce::FileChooser> chooser_;
    SessionState::WaveData wave_;
    float meterDb_ = -120.0f;
    juce::Rectangle<int> waveArea_, waveRuler_, meterArea_;

    // Manual learn-region selection (source-sample coordinates).
    bool manualMode_ = false;
    std::vector<std::pair<int, int>> selections_;
    bool dragging_ = false;
    int  dragStartSample_ = 0, dragCurSample_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace tonefill::plugin::ui
