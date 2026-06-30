#include "engine/analysis/AmbienceModelBuilder.h"
#include "dsp/Stft.h"
#include "dsp/TonalDetect.h"
#include "dsp/VoiceDetect.h"

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
                                              float thresholdNorm, float speechReject,
                                              std::vector<std::pair<int, int>>* rangesOut = nullptr)
{
    if (rangesOut != nullptr) rangesOut->clear();
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
    // A second, higher band (>2 kHz) gives a cheap spectral-shape fingerprint per frame, used
    // below to keep only the dominant room-tone "colour" and drop odd fragments (passing car,
    // different room) that would otherwise be concatenated and not blend.
    juce::AudioBuffer<float> hpHi;
    hpHi.makeCopyOf (src);
    for (int ch = 0; ch < numCh; ++ch)
    {
        juce::IIRFilter f;
        f.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 2000.0));
        f.processSamples (hpHi.getWritePointer (ch), len);
    }

    const int numFrames = (len - N) / H + 1;
    std::vector<float> frameRms ((std::size_t) numFrames, 0.0f), midRms ((std::size_t) numFrames, 0.0f),
                       hiRms ((std::size_t) numFrames, 0.0f);
    for (int f = 0; f < numFrames; ++f)
    {
        float b = 0.0f, m = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            b  = juce::jmax (b,  src.getRMSLevel (ch, f * H, N));
            m  = juce::jmax (m,  hp.getRMSLevel (ch, f * H, N));
            hi = juce::jmax (hi, hpHi.getRMSLevel (ch, f * H, N));
        }
        frameRms[(std::size_t) f] = b;
        midRms[(std::size_t) f]   = m;
        hiRms[(std::size_t) f]    = hi;
    }

    auto floorOf = [numFrames] (std::vector<float> v)
    {
        std::sort (v.begin(), v.end());
        return v[(std::size_t) (numFrames / 10)]; // 10th percentile
    };
    const float g = std::pow (10.0f, juce::jlimit (0.0f, 1.0f, thresholdNorm) * 30.0f / 20.0f);
    const float bThresh = juce::jmax (floorOf (frameRms) * g, 1.0e-5f);
    const float mThresh = juce::jmax (floorOf (midRms) * g, 1.0e-6f);

    // --- Speech rejection -------------------------------------------------------------------
    // libfvad runs on the SPEECH-BAND (high-passed) signal: stripping the dominant low-frequency
    // engine/road rumble lifts the speech SNR, so dialog buried in loud noise is detected.
    const float srj = juce::jlimit (0.0f, 1.0f, speechReject);
    const int   vadMode   = juce::jlimit (0, 3, (int) std::lround ((1.0f - srj) * 3.0f));
    const auto  voiced    = dsp::detectVoice (hp.getReadPointer (0), len, sr, vadMode);
    const int   voicedDen = (int) std::lround (2.0 + srj * 6.0);  // reject if > 1/2 .. 1/8 voiced
    const float harmThresh = 0.62f - srj * 0.30f;                 // periodicity gate 0.62 .. 0.32
    const int   guard      = (int) std::lround (srj * 3.0);       // dilate speech 0 .. 3 frames

    // Harmonicity gate: a strong pitch-period autocorrelation peak on the speech band marks a
    // voiced vowel even when its level sits under the noise floor (the case libfvad misses in a
    // loud car). Decimated by D to stay cheap; only the 80..400 Hz pitch range is searched, so a
    // sub-80 Hz hum is never flagged.
    const int D = 4;
    const int lagMin = juce::jmax (1, (int) (sr / 400.0 / D));
    const int lagMax = (int) (sr / 80.0 / D);
    auto harmonic = [&] (int f) -> bool
    {
        const float* x = hp.getReadPointer (0) + (long) f * H;
        const int n  = juce::jmin (N, len - f * H);
        const int nd = n / D;
        if (nd <= lagMax + 4) return false;
        double e0 = 0.0;
        for (int i = 0; i < nd; ++i) { const double v = x[i * D]; e0 += v * v; }
        if (e0 < 1.0e-12) return false;
        float best = 0.0f;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            double num = 0.0, e1 = 0.0;
            for (int i = 0; i + lag < nd; ++i)
            {
                const double a = x[i * D], b = x[(i + lag) * D];
                num += a * b; e1 += b * b;
            }
            best = juce::jmax (best, (float) (num / (std::sqrt (e0 * e1) + 1.0e-12)));
        }
        return best > harmThresh;
    };

    std::vector<char> harm ((std::size_t) numFrames, 0);
    int harmCount = 0;
    for (int f = 0; f < numFrames; ++f) if (harmonic (f)) { harm[(std::size_t) f] = 1; ++harmCount; }
    // If periodicity is everywhere it IS the ambience (steady engine drone), not intermittent
    // speech -> don't let the harmonic gate delete the very material we want to keep.
    const bool useHarm = harmCount <= numFrames * 2 / 5;

    std::vector<char> speech ((std::size_t) numFrames, 0);
    for (int f = 0; f < numFrames; ++f)
    {
        bool sp = false;
        if (! voiced.empty())
        {
            int vc = 0;
            for (int i = f * H; i < juce::jmin (f * H + N, len); ++i) vc += voiced[(std::size_t) i];
            if (vc > N / voicedDen) sp = true;
        }
        if (useHarm && harm[(std::size_t) f]) sp = true;
        speech[(std::size_t) f] = sp ? 1 : 0;
    }

    // Non-stationarity gate (breaths / mouth clicks / lip smacks / unvoiced consonants): steady
    // room tone sits AT the local speech-band floor; these events are short transient BUMPS above
    // it. They are aperiodic and unvoiced, so VAD and the harmonic gate both miss them - but a
    // rise over the local floor catches them. Margin scales with Dialog Reject. This is what was
    // letting breaths through at max.
    if (srj > 0.05f)
    {
        const int   W = juce::jmax (8, (int) (1.0 * sr / H)); // ~1 s local floor window
        const float eventMargin = 1.25f + (1.0f - srj) * 2.5f; // 1.25x (srj=1) .. 3.75x (srj~0)
        std::vector<char> event ((std::size_t) numFrames, 0);
        for (int f = 0; f < numFrames; ++f)
        {
            float localMin = midRms[(std::size_t) f];
            const int a = juce::jmax (0, f - W), b = juce::jmin (numFrames - 1, f + W);
            for (int k = a; k <= b; ++k) localMin = juce::jmin (localMin, midRms[(std::size_t) k]);
            if (midRms[(std::size_t) f] > localMin * eventMargin + 1.0e-7f) event[(std::size_t) f] = 1;
        }
        for (int f = 0; f < numFrames; ++f) if (event[(std::size_t) f]) speech[(std::size_t) f] = 1;
    }

    // Dilate speech to cover onsets/offsets where the detectors are weakest.
    std::vector<char> speechD = speech;
    if (guard > 0)
        for (int f = 0; f < numFrames; ++f)
            if (speech[(std::size_t) f])
                for (int d = -guard; d <= guard; ++d)
                    { const int k = f + d; if (k >= 0 && k < numFrames) speechD[(std::size_t) k] = 1; }

    std::vector<char> frameClean ((std::size_t) numFrames, 0);
    for (int f = 0; f < numFrames; ++f)
        frameClean[(std::size_t) f] = (frameRms[(std::size_t) f] < bThresh
                                       && midRms[(std::size_t) f] < mThresh
                                       && ! speechD[(std::size_t) f]) ? 1 : 0;

    // --- Spectral-shape consistency: keep only the dominant room-tone "colour" -----------------
    // Even quiet, non-speech frames can have a different character (a car passing outside, another
    // room tone). Concatenating mismatched colours sounds wrong. Build a robust centre (median of
    // the spectral tilt) over the clean candidates and drop outliers; the same gate is applied to
    // the top-up so added material matches too. Tighter as Dialog Reject rises.
    auto tiltOf = [&] (int f, float& t1, float& t2)
    {
        const float eps = 1.0e-7f;
        const float lb = std::log (frameRms[(std::size_t) f] + eps);
        const float lm = std::log (midRms[(std::size_t) f] + eps);
        const float lh = std::log (hiRms[(std::size_t) f] + eps);
        t1 = lm - lb;   // energy above 300 Hz vs broadband
        t2 = lh - lm;   // energy above 2 kHz vs mid
    };
    bool  haveCluster = false;
    float cT1 = 0.0f, cT2 = 0.0f, sT1 = 1.0f, sT2 = 1.0f;
    const float clusterK = 3.5f - srj * 2.0f; // tolerance in MADs: 1.5 (srj=1) .. 3.5 (srj=0)
    {
        std::vector<float> T1, T2;
        for (int f = 0; f < numFrames; ++f)
            if (frameClean[(std::size_t) f]) { float a, b; tiltOf (f, a, b); T1.push_back (a); T2.push_back (b); }
        if (T1.size() >= 8)
        {
            auto median = [] (std::vector<float> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
            auto mad = [] (const std::vector<float>& v, float m)
            {
                std::vector<float> d; d.reserve (v.size());
                for (float x : v) d.push_back (std::fabs (x - m));
                std::sort (d.begin(), d.end());
                return juce::jmax (1.0e-3f, d[d.size() / 2]);
            };
            cT1 = median (T1); cT2 = median (T2);
            sT1 = mad (T1, cT1); sT2 = mad (T2, cT2);
            haveCluster = true;
        }
    }
    auto withinCluster = [&] (int f)
    {
        if (! haveCluster) return true;
        float a, b; tiltOf (f, a, b);
        const float dist = 0.5f * (std::fabs (a - cT1) / sT1 + std::fabs (b - cT2) / sT2);
        return dist <= clusterK;
    };
    if (haveCluster)
        for (int f = 0; f < numFrames; ++f)
            if (frameClean[(std::size_t) f] && ! withinCluster (f)) frameClean[(std::size_t) f] = 0;

    auto buildRuns = [&] (int minRunLocal)
    {
        std::vector<std::pair<int, int>> rr;
        int runStart = -1;
        for (int f = 0; f <= numFrames; ++f)
        {
            const bool quiet = (f < numFrames) && (frameClean[(std::size_t) f] != 0);
            if (quiet && runStart < 0) runStart = f;
            else if (! quiet && runStart >= 0)
            {
                if (f - runStart >= minRunLocal) rr.push_back ({ runStart, f - 1 });
                runStart = -1;
            }
        }
        return rr;
    };

    auto sumRuns = [&] (const std::vector<std::pair<int, int>>& rr)
    {
        long long t = 0;
        for (const auto& r : rr) t += juce::jmin ((r.second - r.first) * H + N, len - r.first * H);
        return t;
    };

    std::vector<std::pair<int, int>> runs = buildRuns (2); // include >= ~43 ms fragments
    long long total = sumRuns (runs);

    // Yield guarantee: a long, talky source must not collapse to ~1 s of room tone. If the strict
    // pass found little, TOP UP with the quietest NON-SPEECH frames (dialog stays excluded via
    // speechD) until we reach a useful amount, then rebuild allowing short runs. The quietest
    // non-speech frames ARE the best available room tone (e.g. car interior between words).
    const long long target = (long long) (8.0 * sr);
    if (total < target)
    {
        std::vector<int> cand;
        for (int f = 0; f < numFrames; ++f)
            if (! frameClean[(std::size_t) f] && ! speechD[(std::size_t) f] && withinCluster (f)) cand.push_back (f);
        std::sort (cand.begin(), cand.end(),
                   [&] (int a, int b) { return frameRms[(std::size_t) a] < frameRms[(std::size_t) b]; });
        long long acc = total;
        for (int f : cand)
        {
            if (acc >= target) break;
            frameClean[(std::size_t) f] = 1;
            acc += H;
        }
        runs = buildRuns (1);
        total = sumRuns (runs);
    }

    // Last resort, only if the source is essentially ALL speech: use the whole buffer.
    if (runs.empty() || total < (long long) (0.05 * sr))
    {
        juce::AudioBuffer<float> c; c.makeCopyOf (src); return c;
    }

    if (rangesOut != nullptr)
        for (const auto& r : runs)
            rangesOut->push_back ({ r.first * H, juce::jmin (r.second * H + N, len) });

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
    std::vector<std::pair<int, int>> cleanRanges;
    juce::AudioBuffer<float> learn;
    if (ctx.useManualSelection)
        learn.makeCopyOf (raw); // raw already holds ONLY the user-selected material
    else
        learn = selectCleanAmbience (raw, ctx.analysisSampleRate, ctx.cleanThreshold, ctx.speechReject, &cleanRanges);
    const juce::AudioBuffer<float>& src = learn;
    model->cleanRanges = std::move (cleanRanges);

    // Robust stationary-noise spectrum -> drives the constant spectral re-synthesis bed.
    model->noiseSpectrum = dsp::estimateNoiseSpectrum (learn, 2048, 512);

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
