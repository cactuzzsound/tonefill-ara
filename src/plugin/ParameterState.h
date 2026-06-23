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

    // Snapshot the render-affecting parameters into a RenderSettings for a render job.
    // `seed` is NOT an APVTS parameter (not automatable, must round-trip exactly); the
    // processor owns it as plain serialized state and passes it in here.
    engine::model::RenderSettings toRenderSettings (long long targetDurationSamples,
                                                    double targetSampleRate,
                                                    int targetChannels,
                                                    std::uint64_t seed) const;

    // True if `paramID` requires re-analysis (vs. only re-render). Drives the "Analyze"
    // prompt in the UI.
    static bool requiresReanalysis (const juce::String& paramID);

    // Stable parameter IDs.
    struct IDs
    {
        static constexpr auto mode            = "mode";
        static constexpr auto tonalRetention  = "tonalRetention";
        static constexpr auto textureAmount   = "textureAmount";
        static constexpr auto movement        = "movement";
        static constexpr auto fragment        = "fragment";   // Ambience: real-fragment length
        static constexpr auto blend           = "blend";      // Ambience: crossfade amount
        static constexpr auto threshold       = "threshold";  // clean-ambience selectivity (re-analysis)
        static constexpr auto randomness      = "randomness";
        static constexpr auto stereoWidth     = "stereoWidth";
        static constexpr auto crossfadeMs      = "crossfadeMs";
        static constexpr auto outputGain       = "outputGain"; // dB
        // Note: seed is intentionally NOT an APVTS parameter (see toRenderSettings).
        // Analysis-affecting:
        static constexpr auto learnWindowSec  = "learnWindowSec";
        static constexpr auto grainSizeMs     = "grainSizeMs";
        static constexpr auto tonalSensitivity = "tonalSensitivity";
        static constexpr auto speechReject    = "speechReject";
        static constexpr auto leftRightBias   = "leftRightBias";
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
};
} // namespace tonefill::plugin
