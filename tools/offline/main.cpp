// ToneFill offline harness.
//
// Runs the host-agnostic engine on WAV files with NO plugin host and NO ARA. This is the
// development driver for the DSP (Sprints 2-7): every analysis/synthesis stage can be exercised
// and regression-tested on files long before ARA integration exists.
//
// Pipeline: load WAV -> AnalysisContext -> AnalysisSession -> RenderManager -> write WAV.
// With the DSP still stubbed it emits silence of the exact requested duration; that is expected
// and becomes real output as the engine is implemented.
//
// Usage:
//   tonefill_offline --in learn.wav --duration 2.0 --mode hybrid --seed 42 --out fill.wav
//   tonefill_offline --left pre.wav --right post.wav --duration 3 --mode static --out fill.wav
//   tonefill_offline --in learn.wav --analyze-only
//
// Flags:
//   --in <wav>         learn material; used as BOTH left and right context (common case)
//   --left <wav>       left (pre-gap) context        (overrides --in for the left side)
//   --right <wav>      right (post-gap) context       (overrides --in for the right side)
//   --duration <sec>   fill length in seconds         (default 2.0)
//   --mode <m>         static | hybrid | complex      (default hybrid)
//   --seed <int>       deterministic seed             (default 0)
//   --out <wav>        output fill                    (default fill.wav)
//   --analyze-only     run analysis + print diagnostics, no render/output

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/DiagnosticsLogger.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/render/RenderManager.h"
#include "engine/model/RenderSettings.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
namespace model    = tonefill::engine::model;
namespace analysis = tonefill::engine::analysis;
namespace render   = tonefill::engine::render;

// ---- tiny --key value parser (version-proof; avoids juce::ArgumentList API drift) ----
std::string optValue (int argc, char** argv, const std::string& key, const std::string& def = {})
{
    for (int i = 1; i + 1 < argc; ++i)
        if (key == argv[i])
            return argv[i + 1];
    return def;
}

bool hasFlag (int argc, char** argv, const std::string& key)
{
    for (int i = 1; i < argc; ++i)
        if (key == argv[i])
            return true;
    return false;
}

model::Mode parseMode (const std::string& m)
{
    if (m == "static")  return model::Mode::Static;
    if (m == "complex") return model::Mode::Complex;
    return model::Mode::Hybrid; // default
}

// ---- WAV I/O ----
bool loadWav (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
        return false;

    out.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&out, 0, (int) reader->lengthInSamples, 0, true, true);
    sampleRate = reader->sampleRate;
    return true;
}

bool writeWav (const juce::File& file,
               const std::vector<std::vector<float>>& channels,
               double sampleRate)
{
    if (channels.empty() || channels[0].empty())
        return false;

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate,
                             (unsigned int) channels.size(), 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // writer owns the stream now

    std::vector<const float*> ptrs;
    ptrs.reserve (channels.size());
    for (const auto& ch : channels)
        ptrs.push_back (ch.data());

    writer->writeFromFloatArrays (ptrs.data(), (int) channels.size(),
                                  (int) channels[0].size());
    return true;
}

void printDiagnostics (tonefill::core::DiagnosticsLogger& diag)
{
    for (const auto& e : diag.snapshot())
        std::cout << "  [" << e.code << "] " << e.message << '\n';
}
} // namespace

int main (int argc, char** argv)
{
    using tonefill::core::Status;

    const std::string inPath    = optValue (argc, argv, "--in");
    const std::string leftPath  = optValue (argc, argv, "--left",  inPath);
    const std::string rightPath = optValue (argc, argv, "--right", inPath);
    const std::string outPath   = optValue (argc, argv, "--out", "fill.wav");
    const double      duration  = std::stod (optValue (argc, argv, "--duration", "2.0"));
    const std::string modeStr   = optValue (argc, argv, "--mode", "hybrid");
    const std::uint64_t seed    = std::stoull (optValue (argc, argv, "--seed", "0"));
    const bool        analyzeOnly = hasFlag (argc, argv, "--analyze-only");

    if (leftPath.empty() && rightPath.empty())
    {
        std::cerr << "error: provide --in <wav> (or --left/--right). See header for usage.\n";
        return 2;
    }

    // ---- Load context audio ----
    analysis::AnalysisContext ctx;
    double srLeft = 0.0, srRight = 0.0;

    if (! leftPath.empty() && ! loadWav (juce::File (leftPath), ctx.leftContext, srLeft))
    {
        std::cerr << "error: cannot read left/in WAV: " << leftPath << '\n';
        return 2;
    }
    if (! rightPath.empty() && ! loadWav (juce::File (rightPath), ctx.rightContext, srRight))
    {
        std::cerr << "error: cannot read right WAV: " << rightPath << '\n';
        return 2;
    }

    const double sampleRate = srLeft > 0.0 ? srLeft : srRight;
    ctx.analysisSampleRate = sampleRate;
    ctx.numChannels = juce::jmax (ctx.leftContext.getNumChannels(),
                                  ctx.rightContext.getNumChannels(), 1);
    ctx.leftEnabled  = ! leftPath.empty()  && ctx.leftContext.getNumSamples()  > 0;
    ctx.rightEnabled = ! rightPath.empty() && ctx.rightContext.getNumSamples() > 0;
    ctx.sourceContentHash = (std::uint64_t) (ctx.leftContext.getNumSamples()
                                             + ctx.rightContext.getNumSamples());

    std::cout << "Loaded: " << ctx.numChannels << " ch @ " << sampleRate << " Hz, "
              << "left=" << ctx.leftContext.getNumSamples() << " smp, "
              << "right=" << ctx.rightContext.getNumSamples() << " smp\n";

    // ---- Analyze ----
    tonefill::core::DiagnosticsLogger diag;
    analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    const auto t0 = std::chrono::steady_clock::now();
    auto modelResult = session.run (ctx, cancel);
    const auto t1 = std::chrono::steady_clock::now();

    if (! modelResult.ok())
    {
        std::cerr << "analysis failed: " << modelResult.error().message << '\n';
        printDiagnostics (diag);
        return 1;
    }

    const auto analyzeMs =
        std::chrono::duration_cast<std::chrono::milliseconds> (t1 - t0).count();
    std::cout << "Analysis OK in " << analyzeMs << " ms\n";
    printDiagnostics (diag);

    // Detected tonal partials (channel 0) — visibility into the tonal layer.
    if (const auto& m = *modelResult.value(); ! m.tonalPerChannel.empty())
    {
        const auto& partials = m.tonalPerChannel[0].partials;
        std::cout << "Tonal partials (ch0): " << partials.size() << '\n';
        for (std::size_t i = 0; i < partials.size() && i < 8; ++i)
            std::cout << "  " << partials[i].frequencyHz << " Hz  amp=" << partials[i].amplitude << '\n';
    }

    if (analyzeOnly)
        return 0;

    // ---- Render ----
    render::RenderManager rm;
    rm.setModel (modelResult.value());

    model::RenderSettings settings;
    settings.mode                  = parseMode (modeStr);
    settings.seed                  = seed;
    settings.targetSampleRate      = sampleRate;
    settings.targetChannels        = ctx.numChannels;
    settings.targetDurationSamples = (long long) std::llround (duration * sampleRate);

    render::RenderResultPtr fill;
    const auto r0 = std::chrono::steady_clock::now();
    rm.requestRender (settings,
        [&fill] (tonefill::core::Result<render::RenderResultPtr> r)
        {
            if (r.ok()) fill = r.value();
            else        std::cerr << "render failed: " << r.error().message << '\n';
        });
    const auto r1 = std::chrono::steady_clock::now();

    if (fill == nullptr)
        return 1;

    const auto renderMs =
        std::chrono::duration_cast<std::chrono::milliseconds> (r1 - r0).count();
    std::cout << "Render OK in " << renderMs << " ms ("
              << settings.targetDurationSamples << " smp, mode=" << modeStr
              << ", seed=" << seed << ")\n";

    if (! writeWav (juce::File (outPath), fill->channels, fill->sampleRate))
    {
        std::cerr << "error: cannot write output WAV: " << outPath << '\n';
        return 1;
    }

    std::cout << "Wrote: " << outPath << '\n';
    return 0;
}
