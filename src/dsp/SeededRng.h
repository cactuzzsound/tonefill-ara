#pragma once

#include <cstdint>

namespace tonefill::dsp
{
// Deterministic RNG (splitmix64). The render contract requires that identical
// (seed, model, settings) produce bit-identical output, so all stochastic synthesis
// MUST draw only from a SeededRng. Never use time/thread-dependent randomness.
//
// Use sub-streams (deriveSubStream) to give each layer/channel an independent yet
// reproducible sequence from a single master seed.
class SeededRng
{
public:
    explicit SeededRng (std::uint64_t seed = 0) : state_ (seed) {}

    std::uint64_t nextUInt64() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform float in [0, 1).
    float nextFloat() noexcept
    {
        return static_cast<float> (nextUInt64() >> 40) * (1.0f / 16777216.0f); // 24-bit mantissa
    }

    // Uniform float in [lo, hi).
    float nextFloat (float lo, float hi) noexcept { return lo + (hi - lo) * nextFloat(); }

    // Derive an independent sub-stream for a given layer/channel index.
    SeededRng deriveSubStream (std::uint64_t streamIndex) const noexcept
    {
        return SeededRng (state_ ^ (0xD1B54A32D192ED03ULL * (streamIndex + 1)));
    }

private:
    std::uint64_t state_;
};
} // namespace tonefill::dsp
