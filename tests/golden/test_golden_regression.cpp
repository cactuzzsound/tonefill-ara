// Golden regression for the end-to-end engine (analysis -> render) on synthetic fixtures.
//
// Golden = a compact signature per (fixture, mode): {channels, samples, checksum of samples
// quantized to 24-bit}. Stored as text in golden/goldens.txt. 24-bit quantization makes the
// gate robust to sub-LSB floating-point differences across compilers/platforms while still
// catching any real DSP change. (Same-binary bit-exact determinism is covered separately by
// test_render_determinism.cpp.)
//
// Workflow:
//   * First run (or a missing entry): the signature is RECORDED and the case passes with a
//     warning. Inspect goldens.txt, then commit it — from then on it gates.
//   * Set env TONEFILL_UPDATE_GOLDENS=1 to force-rewrite all entries (after an intended DSP
//     change). Review the diff and commit.

#include <catch2/catch_test_macros.hpp>

#include "core/DiagnosticsLogger.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/render/RenderManager.h"
#include "engine/model/RenderSettings.h"
#include "golden/fixtures.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef TONEFILL_GOLDEN_DIR
 #define TONEFILL_GOLDEN_DIR "."
#endif

namespace
{
namespace model    = tonefill::engine::model;
namespace analysis = tonefill::engine::analysis;
namespace render   = tonefill::engine::render;

struct Golden
{
    int           numCh      = 0;
    long long     numSamples = 0;
    std::uint64_t checksum   = 0;
    bool operator== (const Golden& o) const
    {
        return numCh == o.numCh && numSamples == o.numSamples && checksum == o.checksum;
    }
};

// FNV-1a over shape + 24-bit-quantized samples. Stable across platforms for identical audio.
Golden signatureOf (const render::RenderResult& out)
{
    Golden g;
    g.numCh      = (int) out.channels.size();
    g.numSamples = out.channels.empty() ? 0 : (long long) out.channels[0].size();

    std::uint64_t h = 0xCBF29CE484222325ULL;
    auto mix = [&h] (std::uint64_t v) { h ^= v; h *= 0x100000001B3ULL; };
    mix ((std::uint64_t) g.numCh);
    mix ((std::uint64_t) g.numSamples);

    for (const auto& ch : out.channels)
        for (float s : ch)
        {
            const float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
            const std::int32_t q = (std::int32_t) std::lround (c * 8388607.0f); // 24-bit
            mix ((std::uint64_t) (std::uint32_t) q);
        }

    g.checksum = h;
    return g;
}

std::string goldenPath() { return std::string (TONEFILL_GOLDEN_DIR) + "/goldens.txt"; }

std::map<std::string, Golden> loadGoldens()
{
    std::map<std::string, Golden> m;
    std::ifstream in (goldenPath());
    std::string line;
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss (line);
        std::string key, hex;
        Golden g;
        if (ss >> key >> g.numCh >> g.numSamples >> hex)
        {
            g.checksum = std::stoull (hex, nullptr, 16); // accepts 0x-prefixed
            m[key] = g;
        }
    }
    return m;
}

void saveGoldens (const std::map<std::string, Golden>& m)
{
    std::ofstream out (goldenPath(), std::ios::trunc);
    out << "# ToneFill golden signatures: <key> <numCh> <numSamples> <checksum>\n"
        << "# Auto-managed by test_golden_regression. Review diffs before committing.\n";
    for (const auto& [key, g] : m)
    {
        std::ostringstream hex;
        hex << "0x" << std::hex << g.checksum;
        out << key << ' ' << g.numCh << ' ' << g.numSamples << ' ' << hex.str() << '\n';
    }
}

const char* modeName (model::Mode m)
{
    switch (m)
    {
        case model::Mode::Static:  return "static";
        case model::Mode::Hybrid:  return "hybrid";
        case model::Mode::Complex: return "complex";
    }
    return "?";
}

// Run analysis + render for one fixture/mode; returns the rendered output.
render::RenderResultPtr runPipeline (const juce::AudioBuffer<float>& fixture,
                                     double sr, model::Mode mode)
{
    analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (fixture);
    ctx.rightContext.makeCopyOf (fixture);
    ctx.analysisSampleRate = sr;
    ctx.numChannels = fixture.getNumChannels();
    ctx.sourceContentHash = (std::uint64_t) fixture.getNumSamples();

    tonefill::core::DiagnosticsLogger diag;
    analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    auto modelRes = session.run (ctx, cancel);
    if (! modelRes.ok())
        return nullptr;

    render::RenderManager rm;
    rm.setModel (modelRes.value());

    model::RenderSettings s;
    s.mode                  = mode;
    s.seed                  = 1234;
    s.targetSampleRate      = sr;
    s.targetChannels        = ctx.numChannels;
    s.targetDurationSamples = (long long) (sr * 0.5); // 0.5 s

    render::RenderResultPtr out;
    rm.requestRender (s, [&out] (tonefill::core::Result<render::RenderResultPtr> r)
                         { if (r.ok()) out = r.value(); });
    return out;
}
} // namespace

TEST_CASE ("Golden regression: engine output is stable across commits")
{
    constexpr double sr  = 48000.0;
    constexpr int    ch  = 2;
    constexpr int    len = 48000; // 1 s of context material

    struct Fixture { std::string name; juce::AudioBuffer<float> buf; };
    std::vector<Fixture> fixtures;
    fixtures.push_back ({ "white_noise",   tonefill::test::whiteNoise   (ch, len, 42) });
    fixtures.push_back ({ "sine_1k",       tonefill::test::sine         (ch, len, sr, 1000.0) });
    fixtures.push_back ({ "hum_50",        tonefill::test::hum          (ch, len, sr, 50.0) });
    fixtures.push_back ({ "impulse_train", tonefill::test::impulseTrain (ch, len, 4800) });

    const bool update = std::getenv ("TONEFILL_UPDATE_GOLDENS") != nullptr;
    auto goldens = loadGoldens();
    bool changed = false;

    for (const auto& fx : fixtures)
        for (model::Mode mode : { model::Mode::Static, model::Mode::Hybrid, model::Mode::Complex })
        {
            const std::string key = fx.name + "/" + modeName (mode);

            auto out = runPipeline (fx.buf, sr, mode);
            REQUIRE (out != nullptr);
            const Golden sig = signatureOf (*out);

            // Output must always be the exact requested length.
            REQUIRE (sig.numSamples == (long long) (sr * 0.5));

            const auto it = goldens.find (key);
            if (update || it == goldens.end())
            {
                goldens[key] = sig;
                changed = true;
                WARN ("Recorded golden for " << key
                      << " (numCh=" << sig.numCh << ", numSamples=" << sig.numSamples << ")");
            }
            else
            {
                CHECK (sig == it->second); // shape + 24-bit checksum
            }
        }

    if (changed)
    {
        saveGoldens (goldens);
        WARN ("goldens.txt was written/updated at " << goldenPath()
              << " - review the diff and commit it.");
    }
}
