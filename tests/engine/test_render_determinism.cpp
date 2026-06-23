#include <catch2/catch_test_macros.hpp>

#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/AmbienceModel.h"
#include "engine/model/RenderSettings.h"

#include <atomic>

using namespace tonefill::engine;

// Even at the silence-scaffold stage, the renderer must produce a buffer of exactly the
// requested length and shape. Once real DSP lands, this becomes a golden-file regression.
TEST_CASE ("AmbienceRenderer produces exact-length, deterministic output")
{
    model::AmbienceModel raw;
    raw.numChannels = 2;
    raw.analysisSampleRate = 48000.0;

    synthesis::AmbienceRenderer renderer;

    model::RenderSettings settings;
    settings.mode = model::Mode::Static;
    settings.targetChannels = 2;
    settings.targetSampleRate = 48000.0;
    settings.targetDurationSamples = 48000; // 1 second
    settings.seed = 42;

    std::atomic<bool> cancel { false };

    auto r1 = renderer.render (raw, settings, cancel);
    auto r2 = renderer.render (raw, settings, cancel);

    REQUIRE (r1.ok());
    REQUIRE (r2.ok());
    REQUIRE (r1.value().channels.size() == 2u);
    REQUIRE (r1.value().channels[0].size() == 48000u);

    // Same seed + inputs => identical samples (determinism contract).
    REQUIRE (r1.value().channels[0] == r2.value().channels[0]);
    REQUIRE (r1.value().channels[1] == r2.value().channels[1]);
}

TEST_CASE ("AmbienceRenderer rejects zero-length target")
{
    model::AmbienceModel raw; raw.numChannels = 1;
    synthesis::AmbienceRenderer renderer;
    model::RenderSettings settings; settings.targetDurationSamples = 0;
    std::atomic<bool> cancel { false };

    auto r = renderer.render (raw, settings, cancel);
    REQUIRE_FALSE (r.ok());
}
