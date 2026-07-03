#include "ToneFillAS_HostProcessor.h"
#include "ToneFillAS_Defs.h"

#include "AAX_IController.h"
#include "AAX_IEffectParameters.h"

#include "engine/analysis/AnalysisSession.h"
#include "engine/analysis/AnalysisContext.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"
#include "core/DiagnosticsLogger.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
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
    mAnalyzed   = false;
    mGenPos     = 0;
    mFill.clear();
    mFillLen    = 0;
    asLog ("PreRender: ch=" + juce::String (mChannels) + " sr=" + juce::String (mSampleRate));
    return AAX_SUCCESS;
}

void ToneFillAS_HostProcessor::analyzeAndBuild (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize)
{
    const int ch = juce::jmax (1, inAudioInCount);
    const int64_t s0 = GetSrcStart(), s1 = GetSrcEnd();
    const long long cap = (long long) (900.0 * mSampleRate); // 15 min

    // Random-access read of the WHOLE source range via GetAudio (fills inAudioIns each chunk).
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
    if (len < 4096) { asLog ("analyze: too little source"); return; }

    juce::AudioBuffer<float> buf (ch, (int) len);
    for (int c = 0; c < ch; ++c) buf.copyFrom (c, 0, accum[(std::size_t) c].data(), (int) len);

    tonefill::core::DiagnosticsLogger diag;
    tonefill::engine::analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    tonefill::engine::analysis::AnalysisContext ctx;
    ctx.leftContext.makeCopyOf (buf);
    ctx.rightContext.makeCopyOf (buf);
    ctx.analysisSampleRate = mSampleRate;
    ctx.numChannels = ch;
    ctx.leftEnabled = true;
    ctx.useManualSelection = false;
    ctx.cleanThreshold = (float) readNorm (kParamClean);
    ctx.speechReject   = (float) readNorm (kParamVoice);
    ctx.sourceContentHash = (std::uint64_t) len;

    auto rr = session.run (ctx, cancel);
    if (! rr.ok()) { asLog ("analyze: FAILED"); return; }
    auto model = rr.value();
    asLog ("analyze: OK learnSec=" + juce::String (model->learnMaterialSeconds)
           + " cleanLen=" + juce::String (model->cleanAudioPerChannel.empty() ? 0 : (int) model->cleanAudioPerChannel[0].size()));

    tonefill::engine::model::RenderSettings s;
    const bool complex = readNorm (kParamMode) > 0.5;
    s.mode       = complex ? tonefill::engine::model::Mode::Complex : tonefill::engine::model::Mode::Ambience;
    s.movement   = (float) readNorm (kParamMovement);
    s.fragmentMs = 1000.0f;
    s.blendFrac  = 0.5f;
    s.randomness = (float) readNorm (kParamVariation);
    s.seed       = 1;
    const double loopSec = 15.0 + (double) s.randomness * 45.0;
    const int loopLen = (int) (loopSec * mSampleRate);
    const int xf = (int) (0.25 * mSampleRate);
    s.targetSampleRate = mSampleRate;
    s.targetChannels = juce::jmax (1, model->numChannels);
    s.targetDurationSamples = (long long) (loopLen + xf);

    tonefill::engine::synthesis::AmbienceRenderer renderer;
    auto out = renderer.render (*model, s, cancel);
    if (! out.ok()) { asLog ("render: FAILED"); return; }
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
    if (chans.empty() || chans[0].empty()) return;
    mFill = std::move (chans);
    mFillLen = (long long) mFill[0].size();
    asLog ("fill: len=" + juce::String (mFillLen) + " ch=" + juce::String ((int) mFill.size()));
}

AAX_Result ToneFillAS_HostProcessor::RenderAudio (const float* const inAudioIns[], int32_t inAudioInCount,
                                                  float* const inAudioOuts[], int32_t inAudioOutCount,
                                                  int32_t* ioWindowSize)
{
    if (ioWindowSize == nullptr) return AAX_ERROR_NULL_OBJECT;
    const int n  = *ioWindowSize;
    const int ci = juce::jmin (inAudioInCount, inAudioOutCount);

    if (! mAnalyzed) { analyzeAndBuild (inAudioIns, inAudioInCount, n); mAnalyzed = true; }

    const double gainDb = -24.0 + readNorm (kParamGain) * 48.0;
    const float  gain   = std::pow (10.0f, (float) gainDb / 20.0f);

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
