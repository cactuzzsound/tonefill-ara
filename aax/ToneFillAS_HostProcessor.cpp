#include "ToneFillAS_HostProcessor.h"
#include "ToneFillAS_Defs.h"
#include "ToneFillAS_Parameters.h"
#include "ASShared.h"

#include "AAX_IController.h"
#include "AAX_IEffectParameters.h"

#include "engine/analysis/AnalysisSession.h"
#include "engine/analysis/AnalysisContext.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"
#include "core/DiagnosticsLogger.h"
#include "dsp/Loudness.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tonefill_aax;

namespace
{
void asLog (const juce::String& msg)
{
    juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("tonefill_aax.log")
        .appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + msg + "\n");
}

// Concatenate the manual regions (source-sample coords) into a learn buffer with tiny equal-power
// joins (mirror of the JUCE plugin's buildManual).
juce::AudioBuffer<float> buildManual (const juce::AudioBuffer<float>& src,
                                      const std::vector<std::pair<int, int>>& ranges, double sr)
{
    const int ch = src.getNumChannels(), n = src.getNumSamples();
    const int xf = (int) (0.02 * sr);
    long long tot = 0;
    for (const auto& r : ranges) tot += juce::jlimit (0, n, r.second) - juce::jlimit (0, n, r.first);
    if (tot < (long long) (0.05 * sr)) { juce::AudioBuffer<float> c; c.makeCopyOf (src); return c; }
    juce::AudioBuffer<float> out (ch, (int) tot);
    int w = 0;
    for (const auto& r : ranges)
    {
        const int s = juce::jlimit (0, n, r.first), e = juce::jlimit (0, n, r.second), len = e - s;
        if (len <= 0) continue;
        for (int c = 0; c < ch; ++c)
        {
            if (w > 0 && xf > 0)
            {
                const int ov = juce::jmin (xf, juce::jmin (len, w));
                float* d = out.getWritePointer (c); const float* sp = src.getReadPointer (c);
                for (int i = 0; i < ov; ++i)
                { const float g = (float) i / (float) juce::jmax (1, ov - 1) * 1.5707964f;
                  d[w - ov + i] = d[w - ov + i] * std::cos (g) + sp[s + i] * std::sin (g); }
                out.copyFrom (c, w, src, c, s + ov, len - ov);
            }
            else out.copyFrom (c, w, src, c, s, len);
        }
        w += (w > 0 && xf > 0) ? len - juce::jmin (xf, juce::jmin (len, w)) : len;
    }
    out.setSize (ch, juce::jmax (1, w), true);
    return out;
}
} // namespace

AAX_CHostProcessor* AAX_CALLBACK ToneFillAS_HostProcessor::Create() { return new ToneFillAS_HostProcessor(); }

ToneFillAS_HostProcessor::ToneFillAS_HostProcessor() = default;
ToneFillAS_HostProcessor::~ToneFillAS_HostProcessor() = default;

double ToneFillAS_HostProcessor::readNorm (const char* paramID) const
{
    double v = 0.0;
    if (auto* p = const_cast<ToneFillAS_HostProcessor*> (this)->GetEffectParameters())
        p->GetParameterNormalizedValue (paramID, &v);
    return v;
}

double ToneFillAS_HostProcessor::sampleRate() const
{
    AAX_CSampleRate sr = 48000;
    if (const auto* ctrl = Controller())
        ctrl->GetSampleRate (&sr);
    return (double) sr;
}

AAX_Result ToneFillAS_HostProcessor::PreRender (int32_t iAudioInCount, int32_t /*iAudioOutCount*/, int32_t /*iWindowSize*/)
{
    mChannels   = juce::jmax (1, iAudioInCount);
    mSampleRate = sampleRate();
    mGenPos     = 0;
    mHiss.clear(); mHissLastFreq = -1.0f; mHissLastQ = -1.0f; // fresh filter state per pass
    // NOTE: model + fill caches survive across passes (Preview -> Render) on purpose; ensureFill
    // re-analyses / re-renders only when the relevant parameters or the source range changed.
    asLog ("PreRender: ch=" + juce::String (mChannels) + " sr=" + juce::String (mSampleRate));
    return AAX_SUCCESS;
}

bool ToneFillAS_HostProcessor::analyze (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize)
{
    const int ch = juce::jmax (1, inAudioInCount);
    const int64_t s0 = GetSrcStart(), s1 = GetSrcEnd();
    const long long cap = (long long) (900.0 * mSampleRate); // 15 min

    // Random-access read of the source range via GetAudio (Pro Tools' WHOLE FILE widens s0..s1).
    std::vector<std::vector<float>> accum ((std::size_t) ch);
    long long len = 0;
    for (int64_t loc = s0; loc < s1 && len < cap; )
    {
        int32_t got = (int32_t) juce::jmin<int64_t> (windowSize, s1 - loc);
        if (got <= 0) break;
        if (GetAudio (inAudioIns, inAudioInCount, loc, &got) != AAX_SUCCESS || got <= 0) break;
        for (int c = 0; c < ch; ++c)
            if (inAudioIns[c]) accum[(std::size_t) c].insert (accum[(std::size_t) c].end(), inAudioIns[c], inAudioIns[c] + got);
        loc += got; len += got;
    }
    asLog ("analyze: srcRange=" + juce::String ((long long) (s1 - s0)) + " read=" + juce::String (len) + " ch=" + juce::String (ch));
    if (len < 4096) { asLog ("analyze: too little source"); return false; }

    juce::AudioBuffer<float> full (ch, (int) len);
    for (int c = 0; c < ch; ++c) full.copyFrom (c, 0, accum[(std::size_t) c].data(), (int) len);

    // Mono detection (mirror of the ARA path): near-identical L/R -> collapse to one correlated
    // channel so a dual-mono source renders as mono, not decorrelated stereo.
    int effCh = ch;
    if (ch >= 2)
    {
        double diff = 0.0, ref = 0.0;
        const float* a = full.getReadPointer (0); const float* b = full.getReadPointer (1);
        for (int i = 0; i < (int) len; ++i) { const double d = a[i] - b[i]; diff += d * d; ref += (double) a[i] * a[i]; }
        if (ref > 0.0 && diff / ref < 1.0e-4) effCh = 1;
    }
    juce::AudioBuffer<float> buf (effCh, (int) len);
    for (int c = 0; c < effCh; ++c) buf.copyFrom (c, 0, full, c, 0, (int) len);

    // Manual mode: learn only from the regions the user dragged on the waveform (from the GUI).
    ASShared* sh = nullptr;
    if (auto* p = dynamic_cast<ToneFillAS_Parameters*> (GetEffectParameters())) sh = &p->shared();
    std::vector<std::pair<int, int>> ranges;
    if (sh != nullptr) { const juce::SpinLock::ScopedLockType l (sh->lock); ranges = sh->manualRanges; }
    const bool useManual = readNorm (kParamManual) > 0.5 && ! ranges.empty();
    const juce::AudioBuffer<float> learnInput = useManual ? buildManual (buf, ranges, mSampleRate) : buf;

    tonefill::core::DiagnosticsLogger diag;
    tonefill::engine::analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    tonefill::engine::analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (learnInput);
    ctx.rightContext.makeCopyOf (learnInput);
    ctx.analysisSampleRate = mSampleRate;
    ctx.numChannels = effCh;
    ctx.leftEnabled = true;
    ctx.useManualSelection = useManual;
    ctx.cleanThreshold        = (float) readNorm (kParamClean);
    ctx.speechReject          = (float) readNorm (kParamVoice);
    ctx.flatness              = (float) readNorm (kParamFlatness);
    ctx.minFillSeconds        = (float) readReal (kParamMinFill, 0.2, 5.0);
    ctx.statisticalSelection  = readNorm (kParamExperim) > 0.5;
    ctx.sourceContentHash     = (std::uint64_t) len;

    auto rr = session.run (ctx, cancel);
    if (! rr.ok()) { asLog ("analyze: FAILED"); return false; }
    mModel = rr.value();
    asLog ("analyze: OK learnSec=" + juce::String (mModel->learnMaterialSeconds)
           + " avail=" + juce::String (mModel->availableCleanSeconds)
           + " chunks=" + juce::String ((int) mModel->cleanRanges.size()) + " manual=" + juce::String ((int) useManual));

    // Publish waveform + diagnostics to the GUI (peaks over the WHOLE analysed source; clean overlay
    // = the manual regions in Manual, or the auto-selected ranges otherwise).
    if (sh != nullptr)
    {
        const int bins = 600;
        const int per = juce::jmax (1, (int) (len / bins));
        std::vector<float> peak ((std::size_t) bins, 0.0f);
        std::vector<char>  clean ((std::size_t) bins, 0);
        const float* s0 = buf.getReadPointer (0);
        for (int b = 0; b < bins; ++b)
        {
            float pk = 0.0f; const int from = b * per, to = juce::jmin (from + per, (int) len);
            for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::fabs (s0[i]));
            peak[(std::size_t) b] = pk;
        }
        const auto& overlay = useManual ? ranges : mModel->cleanRanges;
        for (int b = 0; b < bins; ++b)
        {
            const int bs = b * per, be = bs + per; bool cl = false;
            for (const auto& r : overlay) if (r.first < be && r.second > bs) { cl = true; break; }
            clean[(std::size_t) b] = cl ? 1 : 0;
        }
        const float targetRms = (mModel->noisePerChannel.empty() ? 0.0f : mModel->noisePerChannel[0].targetRms);
        const juce::SpinLock::ScopedLockType l (sh->lock);
        sh->peak = std::move (peak); sh->clean = std::move (clean);
        sh->sourceSamples = (int) len; sh->sampleRate = mSampleRate;
        sh->usedSec = mModel->learnMaterialSeconds; sh->availSec = mModel->availableCleanSeconds;
        sh->seamDb = mModel->joinRoughnessDb; sh->chunks = (int) mModel->cleanRanges.size();
        sh->levelDb = targetRms > 0.0f ? 20.0f * std::log10 (targetRms) : -120.0f;
        sh->ready = true;
    }
    return true;
}

void ToneFillAS_HostProcessor::renderFill()
{
    if (mModel == nullptr) return;

    // Mirror of the JUCE FillWorker's render path.
    tonefill::engine::model::RenderSettings s;
    s.mode        = tonefill::engine::model::Mode::Ambience;
    s.paulStretch = readNorm (kParamEnhance) > 0.5;
    s.fragmentMs  = 200.0f + (float) readNorm (kParamChunk) * 2800.0f; // 200..3000 ms
    s.blendFrac   = 0.05f + (float) readNorm (kParamXfade) * 0.45f;    // 5..50 %
    s.randomness  = (float) readNorm (kParamSmooth);
    s.seed        = (std::uint64_t) juce::jmax (1.0, readReal (kParamSeed, 1.0, 100.0));

    // Loop length decoupled from Smoothness: scales with the clean material found (S7.1).
    const double availSec = (double) mModel->learnMaterialSeconds;
    const double loopSec  = juce::jlimit (15.0, 60.0, availSec * 4.0);
    const int loopLen = (int) (loopSec * mSampleRate);
    const int xf      = (int) (0.25 * mSampleRate);
    s.targetSampleRate      = mSampleRate;
    s.targetChannels        = juce::jmax (1, mModel->numChannels);
    s.targetDurationSamples = (long long) (loopLen + xf);

    std::atomic<bool> cancel { false };
    tonefill::engine::synthesis::AmbienceRenderer renderer;
    auto out = renderer.render (*mModel, s, cancel);
    if (! out.ok()) { asLog ("render: FAILED"); return; }
    auto chans = std::move (out.value().channels);

    // Seamless loop fold: blend the continuation tail back over the head, then trim.
    for (auto& c : chans)
        if ((int) c.size() >= loopLen + xf)
        {
            for (int i = 0; i < xf; ++i)
            {
                const float g = (float) i / (float) (xf - 1) * 1.5707963f;
                c[(std::size_t) i] = c[(std::size_t) (loopLen + i)] * std::cos (g) + c[(std::size_t) i] * std::sin (g);
            }
            c.resize ((std::size_t) loopLen);
        }
    if (chans.empty() || chans[0].empty()) { asLog ("render: empty"); return; }

    // Normalize: measured on the WHOLE loop (identical to the ARA applyAndPublish path). The hiss
    // filter is NOT baked here - it is applied live on the tiled output in RenderAudio, exactly like
    // the ARA processBlock, so measurement and filter-continuity match the plugin.
    if (readNorm (kParamNormOn) > 0.5)
    {
        const bool lufs = readNorm (kParamNormLufs) > 0.5;
        const double target = readReal (kParamNormTarget, -60.0, 0.0);
        const float measured = lufs ? tonefill::dsp::integratedLufs (chans, mSampleRate)
                                    : tonefill::dsp::peakDbfs (chans);
        if (measured > -119.0f)
        {
            float gainDb = (float) target - measured;
            if (lufs) // keep true peak under -1 dBFS, like the plugin
            {
                const float peakDb = tonefill::dsp::peakDbfs (chans);
                const float newPeak = peakDb + gainDb;
                if (newPeak > -1.0f) gainDb -= (newPeak + 1.0f);
            }
            const float g = std::pow (10.0f, gainDb / 20.0f);
            if (std::fabs (g - 1.0f) > 1.0e-4f)
                for (auto& c : chans) for (auto& v : c) v *= g;
            asLog ("normalize: meas=" + juce::String (measured, 1) + (lufs ? " LUFS" : " dBFS")
                   + " gainDb=" + juce::String (gainDb, 1));
        }
    }

    mFill = std::move (chans);
    mFillLen = (long long) mFill[0].size();
    asLog ("fill: len=" + juce::String (mFillLen) + " ch=" + juce::String ((int) mFill.size()));
}

void ToneFillAS_HostProcessor::ensureFill (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize)
{
    // Manual state + a cheap hash of the ranges -> re-analyse when the selection changes.
    long long manHash = readNorm (kParamManual) > 0.5 ? 1 : 0;
    if (auto* p = dynamic_cast<ToneFillAS_Parameters*> (GetEffectParameters()))
    {
        const juce::SpinLock::ScopedLockType l (p->shared().lock);
        for (const auto& r : p->shared().manualRanges) manHash = manHash * 1000003LL + r.first * 31 + r.second;
    }
    char sig[224];
    std::snprintf (sig, sizeof (sig), "%lld:%lld:%d:%.0f|%.4f %.4f %.4f %.4f %d|%lld",
                   (long long) GetSrcStart(), (long long) GetSrcEnd(), mChannels, mSampleRate,
                   readNorm (kParamClean), readNorm (kParamVoice),
                   readNorm (kParamMinFill), readNorm (kParamFlatness),
                   readNorm (kParamExperim) > 0.5 ? 1 : 0, manHash);
    if (mAnalysisSig != sig)
    {
        if (! analyze (inAudioIns, inAudioInCount, windowSize)) return;
        mAnalysisSig = sig;
        mRenderSig.clear(); // model changed -> fill must be rebuilt
    }

    // Only RENDER-affecting params here. Hiss Filter and Output gain are applied live in RenderAudio
    // (like the ARA processBlock), so changing them must NOT force a re-render.
    char rsig[192];
    std::snprintf (rsig, sizeof (rsig), "%.4f %.4f %.4f %d %.0f|%d %.4f %d",
                   readNorm (kParamChunk), readNorm (kParamXfade), readNorm (kParamSmooth),
                   readNorm (kParamEnhance) > 0.5 ? 1 : 0, readReal (kParamSeed, 1.0, 100.0),
                   readNorm (kParamNormOn) > 0.5 ? 1 : 0, readNorm (kParamNormTarget),
                   readNorm (kParamNormLufs) > 0.5 ? 1 : 0);
    if (mModel != nullptr && mRenderSig != rsig)
    {
        renderFill();
        if (mFillLen > 0) mRenderSig = rsig;
    }
}

AAX_Result ToneFillAS_HostProcessor::RenderAudio (const float* const inAudioIns[], int32_t inAudioInCount,
                                                  float* const inAudioOuts[], int32_t inAudioOutCount,
                                                  int32_t* ioWindowSize)
{
    if (ioWindowSize == nullptr) return AAX_ERROR_NULL_OBJECT;
    const int n  = *ioWindowSize;
    const int ci = juce::jmin (inAudioInCount, inAudioOutCount);

    // (Re)build lazily; cached across windows and passes. During Preview a render-knob tweak
    // re-renders on the next window, so Preview follows the knobs like the plugin does.
    ensureFill (inAudioIns, inAudioInCount, n);

    // Manual Output gain is live only when Normalize is off (normalize is baked into the fill).
    const bool  normOn = readNorm (kParamNormOn) > 0.5;
    const float gain   = normOn ? 1.0f
                                : std::pow (10.0f, (float) readReal (kParamGain, -24.0, 24.0) / 20.0f);

    for (int c = 0; c < ci; ++c)
    {
        if (! inAudioOuts[c]) continue;
        float* o = inAudioOuts[c];
        if (mFillLen > 0)
        {
            const auto& f = mFill[(std::size_t) juce::jmin (c, (int) mFill.size() - 1)];
            for (int i = 0; i < n; ++i) o[i] = f[(std::size_t) ((mGenPos + i) % mFillLen)] * gain;
        }
        else
            std::memset (o, 0, sizeof (float) * (std::size_t) n);
    }
    if (mFillLen > 0) mGenPos += n;

    // Hiss filter applied LIVE, stateful across the pass (identical to the ARA processBlock): only
    // when Enhance AND Hiss Filter are on. Coefficients rebuilt only when the knobs move.
    if (mFillLen > 0 && readNorm (kParamEnhance) > 0.5 && readNorm (kParamHissOn) > 0.5)
    {
        if ((int) mHiss.size() < ci) mHiss.resize ((std::size_t) ci);
        const float freq = (float) readReal (kParamHissFreq, 3000.0, 15000.0);
        const float q    = (float) readReal (kParamHissQ, 0.3, 2.0);
        if (std::abs (freq - mHissLastFreq) > 0.5f || std::abs (q - mHissLastQ) > 1.0e-3f)
        {
            const auto co = juce::IIRCoefficients::makeLowPass (mSampleRate, freq, q);
            for (auto& f : mHiss) f.setCoefficients (co);
            mHissLastFreq = freq; mHissLastQ = q;
        }
        for (int c = 0; c < ci; ++c)
            if (inAudioOuts[c]) mHiss[(std::size_t) c].processSamples (inAudioOuts[c], n);
    }
    return AAX_SUCCESS;
}

AAX_Result ToneFillAS_HostProcessor::PostRender()
{
    return AAX_SUCCESS;
}
