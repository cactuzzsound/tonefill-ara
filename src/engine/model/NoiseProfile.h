#pragma once

#include <vector>

namespace tonefill::engine::model
{
// Diffuse stationary bed description for a single channel. Immutable once built.
struct NoiseProfile
{
    // Smoothed long-term average magnitude spectra (fractional-octave smoothed).
    std::vector<float> residualLtas; // after tonal removal -> shapes the synth noise bed
    std::vector<float> fullLtas;     // including tonal -> used for energy/output matching

    int   fftSize     = 0;           // bin count basis for the LTAS curves
    float targetRms   = 0.0f;        // linear RMS of the clean ambience bed

    // Coarse per-band energies (e.g. low/mid/high) for fast checks & Complex per-band mod.
    std::vector<float> bandEnergies;
};
} // namespace tonefill::engine::model
