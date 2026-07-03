#include "dsp/GranularSynth.h"
#include "dsp/Windows.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace tonefill::dsp
{
void granularResynthesize (float* out, int n,
                           const float* src, int srcLen,
                           int grainLen, SeededRng& rng, int antiRepeat)
{
    if (n <= 0 || srcLen <= 0 || grainLen <= 0) return;
    std::fill (out, out + n, 0.0f);

    // Too short to scramble: tile the source (degenerate fallback).
    // Too short to scramble: tile the source with a short equal-power crossfade at each wrap
    // instead of a hard `i % srcLen` loop point (the worst case for seamlessness).
    if (srcLen < 2 * grainLen)
    {
        const int xf = std::max (1, std::min (srcLen / 8, grainLen / 2));
        const float halfPi = 1.5707963267948966f;
        int o = 0; bool firstTile = true;
        while (o < n)
        {
            const int copy = std::min (srcLen, n - o);
            for (int i = 0; i < copy; ++i)
            {
                const float s = src[i];
                if (! firstTile && i < xf)
                {
                    const float g = (float) i / (float) std::max (1, xf - 1) * halfPi;
                    out[o + i] = out[o + i] * std::cos (g) + s * std::sin (g);
                }
                else out[o + i] = s;
            }
            o += srcLen - xf;
            firstTile = false;
        }
        return;
    }

    const auto win = makeWindow (WindowType::Tukey, grainLen, 0.5);
    const int maxStart = srcLen - grainLen;
    const int synHop = grainLen / 2;

    // Zero-cross anchoring: snap a chosen start to the nearest rising zero crossing so each grain
    // begins near zero (less phase discontinuity under the Tukey OLA, esp. on tonal AC-hum beds).
    const int snapWin = std::max (8, grainLen / 32);
    auto snapToZeroCross = [&] (int s) -> int
    {
        int best = s, bestDist = snapWin + 1;
        const int lo = std::max (1, s - snapWin), hi = std::min (maxStart, s + snapWin);
        for (int k = lo; k <= hi; ++k)
            if (src[k - 1] <= 0.0f && src[k] > 0.0f)
            { const int d = std::abs (k - s); if (d < bestDist) { bestDist = d; best = k; } }
        return best;
    };

    std::vector<float> winSum ((std::size_t) n, 0.0f);
    std::deque<int> recent;

    for (int pos = 0; pos < n; pos += synHop)
    {
        // Pick a grain start far from recent ones (anti-repeat) — a few tries, then accept.
        int start = 0;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            start = (int) (rng.nextFloat() * (float) maxStart);
            bool tooClose = false;
            for (int r : recent)
                if (std::abs (start - r) < grainLen / 2) { tooClose = true; break; }
            if (! tooClose) break;
        }
        start = snapToZeroCross (start);
        recent.push_back (start);
        if ((int) recent.size() > antiRepeat) recent.pop_front();

        for (int k = 0; k < grainLen; ++k)
        {
            const int o = pos + k;
            if (o < n)
            {
                const float w = win[(std::size_t) k];
                out[o]                  += src[start + k] * w;
                winSum[(std::size_t) o] += w * w;
            }
        }
    }

    for (int i = 0; i < n; ++i)
        if (winSum[(std::size_t) i] > 1e-8f)
            out[i] /= winSum[(std::size_t) i];
}
} // namespace tonefill::dsp
