#include "dsp/AmbienceConcat.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace tonefill::dsp
{
void concatenateAmbience (float* out, int n,
                          const float* src, int srcLen,
                          int fragLen, int xfadeLen,
                          SeededRng& rng, int antiRepeat, float variation)
{
    if (n <= 0 || srcLen <= 0) return;
    std::fill (out, out + n, 0.0f);

    xfadeLen = std::max (1, std::min (xfadeLen, fragLen / 2));

    // Source too short to draw distinct fragments: loop it with a short crossfade at the seam.
    if (srcLen < fragLen + xfadeLen)
    {
        for (int i = 0; i < n; ++i) out[i] = src[i % srcLen];
        return;
    }

    const int maxStart = srcLen - fragLen;
    const int advance  = fragLen - xfadeLen;
    const float halfPi = 1.5707963267948966f;

    std::deque<int> recent;
    int pos = 0;
    bool first = true;

    while (pos < n)
    {
        int start = 0;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            start = (int) (rng.nextFloat() * (float) maxStart);
            bool tooClose = false;
            for (int r : recent)
                if (std::abs (start - r) < fragLen / 2) { tooClose = true; break; }
            if (! tooClose) break;
        }
        recent.push_back (start);
        if ((int) recent.size() > antiRepeat) recent.pop_front();

        // Occasionally play the fragment reversed: room tone is ~time-symmetric, so this adds
        // variety (breaks audible repetition) without needing more source material.
        const bool reverse = rng.nextFloat() < 0.5f;

        // Per-fragment level jitter (±~2.5 dB at variation=1): even reused material reads as
        // "different" each time, which dissolves the looped feel on short source samples.
        const float jitterDb = (rng.nextFloat() * 2.0f - 1.0f) * variation * 2.5f;
        const float fragGain = std::pow (10.0f, jitterDb / 20.0f);

        for (int i = 0; i < fragLen; ++i)
        {
            const int o = pos + i;
            if (o >= n) break;
            const float s = src[start + (reverse ? (fragLen - 1 - i) : i)] * fragGain;
            if (i < xfadeLen && ! first)
            {
                // Equal-power blend with the previous fragment's tail already in `out`.
                const float g = (float) i / (float) (xfadeLen - 1) * halfPi;
                out[o] = out[o] * std::cos (g) + s * std::sin (g);
            }
            else
            {
                out[o] = s;
            }
        }

        pos += advance;
        first = false;
    }
}

void grainCloud (float* out, int n,
                 const float* src, int srcLen,
                 int grainLen, int density,
                 SeededRng& rng, int antiRepeat, float variation)
{
    if (n <= 0 || srcLen <= 0) return;
    std::fill (out, out + n, 0.0f);

    grainLen = std::max (64, std::min (grainLen, srcLen));
    density  = std::max (1, density);
    const int hop = std::max (1, grainLen / density);
    const int maxStart = std::max (1, srcLen - grainLen);
    const float twoPi = 6.2831853071795864f;

    // Hann window (precomputed).
    std::vector<float> win ((std::size_t) grainLen);
    for (int i = 0; i < grainLen; ++i)
        win[(std::size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) (grainLen - 1));

    std::deque<int> recent;
    const int jitter = (int) (variation * (float) hop); // small grid jitter for less regularity

    for (int pos = -grainLen + hop; pos < n; pos += hop)
    {
        // Pick a random, non-recently-used source start.
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

        const bool reverse = rng.nextFloat() < 0.5f * variation;
        const int  pj = jitter > 0 ? (int) (rng.nextFloat (-1.0f, 1.0f) * (float) jitter) : 0;

        for (int i = 0; i < grainLen; ++i)
        {
            const int o = pos + pj + i;
            if (o < 0) continue;
            if (o >= n) break;
            const float s = src[start + (reverse ? (grainLen - 1 - i) : i)];
            out[o] += s * win[(std::size_t) i];
        }
    }
}
} // namespace tonefill::dsp
