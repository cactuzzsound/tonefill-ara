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
#include "dsp/SpectralBands.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
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

// Immutable snapshot of the parameters (captured on the render thread, consumed by the worker so it
// never touches AAX interfaces off-thread).
struct RenderParams
{
    double sr = 48000.0;
    bool  manual = false, experimental = false, enhance = false, normOn = false, normLufs = true;
    bool  spectral = false, spectralAdvanced = false;
    int   bands = 7;
    std::vector<float> spectralEdges; // advanced: explicit band edges (Hz) from the editor
    float clean = 0, voice = 0, flatness = 0, minFill = 2, chunk = 0, xfade = 0, smooth = 0;
    double seed = 1, normTarget = -16.0;
    std::vector<std::pair<int, int>> manualRanges;
    std::string aSig, rSig;
};

struct FillResult
{
    std::vector<std::vector<float>> fill;
    long long   len = 0;
    std::string rSig;
};

// Analyse (pure engine, no AAX). Publishes the waveform + diagnostics to shared. Returns the model.
tonefill::engine::model::AmbienceModelPtr
analyseImpl (const RenderParams& p, const juce::AudioBuffer<float>& raw, ASShared* sh)
{
    const int len = raw.getNumSamples();
    const bool useManual = p.manual && ! p.manualRanges.empty();
    const juce::AudioBuffer<float> learnInput = useManual ? buildManual (raw, p.manualRanges, p.sr) : raw;

    tonefill::core::DiagnosticsLogger diag;
    tonefill::engine::analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    tonefill::engine::analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (learnInput);
    ctx.rightContext.makeCopyOf (learnInput);
    ctx.analysisSampleRate = p.sr;
    ctx.numChannels = raw.getNumChannels();
    ctx.leftEnabled = true;
    ctx.useManualSelection = useManual;
    ctx.cleanThreshold = p.clean;
    ctx.speechReject   = p.voice;
    ctx.flatness       = p.flatness;
    ctx.minFillSeconds = p.minFill;
    ctx.statisticalSelection = p.experimental;
    ctx.sourceContentHash = (std::uint64_t) len;

    auto rr = session.run (ctx, cancel);
    if (! rr.ok()) { asLog ("analyse: FAILED"); return nullptr; }
    auto model = rr.value();
    asLog ("analyse: OK learnSec=" + juce::String (model->learnMaterialSeconds)
           + " avail=" + juce::String (model->availableCleanSeconds)
           + " chunks=" + juce::String ((int) model->cleanRanges.size()) + " manual=" + juce::String ((int) useManual));

    if (sh != nullptr && len > 0)
    {
        const int bins = 600;
        const int per = juce::jmax (1, len / bins);
        std::vector<float> peak ((std::size_t) bins, 0.0f);
        std::vector<char>  clean ((std::size_t) bins, 0);
        const float* s0 = raw.getReadPointer (0);
        for (int b = 0; b < bins; ++b)
        {
            float pk = 0.0f; const int from = b * per, to = juce::jmin (from + per, len);
            for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::fabs (s0[i]));
            peak[(std::size_t) b] = pk;
        }
        const auto& overlay = useManual ? p.manualRanges : model->cleanRanges;
        for (int b = 0; b < bins; ++b)
        {
            const int bs = b * per, be = bs + per; bool cl = false;
            for (const auto& r : overlay) if (r.first < be && r.second > bs) { cl = true; break; }
            clean[(std::size_t) b] = cl ? 1 : 0;
        }
        const float targetRms = model->noisePerChannel.empty() ? 0.0f : model->noisePerChannel[0].targetRms;
        const juce::SpinLock::ScopedLockType l (sh->lock);
        sh->peak = std::move (peak); sh->clean = std::move (clean);
        sh->sourceSamples = len; sh->sampleRate = p.sr;
        sh->usedSec = model->learnMaterialSeconds; sh->availSec = model->availableCleanSeconds;
        sh->seamDb = model->joinRoughnessDb; sh->chunks = (int) model->cleanRanges.size();
        sh->levelDb = targetRms > 0.0f ? 20.0f * std::log10 (targetRms) : -120.0f;
        sh->ready = true;
    }
    return model;
}

// Render (pure engine, no AAX). Loop fold + normalize on the whole loop, matching the ARA path.
std::shared_ptr<FillResult> renderImpl (const tonefill::engine::model::AmbienceModelPtr& model, const RenderParams& p)
{
    if (model == nullptr) return nullptr;

    tonefill::engine::model::RenderSettings s;
    s.mode        = tonefill::engine::model::Mode::Ambience;
    s.paulStretch = p.enhance;
    s.fragmentMs  = 700.0f; // Chunk Size removed (grainCloud caps to srcLen/4; join machinery hides it)
    s.blendFrac   = 0.05f + p.xfade * 0.45f;
    s.randomness  = p.smooth;
    s.seed        = (std::uint64_t) juce::jmax (1.0, p.seed);

    const double availSec = (double) model->learnMaterialSeconds;
    const double loopSec  = juce::jlimit (15.0, 120.0, availSec * 4.0);
    const int loopLen = (int) (loopSec * p.sr);
    const int xf      = (int) (0.25 * p.sr);
    s.targetSampleRate      = p.sr;
    s.targetChannels        = juce::jmax (1, model->numChannels);
    s.targetDurationSamples = (long long) (loopLen + xf);

    std::atomic<bool> cancel { false };
    tonefill::engine::synthesis::AmbienceRenderer renderer;
    auto out = renderer.render (*model, s, cancel);
    if (! out.ok()) { asLog ("render: FAILED"); return nullptr; }
    auto chans = std::move (out.value().channels);

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
    if (chans.empty() || chans[0].empty()) return nullptr;

    if (p.normOn)
    {
        const bool lufs = p.normLufs;
        const float measured = lufs ? tonefill::dsp::integratedLufs (chans, p.sr)
                                    : tonefill::dsp::peakDbfs (chans);
        if (measured > -119.0f)
        {
            float gainDb = (float) p.normTarget - measured;
            if (lufs) { const float peakDb = tonefill::dsp::peakDbfs (chans); const float np = peakDb + gainDb; if (np > -1.0f) gainDb -= (np + 1.0f); }
            const float g = std::pow (10.0f, gainDb / 20.0f);
            if (std::fabs (g - 1.0f) > 1.0e-4f) for (auto& c : chans) for (auto& v : c) v *= g;
        }
    }

    auto fr = std::make_shared<FillResult>();
    fr->fill = std::move (chans);
    fr->len  = (long long) fr->fill[0].size();
    fr->rSig = p.rSig;
    asLog ("fill: len=" + juce::String (fr->len) + " ch=" + juce::String ((int) fr->fill.size()));
    return fr;
}

// Spectral Mosaic (AAX): split the source into power-complementary bands, select + synthesize each
// band from its own clean material, gain-match each band to the clean room tone's level in that band,
// and sum. Mirrors the ARA path. Reuses analyseImpl per band (sh=nullptr so it never touches the UI).
std::shared_ptr<FillResult>
renderSpectralImpl (const tonefill::engine::model::AmbienceModelPtr& model,
                    const juce::AudioBuffer<float>& raw, const RenderParams& p)
{
    if (model == nullptr) return nullptr;

    const double availSec = (double) model->learnMaterialSeconds;
    const double loopSec  = juce::jlimit (15.0, 120.0, availSec * 4.0);
    const int loopLen = (int) (loopSec * p.sr);
    const int xf      = (int) (0.25 * p.sr);
    const int outN    = loopLen + xf;
    const int srcN    = raw.getNumSamples();
    const int effCh   = juce::jmax (1, model->numChannels);
    if (srcN <= 0 || outN <= 0) return nullptr;

    // Band edges: geometric across 150..8000 Hz from the band count.
    const int nBands = juce::jlimit (3, 12, p.bands);
    const int nEdges = nBands - 1;
    std::vector<double> edges;
    // Advanced with explicit editor edges: use them (sorted, clamped); else a geometric 150..8000 spread.
    if (p.spectralAdvanced && (int) p.spectralEdges.size() == nEdges)
    {
        auto ue = p.spectralEdges; std::sort (ue.begin(), ue.end());
        for (float f : ue) edges.push_back (juce::jlimit (20.0, p.sr * 0.49, (double) f));
    }
    if ((int) edges.size() != nEdges)
    {
        edges.clear();
        const double lo = 150.0, hi = 8000.0;
        for (int e = 0; e < nEdges; ++e)
        { const double t = nEdges > 1 ? (double) e / (double) (nEdges - 1) : 0.0;
          edges.push_back (lo * std::pow (hi / lo, t)); }
    }

    // Mono-collapse the source and split it.
    std::vector<float> mono ((std::size_t) srcN, 0.0f);
    const int lc = juce::jmax (1, raw.getNumChannels());
    for (int c = 0; c < lc; ++c)
    { const float* s = raw.getReadPointer (c); for (int i = 0; i < srcN; ++i) mono[(std::size_t) i] += s[i]; }
    if (lc > 1) for (auto& v : mono) v /= (float) lc;
    auto bands = tonefill::dsp::splitBands (mono.data(), srcN, p.sr, edges);

    // Target timbre = the clean room tone's per-band level (measured full-spectrum on the material the
    // broadband analysis selected), NOT the whole source's band energy (dialogue-weighted in the mids).
    std::vector<float> targetBandRms ((std::size_t) nBands, 0.0f);
    bool haveCleanRef = false;
    { const auto& cap = model->cleanAudioPerChannel;
      if (! cap.empty() && ! cap[0].empty())
      { const int cn = (int) cap[0].size();
        std::vector<float> cm ((std::size_t) cn, 0.0f);
        for (const auto& cc : cap) for (int i = 0; i < juce::jmin (cn, (int) cc.size()); ++i) cm[(std::size_t) i] += cc[(std::size_t) i];
        if (cap.size() > 1) for (auto& v : cm) v /= (float) cap.size();
        auto cb = tonefill::dsp::splitBands (cm.data(), cn, p.sr, edges);
        for (std::size_t bi = 0; bi < cb.size() && bi < targetBandRms.size(); ++bi)
        { double e = 0.0; for (float v : cb[bi]) e += (double) v * v;
          targetBandRms[bi] = (float) std::sqrt (e / juce::jmax (1, (int) cb[bi].size())); }
        haveCleanRef = true; } }

    std::atomic<bool> cancel { false };
    tonefill::engine::synthesis::AmbienceRenderer renderer;
    std::vector<float> accMono ((std::size_t) outN, 0.0f);

    for (std::size_t bi = 0; bi < bands.size(); ++bi)
    {
        const auto& band = bands[bi];
        float target = (bi < targetBandRms.size()) ? targetBandRms[bi] : 0.0f;
        if (! haveCleanRef)
        { double se = 0.0; for (float v : band) se += (double) v * v;
          target = (float) std::sqrt (se / juce::jmax (1, srcN)); }
        if (target < 1.0e-7f) continue;

        juce::AudioBuffer<float> bandBuf (1, srcN);
        std::copy (band.begin(), band.end(), bandBuf.getWritePointer (0));
        auto bandModel = analyseImpl (p, bandBuf, nullptr); // no UI publish
        if (bandModel == nullptr) continue;

        tonefill::engine::model::RenderSettings s;
        s.mode        = tonefill::engine::model::Mode::Ambience;
        s.paulStretch = p.enhance;
        s.fragmentMs  = 700.0f;
        s.blendFrac   = 0.05f + p.xfade * 0.45f;
        s.randomness  = p.smooth;
        s.seed        = ((std::uint64_t) juce::jmax (1.0, p.seed)) ^ (0x9E3779B97F4A7C15ULL * (std::uint64_t) (bi + 1));
        s.targetSampleRate      = p.sr;
        s.targetChannels        = 1;
        s.targetDurationSamples = outN;
        auto out = renderer.render (*bandModel, s, cancel);
        if (! out.ok() || out.value().channels.empty()) continue;
        const auto& bc = out.value().channels[0];

        double oe = 0.0; for (float v : bc) oe += (double) v * v;
        const float outRms = (float) std::sqrt (oe / juce::jmax (1, (int) bc.size()));
        const float g = outRms > 1.0e-9f ? target / outRms : 0.0f;
        const int m2 = juce::jmin (outN, (int) bc.size());
        for (int i = 0; i < m2; ++i) accMono[(std::size_t) i] += bc[(std::size_t) i] * g;
    }

    // Fan to channels, fold the loop seamless, normalize -- same tail handling as renderImpl.
    std::vector<std::vector<float>> chans ((std::size_t) effCh, accMono);
    for (auto& c : chans)
        if ((int) c.size() >= loopLen + xf)
        {
            for (int i = 0; i < xf; ++i)
            { const float gg = (float) i / (float) (xf - 1) * 1.5707963f;
              c[(std::size_t) i] = c[(std::size_t) (loopLen + i)] * std::cos (gg) + c[(std::size_t) i] * std::sin (gg); }
            c.resize ((std::size_t) loopLen);
        }
    if (chans.empty() || chans[0].empty()) return nullptr;

    if (p.normOn)
    {
        const bool lufs = p.normLufs;
        const float measured = lufs ? tonefill::dsp::integratedLufs (chans, p.sr) : tonefill::dsp::peakDbfs (chans);
        if (measured > -119.0f)
        {
            float gainDb = (float) p.normTarget - measured;
            if (lufs) { const float peakDb = tonefill::dsp::peakDbfs (chans); const float np = peakDb + gainDb; if (np > -1.0f) gainDb -= (np + 1.0f); }
            const float g = std::pow (10.0f, gainDb / 20.0f);
            if (std::fabs (g - 1.0f) > 1.0e-4f) for (auto& c : chans) for (auto& v : c) v *= g;
        }
    }

    auto fr = std::make_shared<FillResult>();
    fr->fill = std::move (chans);
    fr->len  = (long long) fr->fill[0].size();
    fr->rSig = p.rSig;
    asLog ("spectral fill: bands=" + juce::String (nBands) + " len=" + juce::String (fr->len));
    return fr;
}
} // namespace

//==============================================================================
// Background worker: analyses + renders off the render thread, publishing the fill.
class ToneFillAS_HostProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (ASShared* sh) : juce::Thread ("ToneFillAS-render"), mShared (sh) {}
    ~Worker() override { signalThreadShouldExit(); mEvent.signal(); stopThread (5000); }

    void submit (RenderParams p, std::shared_ptr<const juce::AudioBuffer<float>> raw)
    {
        { const juce::ScopedLock l (mJobLock); mPending = std::move (p); mPendingRaw = std::move (raw); mHasJob = true; }
        mEvent.signal();
    }

    std::shared_ptr<const FillResult> getFill()
    {
        const juce::ScopedLock l (mPubLock);
        return mFill;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            mEvent.wait (-1);
            if (threadShouldExit()) break;

            RenderParams job;
            std::shared_ptr<const juce::AudioBuffer<float>> raw;
            { const juce::ScopedLock l (mJobLock); if (! mHasJob) continue; job = mPending; raw = mPendingRaw; mHasJob = false; }
            if (raw == nullptr) continue;

            if ((job.aSig != mDoneASig || mModel == nullptr || job.rSig != mDoneRSig) && mShared != nullptr)
                mShared->computing.store (true);

            if (job.aSig != mDoneASig || mModel == nullptr)
            {
                mModel = analyseImpl (job, *raw, mShared);
                mDoneASig = job.aSig;
                mDoneRSig.clear();
            }
            if (mModel != nullptr && job.rSig != mDoneRSig)
            {
                auto fr = job.spectral ? renderSpectralImpl (mModel, *raw, job) : renderImpl (mModel, job);
                if (fr)
                {
                    const juce::ScopedLock l (mPubLock);
                    mFill = fr;
                    mDoneRSig = job.rSig;
                }
            }
            // Done with this job (no newer one queued): the GUI can drop the "recomputing" hint.
            { const juce::ScopedLock l (mJobLock); if (! mHasJob && mShared != nullptr) mShared->computing.store (false); }
        }
    }

private:
    ASShared* mShared;
    juce::WaitableEvent mEvent;

    juce::CriticalSection mJobLock;
    RenderParams mPending;
    std::shared_ptr<const juce::AudioBuffer<float>> mPendingRaw;
    bool mHasJob = false;

    juce::CriticalSection mPubLock;
    std::shared_ptr<const FillResult> mFill;

    tonefill::engine::model::AmbienceModelPtr mModel; // worker-local cache
    std::string mDoneASig, mDoneRSig;
};

//==============================================================================
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
    mHiss.clear(); mHissLastFreq = -1.0f; mHissLastQ = -1.0f;

    if (mWorker == nullptr)
    {
        ASShared* sh = nullptr;
        if (auto* p = dynamic_cast<ToneFillAS_Parameters*> (GetEffectParameters())) sh = &p->shared();
        mWorker = std::make_unique<Worker> (sh);
        mWorker->startThread();
    }
    asLog ("PreRender: ch=" + juce::String (mChannels) + " sr=" + juce::String (mSampleRate));
    return AAX_SUCCESS;
}

void ToneFillAS_HostProcessor::ensureRawSource (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize)
{
    const int ch = juce::jmax (1, inAudioInCount);
    const int64_t s0 = GetSrcStart(), s1 = GetSrcEnd();
    char sig[96]; std::snprintf (sig, sizeof (sig), "%lld:%lld:%d:%.0f", (long long) s0, (long long) s1, ch, mSampleRate);
    if (mRaw != nullptr && mRawSig == sig) return; // source range unchanged

    const long long cap = (long long) (900.0 * mSampleRate); // 15 min
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
    mRawSig = sig;
    if (len < 4096) { mRaw = nullptr; asLog ("ensureRawSource: too little source"); return; }

    juce::AudioBuffer<float> full (ch, (int) len);
    for (int c = 0; c < ch; ++c) full.copyFrom (c, 0, accum[(std::size_t) c].data(), (int) len);

    // Mono detection: near-identical L/R -> one correlated channel (mirror of the ARA path).
    int effCh = ch;
    if (ch >= 2)
    {
        double diff = 0.0, ref = 0.0;
        const float* a = full.getReadPointer (0); const float* b = full.getReadPointer (1);
        for (int i = 0; i < (int) len; ++i) { const double d = a[i] - b[i]; diff += d * d; ref += (double) a[i] * a[i]; }
        if (ref > 0.0 && diff / ref < 1.0e-4) effCh = 1;
    }
    auto buf = std::make_shared<juce::AudioBuffer<float>> (effCh, (int) len);
    for (int c = 0; c < effCh; ++c) buf->copyFrom (c, 0, full, c, 0, (int) len);
    mRaw = buf;
    asLog ("ensureRawSource: len=" + juce::String (len) + " effCh=" + juce::String (effCh));
}

AAX_Result ToneFillAS_HostProcessor::RenderAudio (const float* const inAudioIns[], int32_t inAudioInCount,
                                                  float* const inAudioOuts[], int32_t inAudioOutCount,
                                                  int32_t* ioWindowSize)
{
    if (ioWindowSize == nullptr) return AAX_ERROR_NULL_OBJECT;
    const int n  = *ioWindowSize;
    const int ci = juce::jmin (inAudioInCount, inAudioOutCount);

    ensureRawSource (inAudioIns, inAudioInCount, n);

    ASShared* sh = nullptr;
    if (auto* p = dynamic_cast<ToneFillAS_Parameters*> (GetEffectParameters())) sh = &p->shared();

    // Snapshot the parameters (+ manual ranges) for the worker.
    RenderParams pr;
    pr.sr = mSampleRate;
    pr.manual       = readNorm (kParamManual)  > 0.5;
    pr.experimental = readNorm (kParamExperim) > 0.5;
    pr.enhance      = readNorm (kParamEnhance)  > 0.5;
    pr.normOn       = readNorm (kParamNormOn)   > 0.5;
    pr.normLufs     = readNorm (kParamNormLufs) > 0.5;
    pr.clean    = (float) readNorm (kParamClean);
    pr.voice    = (float) readNorm (kParamVoice);
    pr.flatness = (float) readNorm (kParamFlatness);
    pr.minFill  = (float) readReal (kParamMinFill, 0.2, 5.0);
    pr.chunk    = (float) readNorm (kParamChunk);
    pr.xfade    = (float) readNorm (kParamXfade);
    pr.smooth   = (float) readNorm (kParamSmooth);
    pr.seed     = readReal (kParamSeed, 1.0, 100.0);
    pr.normTarget = readReal (kParamNormTarget, -60.0, 0.0);
    pr.spectral = readNorm (kParamSpectral) > 0.5;
    pr.bands    = (int) std::lround (readReal (kParamBands, 3.0, 12.0));
    pr.spectralAdvanced = readNorm (kParamSpectralAdv) > 0.5;

    // Publish the analysed source to the GUI's spectrogram once per new source (build outside the lock).
    std::shared_ptr<std::vector<std::vector<float>>> newPreview;
    if (mRaw != nullptr && mRawSig != mPreviewSig)
    {
        newPreview = std::make_shared<std::vector<std::vector<float>>> ((std::size_t) mRaw->getNumChannels());
        for (int c = 0; c < mRaw->getNumChannels(); ++c)
            (*newPreview)[(std::size_t) c].assign (mRaw->getReadPointer (c), mRaw->getReadPointer (c) + mRaw->getNumSamples());
        mPreviewSig = mRawSig;
    }
    if (sh != nullptr)
    {
        const juce::SpinLock::ScopedLockType l (sh->lock);
        pr.manualRanges  = sh->manualRanges;
        pr.spectralEdges = sh->spectralEdges;
        if (newPreview != nullptr) { sh->sourcePreview = newPreview; sh->sourceSr = mSampleRate; sh->sourceGen++; }
    }

    long long manHash = pr.manual ? 1 : 0;
    for (const auto& r : pr.manualRanges) manHash = manHash * 1000003LL + r.first * 31 + r.second;
    long long edgeHash = pr.spectralAdvanced ? 1 : 0;
    for (float f : pr.spectralEdges) edgeHash = edgeHash * 1000003LL + (long long) std::lround (f);
    char asig[256], rsig[192];
    std::snprintf (asig, sizeof (asig), "%s|%.4f %.4f %.4f %.4f %d|%lld", mRawSig.c_str(),
                   pr.clean, pr.voice, pr.flatness, pr.minFill, pr.experimental ? 1 : 0, manHash);
    std::snprintf (rsig, sizeof (rsig), "%d %.4f %.4f %.4f %.0f|%d %.4f %d|%d %d|%d %lld",
                   pr.enhance ? 1 : 0, pr.chunk, pr.xfade, pr.smooth, pr.seed,
                   pr.normOn ? 1 : 0, pr.normTarget, pr.normLufs ? 1 : 0,
                   pr.spectral ? 1 : 0, pr.bands, pr.spectralAdvanced ? 1 : 0, edgeHash);
    pr.aSig = asig; pr.rSig = rsig;

    // Submit to the worker when anything relevant changed.
    const std::string submit = pr.aSig + "#" + pr.rSig;
    if (mWorker != nullptr && mRaw != nullptr && submit != mLastSubmitSig)
    {
        if (sh != nullptr) sh->computing.store (true);
        mWorker->submit (pr, mRaw);
        mLastSubmitSig = submit;
    }

    // Bypass A/B: play the analysed source (looped) instead of the room tone, so you can compare
    // them. Uses mRaw rather than the input buffer, which AudioSuite does not reliably feed here.
    if (readNorm (kParamBypass) > 0.5)
    {
        const long long slen = (mRaw != nullptr) ? mRaw->getNumSamples() : 0;
        for (int c = 0; c < ci; ++c)
        {
            if (! inAudioOuts[c]) continue;
            if (slen > 0)
            {
                const float* s = mRaw->getReadPointer (juce::jmin (c, mRaw->getNumChannels() - 1));
                for (int i = 0; i < n; ++i) inAudioOuts[c][i] = s[(std::size_t) ((mGenPos + i) % slen)];
            }
            else std::memset (inAudioOuts[c], 0, sizeof (float) * (std::size_t) n);
        }
        if (slen > 0) mGenPos += n;
        return AAX_SUCCESS;
    }

    auto fr = (mWorker != nullptr) ? mWorker->getFill() : nullptr;

    // Offline Render: block until the worker has produced THIS render's fill (write the right file).
    // Preview: don't block - play the last-good fill; the worker swaps the new one in when ready.
    const bool previewing = (sh != nullptr) && sh->previewing.load();
    if (! previewing && mRaw != nullptr && mWorker != nullptr)
    {
        for (int i = 0; i < 1200 && (fr == nullptr || fr->rSig != pr.rSig); ++i)
        { juce::Thread::sleep (25); fr = mWorker->getFill(); }
    }

    const bool  normOn = pr.normOn;
    const float gain   = normOn ? 1.0f : std::pow (10.0f, (float) readReal (kParamGain, -24.0, 24.0) / 20.0f);
    const long long fillLen = fr != nullptr ? fr->len : 0;

    for (int c = 0; c < ci; ++c)
    {
        if (! inAudioOuts[c]) continue;
        float* o = inAudioOuts[c];
        if (fillLen > 0)
        {
            const auto& f = fr->fill[(std::size_t) juce::jmin (c, (int) fr->fill.size() - 1)];
            for (int i = 0; i < n; ++i) o[i] = f[(std::size_t) ((mGenPos + i) % fillLen)] * gain;
        }
        else
            std::memset (o, 0, sizeof (float) * (std::size_t) n);
    }
    if (fillLen > 0) mGenPos += n;

    // Hiss filter applied LIVE, stateful across the pass (identical to the ARA processBlock).
    if (fillLen > 0 && pr.enhance && readNorm (kParamHissOn) > 0.5)
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
