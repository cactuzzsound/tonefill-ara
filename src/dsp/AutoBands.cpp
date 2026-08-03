#include "dsp/AutoBands.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
namespace
{
constexpr double kFLo = 25.0, kFHiCap = 18000.0;
constexpr int    kGrid = 96;   // log-frequency profile points
constexpr double kMinEdgeRatio = 1.18; // adjacent edges must be at least this far apart

// A peak's prominence: its height above the higher of the two lowest valleys flanking it.
double prominence (const std::vector<double>& p, int idx)
{
    const int n = (int) p.size();
    double leftMin = p[(std::size_t) idx];
    for (int i = idx - 1; i >= 0; --i) { if (p[(std::size_t) i] > p[(std::size_t) idx]) break; leftMin = std::min (leftMin, p[(std::size_t) i]); }
    double rightMin = p[(std::size_t) idx];
    for (int i = idx + 1; i < n; ++i) { if (p[(std::size_t) i] > p[(std::size_t) idx]) break; rightMin = std::min (rightMin, p[(std::size_t) i]); }
    return p[(std::size_t) idx] - std::max (leftMin, rightMin);
}
} // namespace

std::vector<float> autoBandEdges (const float* x, int n, double sr, int maxBands)
{
    std::vector<float> out;
    if (x == nullptr || n < 8192 || sr <= 0.0) return out;

    const double fHi = std::min (sr * 0.47, kFHiCap);
    if (fHi <= kFLo * 2.0) return out;

    // --- Welch LTAS + per-bin temporal std (stationarity) ---
    const int order = 12, fftSize = 1 << order;   // 4096
    const int hop = fftSize / 2;
    const int nbins = fftSize / 2 + 1;
    const double binHz = sr / fftSize;

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win (fftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> buf ((std::size_t) fftSize * 2);
    std::vector<double> sumDb ((std::size_t) nbins, 0.0), sumDb2 ((std::size_t) nbins, 0.0);
    int frames = 0;

    for (int pos = 0; pos + fftSize <= n; pos += hop)
    {
        for (int i = 0; i < fftSize; ++i) buf[(std::size_t) i] = x[pos + i];
        std::fill (buf.begin() + fftSize, buf.end(), 0.0f);
        win.multiplyWithWindowingTable (buf.data(), (std::size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int b = 0; b < nbins; ++b)
        {
            const double db = 20.0 * std::log10 (buf[(std::size_t) b] + 1.0e-9f);
            sumDb[(std::size_t) b]  += db;
            sumDb2[(std::size_t) b] += db * db;
        }
        ++frames;
    }
    if (frames < 6) return out;

    std::vector<double> meanDb ((std::size_t) nbins), stdDb ((std::size_t) nbins);
    for (int b = 0; b < nbins; ++b)
    {
        const double m = sumDb[(std::size_t) b] / frames;
        meanDb[(std::size_t) b] = m;
        stdDb[(std::size_t) b]  = std::sqrt (std::max (0.0, sumDb2[(std::size_t) b] / frames - m * m));
    }

    // --- resample to a log-frequency grid, subtract the broad tilt, smooth ---
    std::vector<double> lf ((std::size_t) kGrid), lp ((std::size_t) kGrid), lstd ((std::size_t) kGrid);
    for (int g = 0; g < kGrid; ++g)
    {
        const double f = kFLo * std::pow (fHi / kFLo, (double) g / (kGrid - 1));
        lf[(std::size_t) g] = f;
        const double bf = f / binHz;
        const int b0 = std::clamp ((int) bf, 0, nbins - 1);
        const int b1 = std::min (nbins - 1, b0 + 1);
        const double fr = bf - b0;
        lp[(std::size_t) g]   = meanDb[(std::size_t) b0] * (1.0 - fr) + meanDb[(std::size_t) b1] * fr;
        lstd[(std::size_t) g] = stdDb[(std::size_t) b0]  * (1.0 - fr) + stdDb[(std::size_t) b1]  * fr;
    }
    // Smooth (moving average, ~5 points) so bin noise doesn't spawn spurious peaks.
    auto smooth = [] (std::vector<double>& v)
    {
        std::vector<double> s (v.size());
        const int w = 2;
        for (int i = 0; i < (int) v.size(); ++i)
        { double a = 0.0; int c = 0; for (int k = -w; k <= w; ++k) { const int j = i + k; if (j >= 0 && j < (int) v.size()) { a += v[(std::size_t) j]; ++c; } } s[(std::size_t) i] = a / c; }
        v = s;
    };
    smooth (lp); smooth (lstd);
    // Remove the broad spectral tilt (fit a coarse baseline = heavily-smoothed lp) so the RELATIVE
    // concentrations stand out rather than the overall slope.
    std::vector<double> base = lp;
    for (int pass = 0; pass < 6; ++pass) smooth (base);
    std::vector<double> rel ((std::size_t) kGrid);
    for (int g = 0; g < kGrid; ++g) rel[(std::size_t) g] = lp[(std::size_t) g] - base[(std::size_t) g];

    // --- find prominent, steady peaks ---
    const double medStd = [&] { auto t = lstd; std::sort (t.begin(), t.end()); return t[t.size() / 2]; }();
    struct Peak { int g; double prom; };
    std::vector<Peak> peaks;
    for (int g = 1; g < kGrid - 1; ++g)
    {
        if (rel[(std::size_t) g] >= rel[(std::size_t) g - 1] && rel[(std::size_t) g] > rel[(std::size_t) g + 1])
        {
            const double prom = prominence (rel, g);
            // require a real bump AND reasonably steady material (low temporal variance) at the peak.
            if (prom >= 2.5 && lstd[(std::size_t) g] <= medStd * 1.6)
                peaks.push_back ({ g, prom });
        }
    }

    // Too many -> keep the most prominent; then re-sort by frequency.
    if ((int) peaks.size() > maxBands)
    {
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.prom > b.prom; });
        peaks.resize ((std::size_t) maxBands);
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.g < b.g; });

    // --- edges = valleys between consecutive peaks ---
    std::vector<double> edges;
    if (peaks.size() >= 3)
    {
        for (std::size_t i = 0; i + 1 < peaks.size(); ++i)
        {
            int lo = peaks[i].g, hi = peaks[i + 1].g, vg = lo;
            for (int g = lo; g <= hi; ++g) if (rel[(std::size_t) g] < rel[(std::size_t) vg]) vg = g;
            edges.push_back (lf[(std::size_t) vg]);
        }
    }
    else
    {
        // No clear structure -> a sensible geometric default (5 bands across the room-tone range).
        const int def = 5;
        for (int e = 0; e < def - 1; ++e)
        { const double t = (double) (e + 1) / def; edges.push_back (150.0 * std::pow (8000.0 / 150.0, t)); }
    }

    // Enforce spacing + clamp; de-dup edges that are too close.
    std::sort (edges.begin(), edges.end());
    for (double e : edges)
    {
        const double c = std::clamp (e, kFLo * 1.5, fHi * 0.98);
        if (out.empty() || c > out.back() * kMinEdgeRatio) out.push_back ((float) c);
        if ((int) out.size() >= maxBands - 1) break; // maxBands bands = maxBands-1 edges
    }
    return out;
}
} // namespace tonefill::dsp
