#pragma once

#include <vector>

namespace tonefill::dsp
{
struct DetectedPartial
{
    float freqHz    = 0.0f;
    float amplitude = 0.0f; // linear peak amplitude of the sinusoid
    float phase     = 0.0f; // radians, at sample 0 of `src`
};

// Detect steady tonal components (hum / HVAC / whine) in a quasi-stationary signal.
// Strategy: average magnitude spectrum at a FINE resolution (default 16384 -> ~2.9 Hz/bin so
// 50/100/150 Hz harmonics resolve), pick peaks above a smoothed floor, then refine each
// peak's amplitude + phase by direct DFT correlation (exact for integer-cycle content).
//
// Returns partials sorted by amplitude (descending), de-duplicated within ~5 Hz.
std::vector<DetectedPartial> detectPartials (const float* src, int n, double sr,
                                             int fftSize = 16384,
                                             float peakRatio = 4.0f,
                                             int maxPartials = 16);
} // namespace tonefill::dsp
