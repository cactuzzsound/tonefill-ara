#include "dsp/AmbienceConcat.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace tonefill::dsp
{
void concatenateAmbience (float* out, int n,
                          const float* src, int srcLen,
                          int fragLen, int xfadeLen,
                          SeededRng& rng, int antiRepeat)
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

        for (int i = 0; i < fragLen; ++i)
        {
            const int o = pos + i;
            if (o >= n) break;
            const float s = src[start + (reverse ? (fragLen - 1 - i) : i)];
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
} // namespace tonefill::dsp
