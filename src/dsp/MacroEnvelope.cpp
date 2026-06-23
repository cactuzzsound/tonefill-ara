#include "dsp/MacroEnvelope.h"

#include <algorithm>
#include <vector>

namespace tonefill::dsp
{
void applyMacroEnvelope (float* buf, int n, float depth, double sampleRate, SeededRng& rng)
{
    if (n <= 1 || depth <= 0.0f || sampleRate <= 0.0) return;

    // Control points ~every 0.4 s, linearly interpolated -> a slow contour.
    const double secs = (double) n / sampleRate;
    const int cp = (secs / 0.4) < 3.0 ? 3 : (int) (secs / 0.4) + 1;

    std::vector<float> ctrl ((std::size_t) cp);
    for (auto& c : ctrl) c = rng.nextFloat() * 2.0f - 1.0f; // [-1, 1]

    std::vector<float> d ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double pos = (double) i / (double) (n - 1) * (double) (cp - 1);
        const int i0 = (int) pos;
        const int i1 = i0 + 1 < cp ? i0 + 1 : i0;
        const float frac = (float) (pos - i0);
        d[(std::size_t) i] = ctrl[(std::size_t) i0] * (1.0f - frac) + ctrl[(std::size_t) i1] * frac;
    }

    // Anchor: subtract the straight line through the endpoints so d'(0) == d'(n-1) == 0.
    const float d0 = d[0], d1 = d[(std::size_t) (n - 1)];
    for (int i = 0; i < n; ++i)
    {
        const float line = d0 + (d1 - d0) * (float) i / (float) (n - 1);
        // Anchored deviation can exceed +/-1 after the line subtraction; clamp so `depth` is a
        // true level bound (no over-modulation / pumping).
        float e = 1.0f + depth * (d[(std::size_t) i] - line);
        e = std::max (1.0f - depth, std::min (1.0f + depth, e));
        buf[i] *= e;
    }
}
} // namespace tonefill::dsp
