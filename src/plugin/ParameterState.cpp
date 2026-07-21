#include "plugin/ParameterState.h"

namespace tonefill::plugin
{
using APVTS = juce::AudioProcessorValueTreeState;

ParameterState::ParameterState (juce::AudioProcessor& owner)
    : apvts (owner, nullptr, "PARAMETERS", createLayout())
{
}

APVTS::ParameterLayout ParameterState::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterChoice;
    using juce::ParameterID;
    using juce::NormalisableRange;

    APVTS::ParameterLayout layout;

    // Mode order MUST match engine::model::Mode (Static, Hybrid, Complex, Ambience).
    // Default = Ambience (real-fragment fill) — the natural-sounding one.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { IDs::mode, 1 }, "Mode",
        juce::StringArray { "Static", "Hybrid", "Complex", "Ambience" }, 3));

    auto pct = [] { return NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::threshold, 1 },      "Clean Level",   pct(), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::speechReject, 1 },   "Voice Reject",  pct(), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::fragment, 1 },       "Chunk Size",    pct(), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::blend, 1 },          "Crossfade",     pct(), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::randomness, 1 },     "Smoothness",    pct(), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::tonalRetention, 1 }, "Hum Level",     pct(), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::minFill, 1 },  "Min Fill",
        NormalisableRange<float> (0.2f, 5.0f, 0.05f), 2.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::flatness, 1 }, "Flatness", pct(), 0.7f));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::paulStretch, 1 }, "Enhance", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::outputGain, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::normEnabled, 1 }, "Normalize", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::normTarget, 1 }, "Norm Target",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -16.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::normUnit, 1 }, "Norm Unit",
        juce::StringArray { "dBFS", "LUFS" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::wholeFile, 1 }, "Analyze Whole File", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::statistical, 1 }, "Statistical Selection", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::spectral, 1 }, "Spectral Mosaic", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::learnMode, 1 }, "Learn", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::renderLength, 1 }, "Export Len",
        NormalisableRange<float> (0.5f, 30.0f, 0.1f), 5.0f));

    // Enhance-only live HF de-hiss (applied on the audio thread, tweakable in real time).
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { IDs::hissFilter, 1 }, "Hiss Filter", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::hissFreq, 1 }, "Hiss Freq",
        NormalisableRange<float> (3000.0f, 15000.0f, 1.0f, 0.5f), 9000.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::hissQ, 1 }, "Hiss Q",
        NormalisableRange<float> (0.3f, 2.0f, 0.01f), 0.707f));

    return layout;
}
} // namespace tonefill::plugin
