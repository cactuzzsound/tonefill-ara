#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace tonefill::plugin { class PluginProcessor; }

namespace tonefill::plugin::ui
{
// Editor: knobs grouped by use (Ambience / Synthesis / Output), mode selector, Regenerate +
// Export WAV, an output meter, and a live status read-out. A timer mirrors parameters into the
// shared SessionState (so the ARA worker re-renders) and refreshes meter/status.
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

    PluginProcessor& processor_;

    juce::Label    titleLbl_, statusLbl_, modeLbl_, ambSecLbl_, synthSecLbl_, outSecLbl_;
    juce::ComboBox modeBox_;
    juce::TextButton regenButton_ { "Regenerate" }, exportButton_ { "Export WAV" };

    struct Knob { juce::Slider slider; juce::Label label; };
    Knob threshold_, fragment_, blend_, tonal_, texture_, movement_, outGain_;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<CA> modeAtt_;
    std::unique_ptr<SA> thresholdAtt_, fragmentAtt_, blendAtt_, tonalAtt_, textureAtt_, movementAtt_, outGainAtt_;

    std::unique_ptr<juce::FileChooser> chooser_;
    float meterDb_ = -120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
} // namespace tonefill::plugin::ui
