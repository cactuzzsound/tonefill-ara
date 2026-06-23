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

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::fragment, 1 }, "Fragment", pct(), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::blend, 1 },    "Blend",    pct(), 0.3f));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::tonalRetention, 1 }, "Tonal Retention", pct(), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::textureAmount, 1 },  "Texture Amount",  pct(), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::movement, 1 },        "Movement",        pct(), 0.2f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::threshold, 1 },       "Threshold",       pct(), 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::randomness, 1 },      "Randomness",      pct(), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::stereoWidth, 1 },     "Stereo Width",    pct(), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::crossfadeMs, 1 },     "Crossfade",
        NormalisableRange<float> (5.0f, 250.0f, 1.0f), 40.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::outputGain, 1 }, "Output Gain",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    // Seed is deliberately not a parameter: it is plain plugin state owned by the
    // processor (not automatable; must round-trip as an exact 64-bit value).

    // Analysis-affecting parameters.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::learnWindowSec, 1 }, "Learn Window",
        NormalisableRange<float> (0.5f, 10.0f, 0.1f), 4.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::grainSizeMs, 1 }, "Grain Size",
        NormalisableRange<float> (40.0f, 300.0f, 1.0f), 120.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::tonalSensitivity, 1 }, "Tonal Sensitivity", pct(), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::speechReject, 1 },     "Speech Reject",     pct(), 0.6f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::leftRightBias, 1 }, "L/R Bias",
        NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

    return layout;
}

engine::model::RenderSettings
ParameterState::toRenderSettings (long long targetDurationSamples,
                                  double targetSampleRate,
                                  int targetChannels,
                                  std::uint64_t seed) const
{
    using engine::model::Mode;
    engine::model::RenderSettings s;

    s.mode           = static_cast<Mode> ((int) apvts.getRawParameterValue (IDs::mode)->load());
    s.tonalRetention = apvts.getRawParameterValue (IDs::tonalRetention)->load();
    s.textureAmount  = apvts.getRawParameterValue (IDs::textureAmount)->load();
    s.movement       = apvts.getRawParameterValue (IDs::movement)->load();
    s.randomness     = apvts.getRawParameterValue (IDs::randomness)->load();
    s.stereoWidth    = apvts.getRawParameterValue (IDs::stereoWidth)->load();
    s.crossfadeMs    = apvts.getRawParameterValue (IDs::crossfadeMs)->load();
    s.seed           = seed;

    s.targetDurationSamples = targetDurationSamples;
    s.targetSampleRate      = targetSampleRate;
    s.targetChannels        = targetChannels;
    return s;
}

bool ParameterState::requiresReanalysis (const juce::String& paramID)
{
    return paramID == IDs::learnWindowSec
        || paramID == IDs::grainSizeMs
        || paramID == IDs::tonalSensitivity
        || paramID == IDs::speechReject
        || paramID == IDs::leftRightBias;
}
} // namespace tonefill::plugin
