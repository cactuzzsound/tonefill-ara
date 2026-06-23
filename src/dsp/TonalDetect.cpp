#include "dsp/TonalDetect.h"
#include "dsp/Stft.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Exact amplitude/phase of frequency f via direct DFT correlation over the whole signal.
void correlate (const float* x, int n, double sr, double f, float& amp, float& phase)
{
    const double w = 2.0 * kPi * f / sr;
    double c = 0.0, s = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double th = w * i;
        c += x[i] * std::cos (th);
        s += x[i] * std::sin (th);
    }
    amp   = (float) (2.0 / n * std::sqrt (c * c + s * s));
    phase = (float) std::atan2 (c, s);
}
} // namespace

std::vector<DetectedPartial> detectPartials (const float* src, int n, double sr,
                                             int fftSize, float peakRatio, int maxPartials)
{
    std::vector<DetectedPartial> out;
    if (n <= 0 || fftSize < 64) return out;

    StftConfig cfg;
    cfg.fftSize = fftSize;
    cfg.hop     = fftSize / 4;
    Stft stft (cfg);
    const int bins = stft.numBins();

    std::vector<double> avg ((std::size_t) bins, 0.0);
    std::vector<float> mag, phase;

    int used = 0;
    for (int f = 0; f * cfg.hop + fftSize <= n; ++f)
    {
        stft.analyze (src, n, f * cfg.hop, mag, phase);
        for (int b = 0; b < bins; ++b) avg[(std::size_t) b] += mag[(std::size_t) b];
        ++used;
    }
    if (used == 0) // signal shorter than one frame: single zero-padded frame
    {
        stft.analyze (src, n, 0, mag, phase);
        for (int b = 0; b < bins; ++b) avg[(std::size_t) b] = mag[(std::size_t) b];
        used = 1;
    }
    for (int b = 0; b < bins; ++b) avg[(std::size_t) b] /= used;

    const double binHz = sr / fftSize;
    const int W = 24; // smoothed-floor half-window in bins

    auto floorAt = [&] (int b)
    {
        double sum = 0.0; int c = 0;
        for (int k = b - W; k <= b + W; ++k)
            if (k >= 0 && k < bins) { sum += avg[(std::size_t) k]; ++c; }
        return c > 0 ? sum / c : 0.0;
    };

    struct Cand { double f, a, ph; };
    std::vector<Cand> cands;

    for (int b = 2; b < bins - 2; ++b)
    {
        const double m = avg[(std::size_t) b];
        if (m > avg[(std::size_t) (b - 1)] && m >= avg[(std::size_t) (b + 1)]
            && m > peakRatio * floorAt (b))
        {
            const double a = avg[(std::size_t) (b - 1)];
            const double bb = m;
            const double c = avg[(std::size_t) (b + 1)];
            const double denom = a - 2.0 * bb + c;
            const double delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
            const double freq = ((double) b + delta) * binHz;

            float amp = 0.0f, ph = 0.0f;
            correlate (src, n, sr, freq, amp, ph);
            cands.push_back ({ freq, (double) amp, (double) ph });
        }
    }

    std::sort (cands.begin(), cands.end(),
               [] (const Cand& x, const Cand& y) { return x.a > y.a; });

    // Prune spurious peaks far weaker than the strongest tone (keeps real harmonics, drops
    // noise-floor blips). Relative gate avoids an absolute-level assumption.
    const double maxAmp = cands.empty() ? 0.0 : cands.front().a;
    const double ampGate = 0.01 * maxAmp;

    for (const auto& c : cands)
    {
        if (c.a < ampGate) break; // sorted desc -> nothing stronger remains
        bool dup = false;
        for (const auto& o : out)
            if (std::fabs ((double) o.freqHz - c.f) < 5.0) { dup = true; break; }
        if (! dup)
        {
            out.push_back ({ (float) c.f, (float) c.a, (float) c.ph });
            if ((int) out.size() >= maxPartials) break;
        }
    }
    return out;
}
} // namespace tonefill::dsp
