#pragma once

#include <cmath>
#include <vector>

namespace tonefill::dsp
{
enum class WindowType { Hann, Tukey };

// Periodic (DFT-even) Hann: denominator N (NOT N-1). This is the form that satisfies the
// constant-overlap-add (COLA) condition at hops N/2 and N/4 — required for STFT resynthesis.
inline void fillHann (float* w, int N)
{
    const double twoPi = 6.283185307179586;
    for (int n = 0; n < N; ++n)
        w[n] = (float) (0.5 * (1.0 - std::cos (twoPi * (double) n / (double) N)));
}

// Tukey (tapered cosine), flat middle with cosine tapers of total fraction `alpha`.
// alpha=0 -> rectangular, alpha=1 -> Hann.
inline void fillTukey (float* w, int N, double alpha = 0.5)
{
    if (alpha <= 0.0) { for (int n = 0; n < N; ++n) w[n] = 1.0f; return; }
    if (alpha >= 1.0) { fillHann (w, N); return; }

    const double pi = 3.141592653589793;
    const double a  = alpha * (double) (N - 1) / 2.0;
    for (int n = 0; n < N; ++n)
    {
        const double x = (double) n;
        if (x < a)
            w[n] = (float) (0.5 * (1.0 + std::cos (pi * (x / a - 1.0))));
        else if (x > (double) (N - 1) - a)
            w[n] = (float) (0.5 * (1.0 + std::cos (pi * ((x - (double) (N - 1) + a) / a))));
        else
            w[n] = 1.0f;
    }
}

inline std::vector<float> makeWindow (WindowType type, int N, double alpha = 0.5)
{
    std::vector<float> w ((std::size_t) N);
    if (type == WindowType::Hann) fillHann (w.data(), N);
    else                          fillTukey (w.data(), N, alpha);
    return w;
}
} // namespace tonefill::dsp
