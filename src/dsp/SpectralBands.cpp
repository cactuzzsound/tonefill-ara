#include "dsp/SpectralBands.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::dsp
{
namespace
{
// Single 2nd-order Butterworth sections (juce::IIRCoefficients default Q = 1/sqrt2). A Butterworth
// low/high pair at the same cutoff is power-complementary: |LP|^2 + |HP|^2 = 1.
void bwLowpass (std::vector<float>& buf, double sr, double f)
{
    juce::IIRFilter flt;
    flt.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, f));
    flt.processSamples (buf.data(), (int) buf.size());
}

void bwHighpass (std::vector<float>& buf, double sr, double f)
{
    juce::IIRFilter flt;
    flt.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, f));
    flt.processSamples (buf.data(), (int) buf.size());
}
} // namespace

std::vector<std::vector<float>> splitBands (const float* x, int n, double sampleRate,
                                            const std::vector<double>& edges)
{
    std::vector<std::vector<float>> bands;
    if (n <= 0 || x == nullptr) return bands;

    // Successive complementary splits: peel off the low band at each edge, keep filtering the high
    // remainder. A tree of power-complementary splits stays power-complementary overall.
    std::vector<float> remaining (x, x + n);
    for (double f : edges)
    {
        if (f <= 0.0 || f >= sampleRate * 0.5) continue; // ignore out-of-range edges
        std::vector<float> low = remaining;
        bwLowpass  (low,       sampleRate, f);
        bwHighpass (remaining, sampleRate, f);
        bands.push_back (std::move (low));
    }
    bands.push_back (std::move (remaining));
    return bands;
}
} // namespace tonefill::dsp
