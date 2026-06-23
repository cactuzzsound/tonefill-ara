#pragma once

#include <vector>

namespace tonefill::engine::model
{
// Captured conditions at one seam (pre-gap end OR post-gap start), one channel.
// Drives inaudible joins: phase-docking of tonal partials + level/tilt matched crossfades.
struct BoundaryConditionProfile
{
    enum class Side { Pre, Post };

    Side  side = Side::Pre;
    int   channel = 0;

    std::vector<float> seamSnippet;            // short window of real neighbour audio at the seam
    float              seamRms = 0.0f;
    std::vector<float> seamSpectralTilt;       // coarse tilt for spectral edge matching

    // Per-partial instantaneous phase at the exact seam sample (index parallels TonalLayer).
    std::vector<float> partialPhaseAtSeam;

    float recommendedCrossfadeMs = 40.0f;
};
} // namespace tonefill::engine::model
