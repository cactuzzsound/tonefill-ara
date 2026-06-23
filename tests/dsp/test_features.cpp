#include <catch2/catch_test_macros.hpp>

#include "dsp/Stft.h"
#include "dsp/Features.h"
#include "golden/fixtures.h"

#include <vector>

using namespace tonefill::dsp;

namespace
{
// Analyze one mid-buffer frame and return its features.
SpectralFrameFeatures analyzeMidFrame (const juce::AudioBuffer<float>& buf, double sr)
{
    StftConfig cfg; // 2048 / 512 / Hann
    Stft stft (cfg);
    std::vector<float> mag, phase;
    const int start = buf.getNumSamples() / 2 - cfg.fftSize / 2;
    stft.analyze (buf.getReadPointer (0), buf.getNumSamples(), start, mag, phase);

    const double binHz = sr / cfg.fftSize;
    return computeFrameFeatures (stft.lastRawFrame().data(), cfg.fftSize,
                                 mag.data(), nullptr, (int) mag.size(), binHz);
}
} // namespace

TEST_CASE ("White noise is flat; a sine is tonal")
{
    const double sr = 48000.0;
    auto noise = tonefill::test::whiteNoise (1, 48000, 7);
    auto tone  = tonefill::test::sine (1, 48000, sr, 1000.0);

    const auto nf = analyzeMidFrame (noise, sr);
    const auto tf = analyzeMidFrame (tone,  sr);

    CHECK (nf.flatness > 0.2f);   // broadband -> relatively flat
    CHECK (tf.flatness < 0.05f);  // single tone -> very peaky
    CHECK (nf.flatness > tf.flatness);
}

TEST_CASE ("Spectral centroid tracks a known sine frequency")
{
    const double sr = 48000.0;
    auto tone = tonefill::test::sine (1, 48000, sr, 2000.0);
    const auto f = analyzeMidFrame (tone, sr);
    // Within a couple of bins of 2 kHz (bin spacing ~23.4 Hz at 2048/48k).
    CHECK (f.centroidHz > 1850.0f);
    CHECK (f.centroidHz < 2150.0f);
}

TEST_CASE ("Low-band ratio is high for mains hum")
{
    const double sr = 48000.0;
    auto h = tonefill::test::hum (1, 48000, sr, 50.0);
    const auto f = analyzeMidFrame (h, sr);
    CHECK (f.lowBandRatio > 0.5f); // most energy below 200 Hz
}
