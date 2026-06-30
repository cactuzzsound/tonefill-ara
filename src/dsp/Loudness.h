#pragma once

#include <vector>

namespace tonefill::dsp
{
// Loudness measurement for the "Normalize to dBFS / LUFS" feature.
//
// channels: [ch][sample], float PCM. Multi-channel weighted per ITU-R BS.1770 (1.0 for L/R).

// Sample peak across all channels, in dBFS (<= 0). Returns -120 for silence.
float peakDbfs (const std::vector<std::vector<float>>& channels);

// ITU-R BS.1770-4 / EBU R128 integrated (gated) loudness, in LUFS. Returns -120 for silence.
float integratedLufs (const std::vector<std::vector<float>>& channels, double sampleRate);
} // namespace tonefill::dsp
