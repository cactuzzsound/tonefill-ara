#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace tonefill::dsp
{
// Build IIR coefficients for ONE parametric-EQ band. `type` matches tonefill::plugin::EqType:
// 0 Bell, 1 Low Shelf, 2 High Shelf, 3 High Pass, 4 Low Pass, 5 Notch.
// gainDb applies to Bell / shelves only; Q applies to all. Shared by the ARA and AAX render paths.
inline juce::IIRCoefficients makeEqCoefficients (double sr, int type, float freq, float gainDb, float q)
{
    const double f       = juce::jlimit (20.0, sr * 0.49, (double) freq);
    const double qq      = juce::jmax (0.05, (double) q);
    const float  gainLin = (float) std::pow (10.0, (double) gainDb / 20.0); // JUCE takes a linear gain factor

    switch (type)
    {
        case 1:  return juce::IIRCoefficients::makeLowShelf   (sr, f, qq, gainLin);
        case 2:  return juce::IIRCoefficients::makeHighShelf  (sr, f, qq, gainLin);
        case 3:  return juce::IIRCoefficients::makeHighPass    (sr, f, qq);
        case 4:  return juce::IIRCoefficients::makeLowPass     (sr, f, qq);
        case 5:  return juce::IIRCoefficients::makeNotchFilter (sr, f, qq);
        case 0:
        default: return juce::IIRCoefficients::makePeakFilter  (sr, f, qq, gainLin);
    }
}
} // namespace tonefill::dsp
