#include "dsp/Features.h"

#include <algorithm>
#include <cmath>

namespace tonefill::dsp
{
float spectralCentroid (const float* mag, int nBins, double binHz)
{
    double num = 0.0, den = 0.0;
    for (int k = 0; k < nBins; ++k)
    {
        const double m = mag[k];
        num += (double) k * binHz * m;
        den += m;
    }
    return den > 0.0 ? (float) (num / den) : 0.0f;
}

float spectralFlatness (const float* mag, int nBins)
{
    // Power spectrum, skip DC (k=0). Guard against log(0).
    double logSum = 0.0, arithSum = 0.0;
    int count = 0;
    for (int k = 1; k < nBins; ++k)
    {
        const double p = (double) mag[k] * (double) mag[k] + 1e-20;
        logSum += std::log (p);
        arithSum += p;
        ++count;
    }
    if (count == 0 || arithSum <= 0.0) return 0.0f;
    const double geo = std::exp (logSum / (double) count);
    const double ari = arithSum / (double) count;
    const double flat = geo / ari;
    return (float) (flat < 0.0 ? 0.0 : (flat > 1.0 ? 1.0 : flat));
}

float spectralFlux (const float* mag, const float* prevMag, int nBins)
{
    if (prevMag == nullptr) return 0.0f;
    double sum = 0.0;
    for (int k = 0; k < nBins; ++k)
    {
        const double d = (double) mag[k] - (double) prevMag[k];
        if (d > 0.0) sum += d;
    }
    return (float) (sum / (double) nBins);
}

float lowBandRatio (const float* mag, int nBins, double binHz, double cutoffHz)
{
    double low = 0.0, total = 0.0;
    for (int k = 0; k < nBins; ++k)
    {
        const double m = mag[k];
        total += m;
        if ((double) k * binHz < cutoffHz) low += m;
    }
    return total > 0.0 ? (float) (low / total) : 0.0f;
}

float frameRms (const float* frame, int n)
{
    if (n <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double) frame[i] * (double) frame[i];
    return (float) std::sqrt (sum / (double) n);
}

float frameCrestDb (const float* frame, int n)
{
    const float rms = frameRms (frame, n);
    if (rms <= 0.0f) return 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) peak = std::max (peak, std::fabs (frame[i]));
    return 20.0f * std::log10 (std::max (peak, 1e-12f) / rms);
}

SpectralFrameFeatures computeFrameFeatures (const float* frame, int n,
                                            const float* mag, const float* prevMag,
                                            int nBins, double binHz)
{
    SpectralFrameFeatures f;
    f.rms          = frameRms (frame, n);
    f.crestDb      = frameCrestDb (frame, n);
    f.centroidHz   = spectralCentroid (mag, nBins, binHz);
    f.flatness     = spectralFlatness (mag, nBins);
    f.flux         = spectralFlux (mag, prevMag, nBins);
    f.lowBandRatio = lowBandRatio (mag, nBins, binHz);
    return f;
}
} // namespace tonefill::dsp
