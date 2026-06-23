#pragma once

#include "dsp/SeededRng.h"

namespace tonefill::dsp
{
// Slow "breathing" level modulation (design §F Complex). Multiplies buf in place by an
// envelope e(t) = 1 + depth * d'(t), where d'(t) is a smooth seeded random-walk anchored so
// e(0) == e(n-1) == 1 — the endpoints keep the boundary level so edge joins stay intact.
//
// `depth` is the peak deviation (e.g. 0.15 == +/-15% level). Rate is internal (~0.4 s control
// points -> roughly 0.1-0.3 Hz). Deterministic from rng.
void applyMacroEnvelope (float* buf, int n, float depth, double sampleRate, SeededRng& rng);
} // namespace tonefill::dsp
