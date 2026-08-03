#pragma once

#include <vector>

namespace tonefill::dsp
{
// Auto band-layout for Spectral mode. Given the CLEAN room-tone audio (mono, the material the
// broadband analysis already selected), work out how many bands and at what frequencies to place
// their edges, so each band captures a coherent, stationary region of the room's spectrum with good
// material to resynthesize. Returns the internal band edges (Hz), ascending; band count = size()+1
// (clamped so bands are in [3, maxBands]). Empty if there isn't enough material.
//
// Method: a Welch LTAS of the clean audio on a log-frequency grid -> find the room tone's spectral
// concentrations (peaks: mains hum, HVAC, resonances, broadband floor) -> place edges in the valleys
// between them, weighted towards steady (low temporal variance) regions. Falls back to a geometric
// spread when the spectrum has no clear structure.
std::vector<float> autoBandEdges (const float* cleanMono, int n, double sampleRate, int maxBands = 12);
} // namespace tonefill::dsp
