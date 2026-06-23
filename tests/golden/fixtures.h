#pragma once

// Deterministic synthetic test signals (SPRINTS TA-4). Generated from code so no binary input
// files live in git, and inputs are reproducible across platforms/commits.

#include <juce_audio_basics/juce_audio_basics.h>
#include "dsp/SeededRng.h"

#include <cmath>
#include <cstdint>

namespace tonefill::test
{
inline juce::AudioBuffer<float> makeBuffer (int channels, int numSamples)
{
    juce::AudioBuffer<float> b (channels, numSamples);
    b.clear();
    return b;
}

// Seeded white noise (independent stream per channel), amplitude ~0.25.
inline juce::AudioBuffer<float> whiteNoise (int channels, int numSamples, std::uint64_t seed)
{
    auto b = makeBuffer (channels, numSamples);
    dsp::SeededRng master (seed);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto rng = master.deriveSubStream ((std::uint64_t) ch);
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return b;
}

// Pure sine, same on all channels.
inline juce::AudioBuffer<float> sine (int channels, int numSamples, double sr, double freqHz)
{
    auto b = makeBuffer (channels, numSamples);
    const double w = 2.0 * juce::MathConstants<double>::pi * freqHz / sr;
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = 0.25f * (float) std::sin (w * i);
    }
    return b;
}

// Mains hum: fundamental + a few harmonics + a little noise (the tonal-tracker target).
inline juce::AudioBuffer<float> hum (int channels, int numSamples, double sr,
                                     double f0 = 50.0, std::uint64_t seed = 1)
{
    auto b = makeBuffer (channels, numSamples);
    const double pi2 = 2.0 * juce::MathConstants<double>::pi;
    const double amps[4] = { 0.20, 0.10, 0.05, 0.025 }; // 1st..4th harmonic
    dsp::SeededRng master (seed);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto rng = master.deriveSubStream ((std::uint64_t) ch);
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            double s = 0.0;
            for (int h = 0; h < 4; ++h)
                s += amps[h] * std::sin (pi2 * f0 * (h + 1) * i / sr);
            s += 0.02 * (rng.nextFloat() * 2.0 - 1.0); // light noise floor
            d[i] = (float) s;
        }
    }
    return b;
}

// Periodic impulse train (transient/flux test material).
inline juce::AudioBuffer<float> impulseTrain (int channels, int numSamples, int periodSamples)
{
    auto b = makeBuffer (channels, numSamples);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < numSamples; i += juce::jmax (1, periodSamples))
            d[i] = 0.5f;
    }
    return b;
}
} // namespace tonefill::test
