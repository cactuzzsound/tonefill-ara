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

    struct Knob { juce::Slider slider; juce::Label label; };

private:
    void timerCallback() override;
    void exportWav();
    void updateEmphasis();
    void pushSelections();                 // selections_ -> SessionState (source-sample ranges)
    void loadParamsFromState (SessionState& ss); // clip switch: SessionState -> knobs (per-clip params)
    int  xToSample (int x) const;          // waveform x -> source sample
    float sampleToX (int sample) const;    // source sample -> waveform x
    void drawGroupIcon (juce::Graphics&, juce::Rectangle<float> box, int icon, juce::Colour) const;
    void openWaveformWindow();

    PluginProcessor& processor_;
    ToneFillLookAndFeel lnf_;
    juce::TooltipWindow tooltip_ { this, 600 };

    juce::Label titleLbl_, subLbl_;

    Knob threshold_, speech_, fragment_, blend_, variation_, minFill_, flatness_, gain_, length_;

    // Value-pill rectangles for the 9 knobs, rebuilt in resized(), drawn in paint().
    std::vector<juce::Rectangle<int>> valuePills_;

    // Three group cards (Detection / Structure / Texture): background + header rects.
    std::array<juce::Rectangle<int>, 3> groupCard_;
    enum Gicon { GiDetect, GiStruct, GiTexture };

    juce::TextButton regenBtn_ { "Regenerate" }, exportBtn_ { "Export WAV" };
    juce::TextButton autoBtn_ { "Auto" }, manualBtn_ { "Manual" };       // learn-source mode
    juce::TextButton enhanceBtn_ { "Enhance" }, wholeBtn_ { "Full" };    // processing group
    juce::TextButton classicBtn_ { "Classic" }, expBtn_ { "Experimental" }; // selection engine
    juce::TextButton expandBtn_ { "Expand" };                            // open large waveform window

    // Loudness normalize.
    juce::TextButton normBtn_ { "Normalize" };
    juce::Slider     normTarget_;
    juce::ComboBox   normUnit_;
    juce::Label      normReadout_;

    // Enhance-only live HF de-hiss (separate panel under Texture).
    juce::TextButton hissBtn_ { "Hiss Filter" };
    Knob hissFreq_, hissQ_;

    // Rotating tip.
    juce::Label tipLbl_;
    std::array<juce::String, 11> tips_;
    int tipIdx_ = 0, tipTick_ = 0;

    using SA  = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA  = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CBA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> thA_, spA_, frA_, blA_, vaA_, mfA_, flA_, gaA_, leA_, ntA_, hfrA_, hqA_;
    std::unique_ptr<BA>  neA_, wfA_, enA_, hbA_;
    std::unique_ptr<CBA> nuA_;

    std::unique_ptr<WaveformWindow> waveWin_;
    std::unique_ptr<juce::FileChooser> chooser_;
    const SessionState* shownState_ = nullptr; // detect the editor re-pointing to another clip
    int  loadedEpoch_ = -1;                    // paramsEpoch last loaded; reload if a restore bumps it

    SessionState::WaveData wave_;
    float meterDb_ = -120.0f;
    juce::Rectangle<int> headerGroups_, normCard_, hissCard_, bottomCard_, tipCard_;
    juce::Rectangle<int> waveArea_, waveRuler_, dataArea_, meterArea_;

    // Manual learn-region selection (source-sample coordinates).
    bool manualMode_ = false;
    std::vector<std::pair<int, int>> selections_;
    bool dragging_ = false;
    int  dragStartSample_ = 0, dragCurSample_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace tonefill::plugin::ui
