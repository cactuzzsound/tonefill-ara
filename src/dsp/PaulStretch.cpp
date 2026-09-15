#include "dsp/PaulStretch.h"
#include "dsp/FFTWrapper.h"
#include "dsp/SeededRng.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
namespace
{
int roundUpPow2 (int v)
{
    int p = 256;
    while (p < v && p < (1 << 16)) p <<= 1;
    return p;
}
} // namespace

void paulStretch (std::vector<std::vector<float>>& out, int n,
                  const std::vector<std::vector<float>>& src,
                  int windowSize, std::uint64_t seed)
{
    const int outCh = (int) out.size();
    if (outCh <= 0 || n <= 0 || src.empty() || src[0].empty()) return;

    const int srcCh  = (int) src.size();
    const int srcLen = (int) src[0].size();

    int W = roundUpPow2 (std::max (256, windowSize));
    W = std::min (W, roundUpPow2 (std::max (256, srcLen))); // don't exceed the material
    if (W < 64) return;

    const int hopOut = W / 4; // 75% overlap: smoother, fewer phase-flutter artifacts than 50%
    const int bins   = W / 2 + 1;
    const int numFrames = n / hopOut + 2;
    const double inStep = (double) srcLen / (double) std::max (1, numFrames); // one slow pass, wrapping

    // PaulStretch window: (1 - (2t-1)^2)^1.25.
    std::vector<float> win ((std::size_t) W);
    for (int i = 0; i < W; ++i)
    {
        const double t = (double) i / (double) (W - 1);
        win[(std::size_t) i] = (float) std::pow (std::max (0.0, 1.0 - std::pow (2.0 * t - 1.0, 2.0)), 1.25);
    }

    FFTWrapper fft (W);
    SeededRng master (seed);
    const float twoPi = 6.2831853071795864f;
    // Seed also chooses WHERE in the material the slow pass begins, so Regenerate gives an audibly
    // different take (not just a different phase realisation, which sounds identical on steady tone).
    const int startOffset = (int) (SeededRng (seed ^ 0x2545F4914F6CDD1DULL).nextFloat() * (float) srcLen);

    std::vector<float> frame ((std::size_t) W), y ((std::size_t) W);
    std::vector<float> re ((std::size_t) bins), im ((std::size_t) bins), reOut ((std::size_t) bins), imOut ((std::size_t) bins);
    std::vector<float> mags ((std::size_t) bins);

    for (int c = 0; c < outCh; ++c)
    {
        const auto& s = src[(std::size_t) std::min (c, srcCh - 1)];
        auto& o = out[(std::size_t) c];
        std::fill (o.begin(), o.end(), 0.0f);
        auto rng = master.deriveSubStream ((std::uint64_t) c);

        for (int f = 0; f < numFrames; ++f)
        {
            const int inStart = startOffset + (int) ((double) f * inStep);
            for (int i = 0; i < W; ++i)
                frame[(std::size_t) i] = s[(std::size_t) ((inStart + i) % srcLen)] * win[(std::size_t) i];

            fft.forward (frame.data(), re.data(), im.data());
            for (int k = 0; k < bins; ++k)
                mags[(std::size_t) k] = std::sqrt (re[(std::size_t) k] * re[(std::size_t) k] + im[(std::size_t) k] * im[(std::size_t) k]);

            for (int k = 0; k < bins; ++k)
            {
                // Smooth the magnitude across frequency: a single noise frame's spectrum is jagged,
                // and that jaggedness + random phase is what "boils" into audible hiss. A short
                // moving average keeps the room's colour but calms the roughness.
                const int a = std::max (0, k - 3), b = std::min (bins - 1, k + 3);
                float sm = 0.0f; for (int j = a; j <= b; ++j) sm += mags[(std::size_t) j];
                sm /= (float) (b - a + 1);

                // DC and Nyquist must stay real for a real signal; randomise phase elsewhere.
                if (k == 0 || k == bins - 1) { reOut[(std::size_t) k] = sm; imOut[(std::size_t) k] = 0.0f; }
                else { const float ph = rng.nextFloat() * twoPi; reOut[(std::size_t) k] = sm * std::cos (ph); imOut[(std::size_t) k] = sm * std::sin (ph); }
            }
            fft.inverse (reOut.data(), imOut.data(), y.data());

            const int outStart = f * hopOut;
            for (int i = 0; i < W; ++i)
            {
                const int idx = outStart + i;
                if (idx >= 0 && idx < n) o[(std::size_t) idx] += y[(std::size_t) i] * win[(std::size_t) i];
            }
        }
    }
}
} // namespace tonefill::dsp
