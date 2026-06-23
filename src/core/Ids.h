#pragma once

#include <cstdint>

namespace tonefill::core
{
// Content-hash identifiers used as cache keys. 0 means "unset / not yet computed".
using ModelId  = std::uint64_t;   // hash of analysis inputs (source range + learn + params)
using RenderId = std::uint64_t;   // hash of (ModelId + RenderSettings)
using Seed     = std::uint64_t;

inline constexpr ModelId kInvalidModelId = 0;
} // namespace tonefill::core
