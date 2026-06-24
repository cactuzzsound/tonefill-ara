#include "engine/analysis/AmbienceModelBuilder.h"
#include "dsp/Stft.h"
#include "dsp/TonalDetect.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace tonefill::engine::analysis
{
namespace
{
// Select clean room-tone material: keep low-energy, contiguous runs (the quiet gaps between
// dialogue) and drop the loud/speech frames. The whole point — without this the model learns
// the hum/grains from the DIALOGUE, which sounds wrong. Falls back to the full buffer if there
// is too little quiet material.
juce::AudioBuffer<float> selectCleanAmbience (const juce::AudioBuffer<float>& src, double sr,
                                              float thresholdNorm)
{
    const int N = 2048, H = 512;
    const int numCh = src.getNumChannels();
    const int len = src.getNumSamples();
    if (numCh <= 0 || len < 4 * N) { juce::AudioBuffer<float> c; c.makeCopyOf (src); return c; }

    // Speech-band copy (high-pass ~300 Hz): voices/claps/clicks live here; steady hum does not.
    // A frame is clean only if BOTH the broadband level AND the speech-band level are near their
    // floors -> excludes quiet background voices that broadband level alone would keep.
    juce::AudioBuffer<float> hp;
    hp.makeCopyOf (src);
    for (int ch = 0; ch < numCh; ++ch)
    {
        juce::IIRFilter f;
        f.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 300.0));
        f.processSamples (hp.getWritePointer (ch), len);
    }

    const int numFrames = (len - N) / H + 1;
    std::vector<float> frameRms ((std::size_t) numFrames, 0.0f), midRms ((std::size_t) numFrames, 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        float b = 0.0f, m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            b = juce::jmax (b, src.getRMSLevel (ch, f * H, N));
            m = juce::jmax (m, hp.getRMSLevel (ch, f * H, N));
        }
        frameRms[(std::size_t) f] = b;
        midRms[(std::size_t) f]   = m;
    }

    auto floorOf = [numFrames] (std::vector<float> v)
    {
        std::sort (v.begin(), v.end());
        return v[(std::size_t) (numFrames / 10)]; // 10th percentile
    };
    const float g = std::pow (10.0f, juce::jlimit (0.0f, 1.0f, thresholdNorm) * 30.0f / 20.0f);
    const float bThresh = juce::jmax (floorOf (frameRms) * g, 1.0e-5f);
    const float mThresh = juce::jmax (floorOf (midRms) * g, 1.0e-6f);

    std::vector<char> frameClean ((std::size_t) numFrames, 0);
    for (int f = 0; f < numFrames; ++f)
        frameClean[(std::size_t) f] = (frameRms[(std::size_t) f] < bThresh && midRms[(std::size_t) f] < mThresh) ? 1 : 0;

    // Contiguous quiet runs of >= ~85 ms.
    const int minRun = 4;
    std::vector<std::pair<int, int>> runs;
    int runStart = -1;
    for (int f = 0; f <= numFrames; ++f)
    {
        const bool quiet = (f < numFrames) && (frameClean[(std::size_t) f] != 0);
        if (quiet && runStart < 0) runStart = f;
        else if (! quiet && runStart >= 0)
        {
            if (f - runStart >= minRun) runs.push_back ({ runStart, f - 1 });
            runStart = -1;
        }
    }

    // Concatenate ALL clean runs with a short equal-power crossfade at each join (no click).
    // Maximum varied clean material -> the concatenator/granular don't obviously repeat. Only
    // fall back to the whole buffer if there is essentially no quiet material.
    long long total = 0;
    for (const auto& r : runs) total += juce::jmin ((r.second - r.first) * H + N, len - r.first * H);
    if (runs.empty() || total < (long long) (0.1 * sr))
    {
        juce::AudioBuffer<float> c; c.makeCopyOf (src); return c;
    }

    const int joinXf = (int) (0.02 * sr); // 20 ms join crossfade
    const float halfPi = 1.5707963267948966f;
    juce::AudioBuffer<float> clean (numCh, (int) total);
    int w = 0;
    for (std::size_t ri = 0; ri < runs.size(); ++ri)
    {
        const int s = runs[ri].first * H;
        const int span = juce::jmin ((runs[ri].second - runs[ri].first) * H + N, len - s);
        if (w == 0)
        {
            for (int ch = 0; ch < numCh; ++ch) clean.copyFrom (ch, 0, src, ch, s, span);
            w = span;
        }
        else
        {
            const int ov = juce::jmin (joinXf, juce::jmin (span, w));
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = clean.getWritePointer (ch);
                const float* sp = src.getReadPointer (ch);
                for (int i = 0; i < ov; ++i)
                {
                    const float g = (float) i / (float) juce::jmax (1, ov - 1) * halfPi;
                    d[w - ov + i] = d[w - ov + i] * std::cos (g) + sp[s + i] * std::sin (g);
                }
                clean.copyFrom (ch, w, src, ch, s + ov, span - ov);
            }
            w += span - ov;
        }
    }
    clean.setSize (numCh, w, true);
    return clean;
}
} // namespace
} // namespace tonefill::engine::analysis

namespace tonefill::engine::analysis
{
core::Result<model::AmbienceModelPtr>
AmbienceModelBuilder::assemble (const AnalysisContext& ctx, std::atomic<bool>& cancelFlag) const
{
    using core::Status;

    if (cancelFlag.load())
        return core::Result<model::AmbienceModelPtr>::fail (Status::Cancelled, "Analysis cancelled");

    auto model = std::make_shared<model::AmbienceModel>();
    model->numChannels        = ctx.numChannels;
    model->analysisSampleRate = ctx.analysisSampleRate;
    model->modelId            = ctx.sourceContentHash; // TODO: fold in analysis params

    model->tonalPerChannel.resize (static_cast<std::size_t> (ctx.numChannels));
    model->noisePerChannel.resize (static_cast<std::size_t> (ctx.numChannels));
    model->grainsPerChannel.resize (static_cast<std::size_t> (ctx.numChannels));
    model->boundariesPerChannel.resize (static_cast<std::size_t> (ctx.numChannels));

    // First real measurement (TF-501a): per-channel broadband RMS of the learn material.
    // Prefer the left context, fall back to the right. This is the target level the Static
    // noise bed reproduces. TODO(TF-501): move into NoiseProfileBuilder with LTAS + bands.
    const juce::AudioBuffer<float>& raw =
        (ctx.leftEnabled && ctx.leftContext.getNumSamples() > 0) ? ctx.leftContext
                                                                 : ctx.rightContext;
    // Learn ONLY from clean ambience (quiet gaps), not the dialogue. Everything below
    // (RMS, LTAS, tonal, grains) uses this filtered material.
    const juce::AudioBuffer<float> learn = selectCleanAmbience (raw, ctx.analysisSampleRate, ctx.cleanThreshold);
    const juce::AudioBuffer<float>& src = learn;

    // Keep the clean real audio for the Ambience (concatenative) mode.
    model->cleanAudioPerChannel.resize ((std::size_t) learn.getNumChannels());
    for (int ch = 0; ch < learn.getNumChannels(); ++ch)
        model->cleanAudioPerChannel[(std::size_t) ch].assign (learn.getReadPointer (ch),
                                                              learn.getReadPointer (ch) + learn.getNumSamples());
    // LTAS (TF-501b): average magnitude spectrum over the learn material -> noise-bed shape.
    dsp::StftConfig stftCfg; // 2048 / 512 / Hann
    dsp::Stft stft (stftCfg);
    const int bins = stft.numBins();
    std::vector<float> mag, phase;

    for (int ch = 0; ch < ctx.numChannels; ++ch)
    {
        float rms = 0.0f;
        if (ch < src.getNumChannels() && src.getNumSamples() > 0)
            rms = src.getRMSLevel (ch, 0, src.getNumSamples());

        auto& profile = model->noisePerChannel[(std::size_t) ch];
        profile.targetRms = rms;
        profile.fftSize   = stftCfg.fftSize;

        if (ch < src.getNumChannels() && src.getNumSamples() > 0)
        {
            std::vector<double> avg ((std::size_t) bins, 0.0);
            const int frames = dsp::Stft::numFrames (src.getNumSamples(), stftCfg.hop);
            for (int f = 0; f < frames; ++f)
            {
                if (cancelFlag.load()) break;
                stft.analyze (src.getReadPointer (ch), src.getNumSamples(), f * stftCfg.hop, mag, phase);
                for (int b = 0; b < bins; ++b) avg[(std::size_t) b] += mag[(std::size_t) b];
            }
            profile.residualLtas.resize ((std::size_t) bins);
            const double inv = frames > 0 ? 1.0 / (double) frames : 0.0;
            for (int b = 0; b < bins; ++b)
                profile.residualLtas[(std::size_t) b] = (float) (avg[(std::size_t) b] * inv);

            // Tonal layer (TF-401): detect steady partials, store them, and NOTCH them out of
            // the noise LTAS so the hum is reproduced as a clean tone, not as noise (no double-count).
            const auto partials = dsp::detectPartials (src.getReadPointer (ch), src.getNumSamples(),
                                                       ctx.analysisSampleRate);
            auto& tonal = model->tonalPerChannel[(std::size_t) ch];
            const double binHz2048 = ctx.analysisSampleRate / (double) stftCfg.fftSize;

            // Boundary capture (TF-505): per-partial instantaneous phase at the PRE seam (the
            // last sample of the learn material). The renderer continues this phase into the
            // gap so the hum joins seamlessly. detectPartials' phase is at sample 0, so advance
            // it to the seam sample (L-1).
            auto& bpre = model->boundariesPerChannel[(std::size_t) ch].pre;
            bpre.side    = model::BoundaryConditionProfile::Side::Pre;
            bpre.channel = ch;
            const double twoPi = 6.283185307179586;
            const double seamSample = (double) (src.getNumSamples() - 1);

            for (const auto& p : partials)
            {
                model::Partial mp;
                mp.frequencyHz = p.freqHz;
                mp.amplitude   = p.amplitude;
                mp.refPhase    = p.phase;
                mp.stability   = 1.0f;
                tonal.partials.push_back (mp);

                const double wA = twoPi * p.freqHz / ctx.analysisSampleRate;
                bpre.partialPhaseAtSeam.push_back ((float) (p.phase + wA * seamSample));

                const int pb = (int) std::lround (p.freqHz / binHz2048);
                const float floorVal =
                    (pb + 4 < bins) ? profile.residualLtas[(std::size_t) (pb + 4)] : 0.0f;
                for (int k = pb - 1; k <= pb + 1; ++k)
                    if (k >= 0 && k < bins) profile.residualLtas[(std::size_t) k] = floorVal;
            }

            // Seam snippets (TF-505 cz.2): real neighbour audio at each gap edge, used by the
            // commit/splice path for an equal-power crossfade (broadband seam continuity).
            // pre = tail of the left context; post = head of the right context.
            auto& bpost = model->boundariesPerChannel[(std::size_t) ch].post;
            bpost.side    = model::BoundaryConditionProfile::Side::Post;
            bpost.channel = ch;
            const int snip = juce::jmin ((int) (0.05 * ctx.analysisSampleRate), src.getNumSamples());

            auto capture = [snip] (const juce::AudioBuffer<float>& buf, int c, bool fromEnd,
                                   std::vector<float>& dst, float& rmsOut)
            {
                if (c >= buf.getNumChannels() || buf.getNumSamples() <= 0) return;
                const int len = juce::jmin (snip, buf.getNumSamples());
                const int start = fromEnd ? buf.getNumSamples() - len : 0;
                const float* s = buf.getReadPointer (c);
                dst.assign (s + start, s + start + len);
                rmsOut = buf.getRMSLevel (c, start, len);
            };

            if (ctx.leftContext.getNumSamples() > 0)
                capture (ctx.leftContext,  ch, true,  bpre.seamSnippet,  bpre.seamRms);
            if (ctx.rightContext.getNumSamples() > 0)
                capture (ctx.rightContext, ch, false, bpost.seamSnippet, bpost.seamRms);

            // Tonal-removed residual (TF-601): source minus the synthesized partials. The
            // granular layer draws texture grains from this so it doesn't re-introduce the hum.
            auto& corpus = model->grainsPerChannel[(std::size_t) ch];
            corpus.grainSizeSamples = (int) (0.12 * ctx.analysisSampleRate); // 120 ms
            corpus.hopSamples       = corpus.grainSizeSamples / 2;
            corpus.sourceResidual.assign (src.getReadPointer (ch),
                                          src.getReadPointer (ch) + src.getNumSamples());
            for (const auto& mp : tonal.partials)
            {
                const double wA = twoPi * mp.frequencyHz / ctx.analysisSampleRate;
                for (int i = 0; i < src.getNumSamples(); ++i)
                    corpus.sourceResidual[(std::size_t) i] -=
                        mp.amplitude * (float) std::sin (wA * (double) i + mp.refPhase);
            }
            corpus.usableCount =
                juce::jmax (0, (src.getNumSamples() - corpus.grainSizeSamples) / corpus.hopSamples + 1);
        }
    }

    model->learnMaterialSeconds =
        (float) src.getNumSamples() / (float) (ctx.analysisSampleRate > 0 ? ctx.analysisSampleRate : 48000.0);

    // TODO(§E): for each channel:
    //   1. frameSelector_.select(...) on left/right context -> learn segments.
    //   2. concatenate selected segments into clean material.
    //   3. tonalBuilder_.build(...) -> TonalLayer + residual.
    //   4. noiseBuilder_.build(residual, full, ...) -> NoiseProfile.
    //   5. grainBuilder_.build(residual, ...) -> GrainCorpus.
    //   6. BoundaryMatcher -> BoundaryConditionProfile (Pre/Post).
    //   7. set diagnostics flags (speechContaminationHigh, loopRiskHigh, learnMaterialSeconds).
    // Check cancelFlag between stages.

    return core::Result<model::AmbienceModelPtr> (model::AmbienceModelPtr (std::move (model)));
}
} // namespace tonefill::engine::analysis
