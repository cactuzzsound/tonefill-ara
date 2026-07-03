#pragma once

#include "engine/model/RenderSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tonefill::plugin
{
// APVTS-backed parameter source of truth (design §L).
//
// Mutability: edited on the MESSAGE thread; the audio thread reads atomic raw values only.
// Each parameter is classified as RE-ANALYSIS or RE-RENDER so the engine invalidates the
// correct cache (analysis-affecting changes must prompt the user to re-run Analyze).
class ParameterState
{
public:
    explicit ParameterState (juce::AudioProcessor& owner);

    juce::AudioProcessorValueTreeState apvts;

    // Stable parameter IDs (only the controls that actually drive the current engine).
    struct IDs
    {
        static constexpr auto mode           = "mode";
        static constexpr auto threshold      = "threshold";      // clean-ambience level selectivity
        static constexpr auto speechReject   = "speechReject";   // libfvad speech exclusion
        static constexpr auto fragment       = "fragment";       // Ambience: real-fragment length
        static constexpr auto blend          = "blend";          // Ambience: crossfade amount
        static constexpr auto randomness     = "randomness";     // variation: loop length + jitter
        static constexpr auto tonalRetention = "tonalRetention"; // (legacy, no UI)
        static constexpr auto movement       = "movement";       // (legacy, no UI)
        static constexpr auto minFill        = "minFill";        // min stable-fragment length (seconds)
        static constexpr auto flatness       = "flatness";       // stationarity strictness (0..1)
        static constexpr auto paulStretch    = "enhance";        // Enhance: PaulStretch resynthesis on/off
        static constexpr auto outputGain     = "outputGain";     // dB
        static constexpr auto normEnabled    = "normEnabled";    // normalize to a loudness target
        static constexpr auto normTarget     = "normTarget";     // target value (dBFS or LUFS)
        static constexpr auto normUnit       = "normUnit";       // 0 = dBFS (peak), 1 = LUFS
        static constexpr auto wholeFile      = "wholeFile";      // analyze whole item vs first 4 min
        static constexpr auto learnMode      = "learnMode";      // AudioSuite: learn from selection vs generate
        static constexpr auto renderLength   = "renderLength";   // Export WAV length, seconds
        // seed is plain processor state (not automatable), not an APVTS parameter.
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
};
} // namespace tonefill::plugin
