#pragma once

#include <cmath>

namespace tonefill::dsp
{
// Equal-power crossfade over n samples: transitions from a -> b with constant power
// (cos^2 + sin^2 == 1), so a junction between two uncorrelated signals (e.g. real neighbour
// audio and synthesized fill) has no level dip and no click.
//
// out[0]   == a[0]      (start fully on a)
// out[n-1] == b[n-1]    (end fully on b)
// out and a/b may overlap only if out == a or out == b (written left-to-right).
inline void equalPowerCrossfade (const float* a, const float* b, float* out, int n)
{
    if (n <= 0) return;
    if (n == 1) { out[0] = b[0]; return; }

    const float halfPi = 1.5707963267948966f;
    const float invDen = 1.0f / (float) (n - 1);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i * invDen;       // 0..1
        const float g = t * halfPi;
        out[i] = a[i] * std::cos (g) + b[i] * std::sin (g);
    }
}
} // namespace tonefill::dsp
