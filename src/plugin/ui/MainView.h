#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/ui/ToneFillLookAndFeel.h"
#include "plugin/SessionState.h"

#include <array>
#include <memory>

namespace tonefill::plugin { class PluginProcessor; }

namespace tonefill::plugin::ui
{
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (PluginProcessor& processor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void exportWav();
    void setMode (int modeIndex);
    void updateEmphasis (int modeIndex);

    PluginProcessor& processor_;
    ToneFillLookAndFeel lnf_;

    juce::Label titleLbl_, subLbl_;
    std::array<juce::TextButton, 4> tabs_;             // display order: Ambience, Static, Hybrid, Complex
    static constexpr int tabMode_[4] = { 3, 0, 1, 2 }; // -> engine mode index

    struct Knob { juce::Slider slider; juce::Label label; };
    Knob threshold_, fragment_, blend_, tonal_, movement_, gain_;

    juce::TextButton regenBtn_ { "Regenerate" }, exportBtn_ { "Export WAV" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> thA_, frA_, blA_, toA_, moA_, gaA_;

    std::unique_ptr<juce::FileChooser> chooser_;
    SessionState::WaveData wave_;
    float meterDb_ = -120.0f;
    juce::Rectangle<int> waveArea_, meterArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace tonefill::plugin::ui
