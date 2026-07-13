#include "ToneFillAS_HostProcessor.h"
#include "ToneFillAS_Defs.h"

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

    juce::AudioBuffer<float> buf (ch, (int) len);
    for (int c = 0; c < ch; ++c) buf.copyFrom (c, 0, accum[(std::size_t) c].data(), (int) len);

    tonefill::core::DiagnosticsLogger diag;
    tonefill::engine::analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    // Mirror of the JUCE FillWorker's AnalysisContext (Phase A parity).
    tonefill::engine::analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (buf);
    ctx.rightContext.makeCopyOf (buf);
    ctx.analysisSampleRate = mSampleRate;
    ctx.numChannels = ch;
    ctx.leftEnabled = true;
    ctx.useManualSelection = false;
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
           + " chunks=" + juce::String ((int) mModel->cleanRanges.size()));
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

    // Hiss filter (Enhance only): baked into the offline render (the plugin applies it live).
    if (s.paulStretch && readNorm (kParamHissOn) > 0.5)
    {
        const double freq = readReal (kParamHissFreq, 3000.0, 15000.0);
        const double q    = readReal (kParamHissQ, 0.3, 2.0);
        for (auto& c : chans)
        {
            juce::IIRFilter f;
            f.setCoefficients (juce::IIRCoefficients::makeLowPass (mSampleRate, freq, q));
            f.processSamples (c.data(), (int) c.size());
        }
        asLog ("hiss: baked lp " + juce::String (freq, 0) + " Hz q=" + juce::String (q, 2));
    }

    // Normalize: measured on what will ACTUALLY be rendered (S7.3). Selection shorter than the
    // loop -> measure that prefix; longer -> the loop repeats, so the loop measure is the output
    // measure (gated integration of a repeating signal converges to the loop's value).
    if (readNorm (kParamNormOn) > 0.5)
    {
        const bool lufs = readNorm (kParamNormLufs) > 0.5;
        const double target = readReal (kParamNormTarget, -60.0, 0.0);
        const long long selLen = (long long) (GetSrcEnd() - GetSrcStart());
        const long long measLen = juce::jmin<long long> ((long long) chans[0].size(),
                                                          juce::jmax<long long> (1, selLen));
        std::vector<std::vector<float>> meas (chans.size());
        for (std::size_t c = 0; c < chans.size(); ++c)
            meas[c].assign (chans[c].begin(), chans[c].begin() + (std::size_t) measLen);

        const float measured = lufs ? tonefill::dsp::integratedLufs (meas, mSampleRate)
                                    : tonefill::dsp::peakDbfs (meas);
        if (measured > -119.0f)
        {
            float gainDb = (float) target - measured;
            if (lufs) // keep true peak under -1 dBFS, like the plugin
            {
                const float peakDb = tonefill::dsp::peakDbfs (meas);
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
    char sig[192];
    std::snprintf (sig, sizeof (sig), "%lld:%lld:%d:%.0f|%.4f %.4f %.4f %.4f %d",
                   (long long) GetSrcStart(), (long long) GetSrcEnd(), mChannels, mSampleRate,
                   readNorm (kParamClean), readNorm (kParamVoice),
                   readNorm (kParamMinFill), readNorm (kParamFlatness),
                   readNorm (kParamExperim) > 0.5 ? 1 : 0);
    if (mAnalysisSig != sig)
    {
        if (! analyze (inAudioIns, inAudioInCount, windowSize)) return;
        mAnalysisSig = sig;
        mRenderSig.clear(); // model changed -> fill must be rebuilt
    }

    char rsig[192];
    std::snprintf (rsig, sizeof (rsig), "%.4f %.4f %.4f %d %.0f|%d %.4f %.4f|%d %.4f %d",
                   readNorm (kParamChunk), readNorm (kParamXfade), readNorm (kParamSmooth),
                   readNorm (kParamEnhance) > 0.5 ? 1 : 0, readReal (kParamSeed, 1.0, 100.0),
                   readNorm (kParamHissOn) > 0.5 ? 1 : 0, readNorm (kParamHissFreq), readNorm (kParamHissQ),
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
    return AAX_SUCCESS;
}

AAX_Result ToneFillAS_HostProcessor::PostRender()
{
    return AAX_SUCCESS;
}
