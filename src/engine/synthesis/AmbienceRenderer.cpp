#include "engine/synthesis/AmbienceRenderer.h"
#include "dsp/SeededRng.h"
#include "dsp/NoiseShaper.h"
#include "dsp/GranularSynth.h"
#include "dsp/MacroEnvelope.h"
#include "dsp/AmbienceConcat.h"

#include <juce_core/juce_core.h> // juce::ignoreUnused
#include <algorithm>
#include <cmath>

namespace tonefill::engine::synthesis
{
core::Result<AmbienceRenderer::Output>
AmbienceRenderer::render (const model::AmbienceModel& model,
                          const model::RenderSettings& settings,
                          std::atomic<bool>& cancelFlag) const
{
    using core::Status;

    if (settings.targetDurationSamples <= 0)
        return core::Result<Output>::fail (Status::InvalidInput, "Target duration is zero");
    if (cancelFlag.load())
        return core::Result<Output>::fail (Status::Cancelled, "Render cancelled");

    Output out;
    out.sampleRate = settings.targetSampleRate;
    out.channels.assign (static_cast<std::size_t> (settings.targetChannels),
                         std::vector<float> (static_cast<std::size_t> (settings.targetDurationSamples), 0.0f));

    // Phase-2 scaffold: dispatch by mode. Layers currently produce silence.
    switch (settings.mode)
    {
        case model::Mode::Static:   renderStatic   (model, settings, out); break;
        case model::Mode::Hybrid:   renderHybrid   (model, settings, out); break;
        case model::Mode::Complex:  renderComplex  (model, settings, out); break;
        case model::Mode::Ambience: renderAmbience (model, settings, out); break;
    }

    return core::Result<Output> (std::move (out));
}

void AmbienceRenderer::renderCore (const model::AmbienceModel& model,
                                   const model::RenderSettings& settings,
                                   Output& out, bool useGranular) const
{
    // Shared engine: per channel, split energy into tonal + broadband, build the broadband bed
    // (granular concatenation when useGranular and residual material exists, otherwise the
    // LTAS-shaped noise bed), add the phase-docked tonal layer, normalize to the learned level.
    dsp::SeededRng master (settings.seed);
    const float kSqrt3 = 1.7320508075688772f;
    const double sr = settings.targetSampleRate;
    const float kTwoPi = 6.283185307179586f;
    const int nCh = (int) out.channels.size();

    for (int ch = 0; ch < nCh; ++ch)
    {
        float targetRms = 0.0f;
        const std::vector<float>* ltas = nullptr;
        int fftSize = 2048;
        if (ch < (int) model.noisePerChannel.size())
        {
            const auto& p = model.noisePerChannel[(std::size_t) ch];
            targetRms = p.targetRms;
            if (! p.residualLtas.empty()) { ltas = &p.residualLtas; fftSize = p.fftSize; }
        }

        const model::TonalLayer* tonal =
            ch < (int) model.tonalPerChannel.size() ? &model.tonalPerChannel[(std::size_t) ch] : nullptr;

        // Energy split: a sinusoid of amplitude A has RMS A/sqrt(2). The noise bed fills only
        // the broadband remainder so tonal + noise sum to the target level.
        double tonalRms2 = 0.0;
        if (tonal)
            for (const auto& p : tonal->partials) tonalRms2 += 0.5 * (double) p.amplitude * p.amplitude;
        const float noiseRms =
            (float) std::sqrt (std::max (0.0, (double) targetRms * targetRms - tonalRms2));

        auto rng = master.deriveSubStream ((std::uint64_t) ch);
        auto& dst = out.channels[(std::size_t) ch];
        const int n = (int) dst.size();

        // --- broadband bed, scaled to the remainder energy ---
        const model::GrainCorpus* corpus =
            ch < (int) model.grainsPerChannel.size() ? &model.grainsPerChannel[(std::size_t) ch] : nullptr;
        const bool canGranular =
            useGranular && corpus != nullptr && (int) corpus->sourceResidual.size() > corpus->grainSizeSamples;

        if (canGranular)
        {
            // Granular: scramble residual grains -> textural movement without obvious looping.
            dsp::granularResynthesize (dst.data(), n, corpus->sourceResidual.data(),
                                       (int) corpus->sourceResidual.size(),
                                       corpus->grainSizeSamples, rng);
            double sumSq = 0.0;
            for (float s : dst) sumSq += (double) s * s;
            const float cur = n > 0 ? (float) std::sqrt (sumSq / n) : 0.0f;
            const float g = cur > 1e-9f ? noiseRms / cur : 0.0f;
            for (float& s : dst) s *= g;
        }
        else if (ltas != nullptr)
        {
            dsp::StftConfig cfg; cfg.fftSize = fftSize;
            dsp::NoiseShaper shaper (cfg);
            shaper.generate (dst.data(), n, *ltas, rng);
            double sumSq = 0.0;
            for (float s : dst) sumSq += (double) s * s;
            const float cur = n > 0 ? (float) std::sqrt (sumSq / n) : 0.0f;
            const float g = cur > 1e-9f ? noiseRms / cur : 0.0f;
            for (float& s : dst) s *= g;
        }
        else
        {
            const float scale = noiseRms * kSqrt3;
            for (float& s : dst) s = scale * (rng.nextFloat() * 2.0f - 1.0f);
        }

        // --- tonal layer: phase-coherent sinusoids, docked to the pre-gap seam ---
        const model::BoundaryConditionProfile* bpre =
            ch < (int) model.boundariesPerChannel.size() ? &model.boundariesPerChannel[(std::size_t) ch].pre
                                                         : nullptr;
        if (tonal)
            for (std::size_t k = 0; k < tonal->partials.size(); ++k)
            {
                const auto& p = tonal->partials[k];
                const float w = kTwoPi * p.frequencyHz / (float) sr;

                // Continue the boundary phase one sample into the gap (seamless join); fall
                // back to the learn-material reference phase if no boundary was captured.
                float phi0 = p.refPhase;
                if (bpre != nullptr && k < bpre->partialPhaseAtSeam.size())
                    phi0 = bpre->partialPhaseAtSeam[k] + w;

                for (int i = 0; i < n; ++i)
                    dst[(std::size_t) i] += p.amplitude * std::sin (w * (float) i + phi0);
            }

        // --- final safety: normalize total to the learned level (covers split estimation error) ---
        double sumSq = 0.0;
        for (float s : dst) sumSq += (double) s * s;
        const float cur = n > 0 ? (float) std::sqrt (sumSq / n) : 0.0f;
        if (cur > 1e-9f && targetRms > 0.0f)
        {
            const float g = targetRms / cur;
            for (float& s : dst) s *= g;
        }
    }
}

void AmbienceRenderer::renderStatic (const model::AmbienceModel& model,
                                     const model::RenderSettings& settings, Output& out) const
{
    renderCore (model, settings, out, /*useGranular*/ false);
}

void AmbienceRenderer::renderHybrid (const model::AmbienceModel& model,
                                     const model::RenderSettings& settings, Output& out) const
{
    renderCore (model, settings, out, /*useGranular*/ true);
}

void AmbienceRenderer::renderAmbience (const model::AmbienceModel& model,
                                      const model::RenderSettings& settings, Output& out) const
{
    // Concatenative REAL ambience: blend real clean fragments. No synthesis -> natural sound.
    // Falls back to the synth Hybrid bed if there is no clean material to draw from.
    const bool haveClean = ! model.cleanAudioPerChannel.empty()
                           && ! model.cleanAudioPerChannel[0].empty();
    if (! haveClean) { renderCore (model, settings, out, /*useGranular*/ true); return; }

    const double sr = settings.targetSampleRate;
    const int fragLen  = juce::jmax (256, (int) (settings.fragmentMs * 0.001 * sr));
    const int xfadeLen = juce::jmax (64, (int) (settings.blendFrac * fragLen));
    dsp::SeededRng master (settings.seed);

    for (std::size_t ch = 0; ch < out.channels.size(); ++ch)
    {
        const auto& srcCh = model.cleanAudioPerChannel[juce::jmin (ch, model.cleanAudioPerChannel.size() - 1)];
        auto rng = master.deriveSubStream (ch);
        dsp::concatenateAmbience (out.channels[ch].data(), (int) out.channels[ch].size(),
                                  srcCh.data(), (int) srcCh.size(), fragLen, xfadeLen, rng);
    }
}

void AmbienceRenderer::renderComplex (const model::AmbienceModel& model,
                                      const model::RenderSettings& settings, Output& out) const
{
    renderCore (model, settings, out, /*useGranular*/ true);

    // Macro envelope: slow "breathing" level variation, endpoints anchored to keep seam levels.
    // depth scales with the Movement parameter (movement=1 -> ~+/-15%).
    if (settings.movement > 0.0f)
    {
        dsp::SeededRng macroMaster (settings.seed ^ 0xA1B2C3D4E5F60718ULL);
        const float depth = settings.movement * 0.15f;
        for (std::size_t ch = 0; ch < out.channels.size(); ++ch)
        {
            auto rng = macroMaster.deriveSubStream (ch);
            dsp::applyMacroEnvelope (out.channels[ch].data(), (int) out.channels[ch].size(),
                                     depth, settings.targetSampleRate, rng);
        }
    }
}
} // namespace tonefill::engine::synthesis
