#pragma once

namespace tonefill::core
{
// Non-owning, read-only view over deinterleaved float channels.
// Ownership: NEVER owns memory; the caller guarantees lifetime.
// Threading: safe to read from any thread as long as the backing buffer is immutable.
struct AudioBufferView
{
    const float* const* channels   = nullptr;
    int                 numChannels = 0;
    long long           numSamples  = 0;
    double              sampleRate  = 0.0;

    bool isValid() const noexcept
    {
        return channels != nullptr && numChannels > 0 && numSamples > 0 && sampleRate > 0.0;
    }
};
} // namespace tonefill::core
