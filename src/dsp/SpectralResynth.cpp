#include "dsp/SpectralResynth.h"
#include "dsp/FFTWrapper.h"
#include "dsp/SeededRng.h"

#include <cmath>
#include <vector>

namespace tonefill::dsp
{
void synthesizeBed (juce::AudioBuffer<float>& out, int numSamples,
                    const NoiseSpectrum& model, std::uint64_t seed, float targetRms)
{
    const int ch = out.getNumChannels();
    if (! model.valid() || ch <= 0 || numSamples <= 0) return;

    const int fftSize = model.fftSize;
    const int bins    = model.numBins;
    const int hop     = fftSize / 4; // 75% overlap (Hann is COLA here)
    const float twoPi = 6.2831853071795864f;
    const float invSqrtHalfPi = 1.0f / 1.2533141373155001f; // 1/sqrt(pi/2): Rayleigh -> unit mean

    // Hann synthesis window.
    std::vector<float> w ((std::size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        w[(std::size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) (fftSize - 1));

    FFTWrapper fft (fftSize);
    SeededRng master (seed);

    std::vector<float> re ((std::size_t) bins), im ((std::size_t) bins), frame ((std::size_t) fftSize);

    for (int c = 0; c < ch; ++c)
    {
        const auto& mean = model.meanMag[(std::size_t) juce::jmin (c, (int) model.meanMag.size() - 1)];
        float* o = out.getWritePointer (c);
        for (int i = 0; i < numSamples; ++i) o[i] = 0.0f;

        auto rng = master.deriveSubStream ((std::uint64_t) c); // independent per channel -> stereo width

        for (int start = -fftSize + hop; start < numSamples; start += hop)
        {
            for (int k = 0; k < bins; ++k)
            {
                const float u = juce::jmax (1.0e-7f, rng.nextFloat());
                const float ray = std::sqrt (-2.0f * std::log (u)); // Rayleigh (sigma=1)
                const float amp = mean[(std::size_t) k] * ray * invSqrtHalfPi;
                const float phase = rng.nextFloat() * twoPi;
                re[(std::size_t) k] = amp * std::cos (phase);
                im[(std::size_t) k] = amp * std::sin (phase);
            }
            fft.inverse (re.data(), im.data(), frame.data());
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = start + i;
                if (idx >= 0 && idx < numSamples) o[idx] += frame[(std::size_t) i] * w[(std::size_t) i];
            }
        }

        // Normalise the channel to the target room-tone level.
        double e = 0.0;
        for (int i = 0; i < numSamples; ++i) e += (double) o[i] * o[i];
        const double rms = std::sqrt (e / (double) juce::jmax (1, numSamples));
        if (rms > 1.0e-9 && targetRms > 0.0f)
        {
            const float g = (float) ((double) targetRms / rms);
            for (int i = 0; i < numSamples; ++i) o[i] *= g;
        }
    }
}
} // namespace tonefill::dsp
