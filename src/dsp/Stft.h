#pragma once

#include "dsp/FFTWrapper.h"
#include "dsp/Windows.h"

#include <vector>

namespace tonefill::dsp
{
struct StftConfig
{
    int        fftSize = 2048;
    int        hop     = 512;          // 75% overlap
    WindowType window  = WindowType::Hann;
    double     tukeyAlpha = 0.5;       // used only when window == Tukey
};

// Forward STFT helper. Holds reusable scratch; ANALYSIS-WORKER use only (not thread-safe).
// Inverse / overlap-add (for residual + synthesis) is implemented in the compile loop (TF-202b)
// where it can be COLA-tested; kept out of the blind foundation deliberately.
class Stft
{
public:
    explicit Stft (StftConfig cfg);

    const StftConfig& config() const noexcept { return cfg_; }
    int numBins() const noexcept { return fft_.numBins(); }

    // Number of left-aligned, zero-padded frames over `numSamples`.
    static int numFrames (int numSamples, int hop) noexcept
    {
        return numSamples <= 0 ? 0 : (numSamples - 1) / hop + 1;
    }

    // Window the frame starting at `frameStart` (zero-padded at the buffer edges), FFT it,
    // and write magnitude + phase (each numBins() long). Also exposes the windowed time frame.
    void analyze (const float* src, int numSamples, int frameStart,
                  std::vector<float>& magOut, std::vector<float>& phaseOut);

    // Raw (un-windowed) frame samples for the most recent analyze() call (for RMS/crest).
    const std::vector<float>& lastRawFrame() const noexcept { return rawFrame_; }

private:
    StftConfig         cfg_;
    FFTWrapper         fft_;
    std::vector<float> window_;
    std::vector<float> rawFrame_;   // unwindowed copy (zero-padded)
    std::vector<float> winFrame_;   // windowed input to the FFT
    std::vector<float> re_, im_;
};
} // namespace tonefill::dsp
