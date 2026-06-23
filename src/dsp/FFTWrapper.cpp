#include "dsp/FFTWrapper.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstring>

namespace tonefill::dsp
{
namespace
{
int log2OfPowerOfTwo (int n)
{
    int order = 0;
    while ((1 << order) < n) ++order;
    return order; // assumes n is a power of two
}
} // namespace

FFTWrapper::FFTWrapper (int fftSize)
    : fftSize_ (fftSize),
      fft_ (std::make_unique<juce::dsp::FFT> (log2OfPowerOfTwo (fftSize))),
      work_ ((std::size_t) (2 * fftSize), 0.0f)
{
}

FFTWrapper::~FFTWrapper() = default;

void FFTWrapper::forward (const float* timeIn, float* reOut, float* imOut)
{
    // JUCE expects a 2*size buffer with the real input in the first `size` floats.
    std::memcpy (work_.data(), timeIn, sizeof (float) * (std::size_t) fftSize_);
    std::memset (work_.data() + fftSize_, 0, sizeof (float) * (std::size_t) fftSize_);

    fft_->performRealOnlyForwardTransform (work_.data(), true);

    // Output is interleaved complex: bin k -> (work[2k], work[2k+1]), k = 0..size/2.
    const int bins = numBins();
    for (int k = 0; k < bins; ++k)
    {
        reOut[k] = work_[(std::size_t) (2 * k)];
        imOut[k] = work_[(std::size_t) (2 * k + 1)];
    }
}

void FFTWrapper::inverse (const float* reIn, const float* imIn, float* timeOut)
{
    const int bins = numBins();
    std::memset (work_.data(), 0, sizeof (float) * work_.size());
    for (int k = 0; k < bins; ++k)
    {
        work_[(std::size_t) (2 * k)]     = reIn[k];
        work_[(std::size_t) (2 * k + 1)] = imIn[k];
    }

    fft_->performRealOnlyInverseTransform (work_.data());

    std::memcpy (timeOut, work_.data(), sizeof (float) * (std::size_t) fftSize_);

    if (! kInverseNormalized)
    {
        const float scale = 1.0f / (float) fftSize_;
        for (int n = 0; n < fftSize_; ++n) timeOut[n] *= scale;
    }
}
} // namespace tonefill::dsp
