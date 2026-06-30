#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace tonefill::dsp
{
// Robust per-bin magnitude spectrum of the STATIONARY background ("room tone colour"), estimated
// from already-selected clean material. For each frequency bin we take a low-percentile band of
// the per-frame magnitudes (minimum-statistics idea): residual transients / loud bits sit in the
// high percentiles and are ignored, so the model reflects only the steady noise floor's timbre.
//
// This drives spectral RE-SYNTHESIS (see SpectralResynth): a constant, joint-free bed that sounds
// like the real room because room tone IS noise shaped to this spectrum.
struct NoiseSpectrum
{
    int fftSize = 2048;
    int numBins = 0;
    std::vector<std::vector<float>> meanMag; // [channel][bin] robust mean magnitude

    bool valid() const noexcept { return numBins > 0 && ! meanMag.empty() && ! meanMag[0].empty(); }
};

// fftSize must be a power of two. Analyses up to ~maxFrames frames (strided) for bounded cost.
NoiseSpectrum estimateNoiseSpectrum (const juce::AudioBuffer<float>& clean,
                                     int fftSize = 2048, int hop = 512, int maxFrames = 4000);
} // namespace tonefill::dsp
