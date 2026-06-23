#pragma once

#include <memory>
#include <vector>

namespace juce { namespace dsp { class FFT; } }

namespace tonefill::dsp
{
// Thin real-signal FFT over juce::dsp::FFT. fftSize must be a power of two.
//
// Forward: `fftSize` real samples in -> (fftSize/2 + 1) complex bins out (separate re/im).
// Inverse: bins in -> `fftSize` real samples out, normalized (1/N applied).
//
// NOTE(blind): assumes juce::dsp::FFT::performRealOnlyInverseTransform applies 1/N scaling
// (it does in current JUCE). The FFT round-trip unit test pins this — if a JUCE version omits
// the scaling, that test fails by a factor of fftSize and you flip kInverseNormalized here.
class FFTWrapper
{
public:
    explicit FFTWrapper (int fftSize);
    ~FFTWrapper();

    int size()    const noexcept { return fftSize_; }
    int numBins() const noexcept { return fftSize_ / 2 + 1; }

    // reOut/imOut must have numBins() elements each.
    void forward (const float* timeIn, float* reOut, float* imOut);

    // reIn/imIn have numBins() elements; timeOut has size() elements.
    void inverse (const float* reIn, const float* imIn, float* timeOut);

private:
    static constexpr bool kInverseNormalized = true;

    int fftSize_;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float> work_; // 2*fftSize interleaved scratch
};
} // namespace tonefill::dsp
