#include "dsp/Loudness.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// One biquad section, coefficients already normalised by a0 (so a0 == 1).
struct Biquad { double b0, b1, b2, a1, a2; };

// Direct-Form-II-transposed, in place, double-precision state.
void filterInPlace (std::vector<float>& x, const Biquad& f)
{
    double z1 = 0.0, z2 = 0.0;
    for (auto& s : x)
    {
        const double in  = (double) s;
        const double out = f.b0 * in + z1;
        z1 = f.b1 * in - f.a1 * out + z2;
        z2 = f.b2 * in - f.a2 * out;
        s = (float) out;
    }
}

// BS.1770 K-weighting stage 1: high-shelf. Audio-EQ-cookbook coefficients from the spec's
// (f0, Q, gain) so it is correct at any sample rate (not just 48 kHz).
Biquad kHighShelf (double fs)
{
    const double f0 = 1681.974450955533, Q = 0.7071752369554196, G = 3.999843853973347;
    const double A = std::pow (10.0, G / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * Q), sqA = std::sqrt (A);

    const double b0 =  A * ((A + 1) + (A - 1) * cw + 2 * sqA * alpha);
    const double b1 = -2 * A * ((A - 1) + (A + 1) * cw);
    const double b2 =  A * ((A + 1) + (A - 1) * cw - 2 * sqA * alpha);
    const double a0 =       (A + 1) - (A - 1) * cw + 2 * sqA * alpha;
    const double a1 =  2 * ((A - 1) - (A + 1) * cw);
    const double a2 =       (A + 1) - (A - 1) * cw - 2 * sqA * alpha;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// BS.1770 K-weighting stage 2: RLB high-pass.
Biquad kHighPass (double fs)
{
    const double f0 = 38.13547087602444, Q = 0.5003270373238773;
    const double w0 = 2.0 * kPi * f0 / fs, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * Q);

    const double b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2;
    const double a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

double meanSquareLoudness (double z) { return -0.691 + 10.0 * std::log10 (z); }
} // namespace

float peakDbfs (const std::vector<std::vector<float>>& channels)
{
    float pk = 0.0f;
    for (const auto& c : channels)
        for (float s : c) pk = std::max (pk, std::fabs (s));
    return pk > 1.0e-9f ? 20.0f * std::log10 (pk) : -120.0f;
}

float integratedLufs (const std::vector<std::vector<float>>& channelsIn, double fs)
{
    if (channelsIn.empty() || channelsIn[0].empty() || fs <= 0.0) return -120.0f;

    const int nch = (int) channelsIn.size();
    const int N   = (int) channelsIn[0].size();

    // K-weight a working copy of each channel.
    std::vector<std::vector<float>> ch (channelsIn);
    const Biquad hs = kHighShelf (fs), hp = kHighPass (fs);
    for (auto& c : ch) { filterInPlace (c, hs); filterInPlace (c, hp); }

    const int block = (int) std::lround (0.4 * fs); // 400 ms
    const int hop   = (int) std::lround (0.1 * fs); // 75 % overlap

    // Too short for one gating block: fall back to an ungated mean square.
    if (block <= 0 || hop <= 0 || N < block)
    {
        double z = 0.0;
        for (const auto& c : ch) { double s = 0.0; for (float v : c) s += (double) v * v; z += s / std::max (1, N); }
        return z > 0.0 ? (float) meanSquareLoudness (z) : -120.0f;
    }

    // Per-block channel-summed mean square (channel weight 1.0 for L/R).
    std::vector<double> zBlocks;
    for (int start = 0; start + block <= N; start += hop)
    {
        double z = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            const auto& cc = ch[(std::size_t) c];
            double s = 0.0;
            for (int i = 0; i < block; ++i) { const double v = cc[(std::size_t) (start + i)]; s += v * v; }
            z += s / block;
        }
        zBlocks.push_back (z);
    }

    // Absolute gate at -70 LUFS.
    std::vector<double> kept;
    for (double z : zBlocks) if (z > 0.0 && meanSquareLoudness (z) >= -70.0) kept.push_back (z);
    if (kept.empty()) return -120.0f;

    // Relative gate at (mean loudness of kept blocks) - 10 LU.
    double mean = 0.0; for (double z : kept) mean += z; mean /= (double) kept.size();
    const double relThresh = meanSquareLoudness (mean) - 10.0;

    std::vector<double> kept2;
    for (double z : kept) if (meanSquareLoudness (z) >= relThresh) kept2.push_back (z);
    if (kept2.empty()) return -120.0f;

    double mean2 = 0.0; for (double z : kept2) mean2 += z; mean2 /= (double) kept2.size();
    return (float) meanSquareLoudness (mean2);
}
} // namespace tonefill::dsp
