#include <catch2/catch_test_macros.hpp>
#include "engine/model/RenderSettings.h"

using tonefill::engine::model::RenderSettings;
using tonefill::engine::model::Mode;

// The RenderCache key depends on a stable, field-sensitive hash.
TEST_CASE ("RenderSettings::hash is stable and sensitive")
{
    RenderSettings a;
    RenderSettings b = a;
    REQUIRE (a.hash() == b.hash());

    b.seed += 1;
    REQUIRE (a.hash() != b.hash());

    RenderSettings c = a;
    c.mode = Mode::Complex;
    REQUIRE (a.hash() != c.hash());

    RenderSettings d = a;
    d.targetDurationSamples = a.targetDurationSamples + 1;
    REQUIRE (a.hash() != d.hash());
}
