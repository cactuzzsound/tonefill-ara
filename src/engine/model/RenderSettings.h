#pragma once

#include "core/Ids.h"
#include <cstdint>
#include <cstring>

namespace tonefill::engine::model
{
enum class Mode { Static, Hybrid, Complex, Ambience };

// Everything synthesis needs beyond the AmbienceModel.
// Mutability: value type, cheaply copied. Owned by ParameterState on the message thread;
// snapshotted into a RenderJob. Changes here drive RE-RENDER ONLY (never re-analysis).
struct RenderSettings
{
    Mode  mode           = Mode::Hybrid;
    float tonalRetention = 1.0f;  // 0..1
    float textureAmount  = 0.5f;  // 0..1, grain vs noise balance
    float movement       = 0.2f;  // 0..1, macro-envelope depth/rate
    float randomness     = 0.4f;  // 0..1, jitter + grain-path entropy
    float stereoWidth    = 0.5f;  // 0..1
    float crossfadeMs    = 40.0f;

    // Ambience (real-fragment) mode controls.
    float fragmentMs     = 800.0f; // length of each real fragment
    float blendFrac      = 0.3f;   // crossfade as a fraction of fragmentMs (0.05..0.5)

    core::Seed seed = 0;

    long long targetDurationSamples = 0;
    double    targetSampleRate      = 48000.0;
    int       targetChannels        = 2;

    // Stable hash for the RenderCache key (combined with AmbienceModel::modelId).
    // FNV-1a over the byte image of the fields; deterministic across runs.
    core::RenderId hash() const noexcept
    {
        auto mix = [] (std::uint64_t h, std::uint64_t v) noexcept
        {
            h ^= v;
            h *= 0x100000001B3ULL;
            return h;
        };

        std::uint64_t h = 0xCBF29CE484222325ULL;
        h = mix (h, static_cast<std::uint64_t> (mode));
        h = mix (h, bitsOf (tonalRetention));
        h = mix (h, bitsOf (textureAmount));
        h = mix (h, bitsOf (movement));
        h = mix (h, bitsOf (randomness));
        h = mix (h, bitsOf (stereoWidth));
        h = mix (h, bitsOf (crossfadeMs));
        h = mix (h, seed);
        h = mix (h, static_cast<std::uint64_t> (targetDurationSamples));
        h = mix (h, bitsOf (static_cast<float> (targetSampleRate)));
        h = mix (h, static_cast<std::uint64_t> (targetChannels));
        return h;
    }

private:
    static std::uint64_t bitsOf (float f) noexcept
    {
        std::uint32_t u = 0;
        static_assert (sizeof (u) == sizeof (f), "float must be 32-bit");
        std::memcpy (&u, &f, sizeof (u));
        return u;
    }
};
} // namespace tonefill::engine::model
