#pragma once

#include "dsp/FFTWrapper.h"
#include "dsp/Stft.h"      // StftConfig
#include "dsp/SeededRng.h"

#include <vector>

namespace tonefill::dsp
{
// Generates noise whose long-term magnitude spectrum follows a target shape (LTAS), via
// analysis-modify-synthesis STFT overlap-add with window^2 normalization.
//
// Deterministic from the supplied SeededRng. Output is roughly unit-level (shape is mean-
// normalized internally); the caller rescales to the desired RMS.
class NoiseShaper
{
public:
    explicit NoiseShaper (StftConfig cfg);

    // Fill out[0..n) with shaped noise. `shape` has numBins() (= fftSize/2+1) entries.
    void generate (float* out, int n, const std::vector<float>& shape, SeededRng& rng);

    int numBins() const noexcept { return fft_.numBins(); }

private:
    StftConfig         cfg_;
    FFTWrapper         fft_;
    std::vector<float> window_;
    std::vector<float> src_, frame_, re_, im_, tmp_, winSum_;
};
} // namespace tonefill::dsp
