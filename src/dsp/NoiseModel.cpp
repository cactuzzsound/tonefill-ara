#include "dsp/NoiseModel.h"
#include "dsp/Stft.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
NoiseSpectrum estimateNoiseSpectrum (const juce::AudioBuffer<float>& clean, int fftSize, int hop, int maxFrames)
{
    NoiseSpectrum m;
    const int ch = clean.getNumChannels();
    const int n  = clean.getNumSamples();
    if (ch <= 0 || n < fftSize) return m;

    Stft stft ({ fftSize, hop, WindowType::Hann });
    const int bins = stft.numBins();
    m.fftSize = fftSize;
    m.numBins = bins;
    m.meanMag.assign ((std::size_t) ch, std::vector<float> ((std::size_t) bins, 0.0f));

    const int totalFrames = Stft::numFrames (n, hop);
    const int stride = juce::jmax (1, totalFrames / juce::jmax (1, maxFrames));

    std::vector<float> mag, ph;
    for (int c = 0; c < ch; ++c)
    {
        // Collect magnitudes per bin across the (strided) frames.
        std::vector<std::vector<float>> perBin ((std::size_t) bins);
        for (int f = 0; f < totalFrames; f += stride)
        {
            stft.analyze (clean.getReadPointer (c), n, f * hop, mag, ph);
            for (int k = 0; k < bins; ++k) perBin[(std::size_t) k].push_back (mag[(std::size_t) k]);
        }
        // Robust mean from the [10th, 70th] percentile band of each bin.
        for (int k = 0; k < bins; ++k)
        {
            auto& v = perBin[(std::size_t) k];
            if (v.empty()) { m.meanMag[(std::size_t) c][(std::size_t) k] = 0.0f; continue; }
            std::sort (v.begin(), v.end());
            const int a = (int) (v.size() * 0.10), b = juce::jmax (a + 1, (int) (v.size() * 0.70));
            double s = 0.0; int cnt = 0;
            for (int i = a; i < b && i < (int) v.size(); ++i) { s += v[(std::size_t) i]; ++cnt; }
            m.meanMag[(std::size_t) c][(std::size_t) k] = cnt > 0 ? (float) (s / cnt) : 0.0f;
        }
    }
    return m;
}
} // namespace tonefill::dsp
