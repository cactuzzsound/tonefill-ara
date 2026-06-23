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
    if (srcLen < 2 * grainLen)
    {
        for (int i = 0; i < n; ++i) out[i] = src[i % srcLen];
        return;
    }

    const auto win = makeWindow (WindowType::Tukey, grainLen, 0.5);
    const int maxStart = srcLen - grainLen;
    const int synHop = grainLen / 2;

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
