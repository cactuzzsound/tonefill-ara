#include "dsp/SpectralBands.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tonefill::dsp
{
namespace
{
// LR4 = two cascaded 2nd-order Butterworth sections (each juce::IIRCoefficients default Q = 1/sqrt2).
void lrLowpass (std::vector<float>& buf, double sr, double f)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::IIRFilter flt;
        flt.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, f));
        flt.processSamples (buf.data(), (int) buf.size());
    }
}

void lrHighpass (std::vector<float>& buf, double sr, double f)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::IIRFilter flt;
        flt.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, f));
        flt.processSamples (buf.data(), (int) buf.size());
    }
}
} // namespace

std::vector<std::vector<float>> splitBandsLR (const float* x, int n, double sampleRate,
                                              const std::vector<double>& edges)
{
    std::vector<std::vector<float>> bands;
    if (n <= 0 || x == nullptr) return bands;

    // Successive complementary splits: at each edge, peel off the low band and keep filtering the
    // remaining high part. The last band is whatever is left above the top edge.
    std::vector<float> remaining (x, x + n);
    for (double f : edges)
    {
        if (f <= 0.0 || f >= sampleRate * 0.5) continue; // ignore out-of-range edges
        std::vector<float> low = remaining;
        lrLowpass  (low,       sampleRate, f);
        lrHighpass (remaining, sampleRate, f);
        bands.push_back (std::move (low));
    }
    bands.push_back (std::move (remaining));
    return bands;
}
} // namespace tonefill::dsp
