#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/ParameterState.h"

#include "core/DiagnosticsLogger.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/analysis/AnalysisContext.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#if TONEFILL_ARA_AVAILABLE
 #include "plugin/ara/PlaybackRenderer.h"
#endif

namespace tonefill::plugin
{
// Process-global "last learned model" for the non-ARA Learn/Generate workflow. Pro Tools
// AudioSuite uses SEPARATE plugin instances for the Learn (analyze/preview) pass and the Render
// pass, so a per-instance member would be empty at Render time (-> silence). Sharing it here lets
// the Render instance pick up what the Learn instance captured.
struct GlobalLearned
{
    std::mutex m;
    engine::model::AmbienceModelPtr model;
    static GlobalLearned& get() { static GlobalLearned g; return g; }
};
static void setGlobalLearned (engine::model::AmbienceModelPtr m)
{
    auto& g = GlobalLearned::get();
    std::lock_guard<std::mutex> l (g.m);
    g.model = std::move (m);
}
static engine::model::AmbienceModelPtr getGlobalLearned()
{
    auto& g = GlobalLearned::get();
    std::lock_guard<std::mutex> l (g.m);
    return g.model;
}

// Temporary bring-up trace (always-on path, independent of ARA) -> ~/tonefill_ara.log.
static void pluginLog (const juce::String& msg)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("tonefill_ara.log")
        .appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  [proc] " + msg + "\n");
}

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params_ (*this)
{
    pluginLog ("PluginProcessor ctor (ARA_AVAILABLE=" + juce::String ((int) TONEFILL_ARA_AVAILABLE) + ")");
}

PluginProcessor::~PluginProcessor() = default;

#if TONEFILL_ARA_AVAILABLE
void PluginProcessor::didBindToARA() noexcept
{
    juce::AudioProcessorARAExtension::didBindToARA(); // JUCE requires calling the base hook

    // Give THIS instance's playback renderer our per-instance SessionState, so its background
    // worker publishes the waveform / status / manual selection to OUR editor only.
    if (auto* renderer = getPlaybackRenderer<ara::ToneFillPlaybackRenderer>())
    {
        renderer->setSessionState (sessionState_);
        pluginLog ("didBindToARA: wired SessionState to playback renderer");
    }
    else
    {
        pluginLog ("didBindToARA: no playback renderer for this instance");
    }
}
#endif

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Reset only the PER-PASS state; learnedModel_ / genFill_ persist across AudioSuite passes
    // (Learn pass then Generate pass are separate prepareToPlay calls on the same instance).
    if (sampleRate != currentSampleRate_) { genFillLen_ = 0; genHash_ = 0; } // stale at new SR
    currentSampleRate_ = sampleRate;
    hostBlockSize_     = samplesPerBlock;
    learnInputLen_     = 0;
    learnDone_         = false;
    genPos_            = 0;

#if TONEFILL_ARA_AVAILABLE
    // Route ARA playback/bounce through the ARA renderer roles.
    prepareToPlayForARA (sampleRate, samplesPerBlock, getMainBusNumOutputChannels(), getProcessingPrecision());
#else
    juce::ignoreUnused (samplesPerBlock);
#endif
}

void PluginProcessor::releaseResources()
{
#if TONEFILL_ARA_AVAILABLE
    releaseResourcesForARA();
#endif
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

#if TONEFILL_ARA_AVAILABLE
    static std::atomic<bool> loggedProc { false };
    if (! loggedProc.exchange (true))
        pluginLog ("processBlock first call: isBoundToARA=" + juce::String ((int) isBoundToARA())
                   + " ch=" + juce::String (buffer.getNumChannels()));
    // When ARA-bound, the ARAPlaybackRenderer serves the synthesized fill for the region.
    if (processBlockForARA (buffer, isRealtime(), getPlayHead()))
        return;
#endif
    // Not ARA-bound -> Pro Tools AudioSuite (or plain insert) LEARN / GENERATE path.
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const bool learn = params_.apvts.getRawParameterValue (ParameterState::IDs::learnMode)->load() > 0.5f;

    {
        static std::atomic<bool> loggedNonAra { false };
        if (! loggedNonAra.exchange (true))
            pluginLog ("processBlock NON-ARA first call: learnMode=" + juce::String ((int) learn)
                       + " nonRealtime=" + juce::String ((int) isNonRealtime())
                       + " ch=" + juce::String (numCh) + " block=" + juce::String (numSamples)
                       + " mode=" + juce::String ((int) params_.apvts.getRawParameterValue (ParameterState::IDs::mode)->load()));
    }

    if (learn)
    {
        // LEARN: accumulate the selection and analyze it into learnedModel_. Audio is left UNTOUCHED
        // (passthrough), so learning from a region never alters it. Works in Preview and Render.
        // Learn from the WHOLE selection (up to 4 min, or 15 min with "Full") so there is as much
        // clean material as the ARA path has - small windows make the grain cloud repetitive.
        const bool whole = params_.apvts.getRawParameterValue (ParameterState::IDs::wholeFile)->load() > 0.5f;
        const int cap = (int) ((whole ? 900.0 : 240.0) * currentSampleRate_);
        if (learnInput_.getNumSamples() != cap || learnInput_.getNumChannels() != juce::jmax (1, numCh))
        { learnInput_.setSize (juce::jmax (1, numCh), cap, true, true, true); learnInputLen_ = 0; learnDone_ = false; }
        if (learnInputLen_ < cap)
        {
            const int room = juce::jmin (numSamples, cap - learnInputLen_);
            for (int ch = 0; ch < numCh; ++ch) learnInput_.copyFrom (ch, learnInputLen_, buffer, ch, 0, room);
            learnInputLen_ += room;
        }
        // Analyze the WHOLE selection: fire on the final partial block (smaller than the host
        // block size), or as a safety once we have a lot of material / hit the cap.
        const bool tailBlock = hostBlockSize_ > 0 && numSamples > 0 && numSamples < hostBlockSize_
                               && learnInputLen_ > (int) (1.0 * currentSampleRate_);
        const bool safety    = learnInputLen_ >= cap; // only when the selection fills the cap
        if (! learnDone_ && (tailBlock || safety))
            analyzeLearn();
        return; // passthrough
    }

    // GENERATE: output room tone from the learned model - no input needed, so it starts at sample
    // 0 (no head silence). Rebuild the loopable fill only when render params change; tile it
    // otherwise. Output gain is applied per block so the knob responds live in Preview.
    // Adopt the process-global model if this (possibly different) instance never learned.
    if (learnedModel_ == nullptr) { learnedModel_ = getGlobalLearned(); genHash_ = 0; }
    {
        static std::atomic<bool> loggedGen { false };
        if (! loggedGen.exchange (true))
            pluginLog ("processBlock GENERATE first call: hasModel=" + juce::String ((int) (learnedModel_ != nullptr))
                       + " realtime=" + juce::String ((int) (! isNonRealtime())) + " ch=" + juce::String (numCh));
    }
    if (learnedModel_ == nullptr) { buffer.clear(); return; }
    const std::uint64_t h = genParamHash();
    if (h != genHash_ || genFillLen_ <= 0) { buildGenFill(); genHash_ = h; genPos_ = 0; }
    buffer.clear();
    if (genFillLen_ <= 0) return;

    const float gain = juce::Decibels::decibelsToGain (params_.apvts.getRawParameterValue (ParameterState::IDs::outputGain)->load());
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& fillCh = genFill_[(std::size_t) juce::jmin (ch, (int) genFill_.size() - 1)];
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = fillCh[(std::size_t) ((genPos_ + i) % genFillLen_)] * gain;
    }
    genPos_ += numSamples;
}

void PluginProcessor::analyzeLearn()
{
    learnDone_ = true;
    const int n  = learnInputLen_;
    const int ch = learnInput_.getNumChannels();
    if (n < 4096 || ch <= 0) return;

    auto& apvts = params_.apvts;
    auto pv = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    core::DiagnosticsLogger diag;
    engine::analysis::AnalysisSession session (diag);
    std::atomic<bool> cancel { false };

    engine::analysis::AnalysisContext ctx;
    ctx.leftContext.setSize (ch, n);
    for (int c = 0; c < ch; ++c) ctx.leftContext.copyFrom (c, 0, learnInput_, c, 0, n);
    ctx.rightContext.makeCopyOf (ctx.leftContext);
    ctx.analysisSampleRate  = currentSampleRate_;
    ctx.numChannels         = ch;
    ctx.leftEnabled         = true;
    ctx.useManualSelection  = false;
    ctx.cleanThreshold      = pv (ParameterState::IDs::threshold);
    ctx.speechReject        = pv (ParameterState::IDs::speechReject);
    ctx.sourceContentHash   = (std::uint64_t) n;

    pluginLog ("analyzeLearn: n=" + juce::String (n) + " ch=" + juce::String (ch)
               + " thr=" + juce::String (ctx.cleanThreshold) + " voice=" + juce::String (ctx.speechReject));
    auto r = session.run (ctx, cancel);
    if (! r.ok()) { pluginLog ("analyzeLearn: analysis FAILED"); return; }
    learnedModel_ = r.value();
    const int cleanCh  = (int) learnedModel_->cleanAudioPerChannel.size();
    const int cleanLen = (cleanCh > 0) ? (int) learnedModel_->cleanAudioPerChannel[0].size() : 0;
    pluginLog ("analyzeLearn: OK learnSec=" + juce::String (learnedModel_->learnMaterialSeconds)
               + " cleanCh=" + juce::String (cleanCh) + " cleanLen=" + juce::String (cleanLen)
               + " modelCh=" + juce::String (learnedModel_->numChannels));
    setGlobalLearned (learnedModel_); // share so a different Render instance can use it
    genHash_ = 0; genFillLen_ = 0; genFill_.clear(); // force regenerate from the new model
    if (sessionState_ != nullptr)
    {
        sessionState_->learnSeconds.store (learnedModel_->learnMaterialSeconds);
        sessionState_->phase.store (2);
    }
}

std::uint64_t PluginProcessor::genParamHash() const
{
    auto& a = params_.apvts;
    auto f = [&] (const char* id) { return a.getRawParameterValue (id)->load(); };
    auto bits = [] (float x) { std::uint32_t u = 0; std::memcpy (&u, &x, sizeof (u)); return (std::uint64_t) u; };
    auto mix = [] (std::uint64_t h, std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2); return h; };
    std::uint64_t h = 0;
    h = mix (h, (std::uint64_t) f (ParameterState::IDs::mode));
    h = mix (h, bits (f (ParameterState::IDs::fragment)));
    h = mix (h, bits (f (ParameterState::IDs::blend)));
    h = mix (h, bits (f (ParameterState::IDs::randomness)));
    h = mix (h, bits (f (ParameterState::IDs::movement)));
    h = mix (h, f (ParameterState::IDs::paulStretch) > 0.5f ? 1u : 0u);
    return h;
}

void PluginProcessor::buildGenFill()
{
    genFillLen_ = 0; genFill_.clear();
    if (learnedModel_ == nullptr) return;

    auto& apvts = params_.apvts;
    auto pv = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const int ch = juce::jmax (1, learnedModel_->numChannels > 0
                                      ? learnedModel_->numChannels
                                      : (int) learnedModel_->cleanAudioPerChannel.size());

    engine::model::RenderSettings s;
    s.mode           = engine::model::Mode::Ambience;
    s.movement       = pv (ParameterState::IDs::movement);              // Mix
    s.paulStretch    = pv (ParameterState::IDs::paulStretch) > 0.5f;    // Enhance
    s.fragmentMs     = 200.0f + pv (ParameterState::IDs::fragment) * 2800.0f;
    s.blendFrac      = 0.05f + pv (ParameterState::IDs::blend) * 0.45f;
    s.randomness     = pv (ParameterState::IDs::randomness);
    s.seed           = 1;
    const double loopSec = 15.0 + (double) s.randomness * 45.0;
    const int loopLen = (int) (loopSec * currentSampleRate_);
    const int xf      = (int) (0.25 * currentSampleRate_);
    s.targetSampleRate     = currentSampleRate_;
    s.targetChannels       = ch;
    s.targetDurationSamples = (long long) (loopLen + xf);

    const int mdlCleanLen = learnedModel_->cleanAudioPerChannel.empty() ? 0
                                : (int) learnedModel_->cleanAudioPerChannel[0].size();
    pluginLog ("buildGenFill: mode=" + juce::String ((int) s.mode) + " ch=" + juce::String (ch)
               + " modelCleanLen=" + juce::String (mdlCleanLen) + " loopLen=" + juce::String (loopLen));

    std::atomic<bool> cancel { false };
    engine::synthesis::AmbienceRenderer renderer;
    auto out = renderer.render (*learnedModel_, s, cancel);
    if (! out.ok()) { pluginLog ("buildGenFill: render FAILED"); return; }
    auto chans = std::move (out.value().channels);

    for (auto& c : chans)
    {
        if ((int) c.size() >= loopLen + xf)
        {
            for (int i = 0; i < xf; ++i)
            {
                const float g = (float) i / (float) (xf - 1) * 1.5707963f;
                c[(std::size_t) i] = c[(std::size_t) (loopLen + i)] * std::cos (g) + c[(std::size_t) i] * std::sin (g);
            }
            c.resize ((std::size_t) loopLen);
        }
    }
    if (chans.empty() || chans[0].empty()) { pluginLog ("buildGenFill: empty render"); return; }
    genFill_    = std::move (chans);
    genFillLen_ = (long long) genFill_[0].size();
    pluginLog ("buildGenFill: DONE fillLen=" + juce::String (genFillLen_) + " ch=" + juce::String ((int) genFill_.size()));
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = params_.apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        params_.apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
} // namespace tonefill::plugin

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonefill::plugin::PluginProcessor();
}
