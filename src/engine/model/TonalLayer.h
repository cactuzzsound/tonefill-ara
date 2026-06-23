#pragma once

#include <vector>

namespace tonefill::engine::model
{
// One tracked stable partial (hum harmonic, HVAC tone, electrical whine).
struct Partial
{
    double frequencyHz = 0.0;   // mean tracked frequency
    float  amplitude   = 0.0f;  // mean linear amplitude
    float  refPhase    = 0.0f;  // phase reference for boundary phase-docking (radians)
    float  stability   = 0.0f;  // 0..1, fraction of frames present / freq steadiness

    // Optional slow amplitude envelope (Complex mode). Empty => constant amplitude.
    std::vector<float> slowAmpEnvelope;
};

// Tonal skeleton for a single channel. Immutable once built.
struct TonalLayer
{
    std::vector<Partial> partials;
    float                totalEnergy = 0.0f; // for energy-budget balancing across layers
};
} // namespace tonefill::engine::model
