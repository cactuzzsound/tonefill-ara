#include "plugin/ara/PlaybackRenderer.h"

#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_formats/juce_audio_formats.h> // juce::ARAAudioSourceReader

#include "core/DiagnosticsLogger.h"
#include "dsp/Loudness.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"
#include "plugin/SessionState.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace tonefill::plugin::ara
{
// Debug log to ~/tonefill_ara.log so the ARA path can be traced from outside the DAW.
void araLog (const juce::String& msg)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("tonefill_ara.log")
        .appendText (juce::Time::getCurrentTime().toString (false, true, true, true) + "  " + msg + "\n");
}

// Background one-shot: read the ARA source, analyze, render a loopable fill, publish it.
class ToneFillPlaybackRenderer::FillWorker : public juce::Thread
{
public:
    using Publish = std::function<void (std::shared_ptr<const FillData>)>;

    FillWorker (juce::ARAAudioSource* sourceIn, int channelsIn, double sampleRateIn,
                std::shared_ptr<plugin::SessionState> stateIn, Publish publishIn)
        : juce::Thread ("ToneFillAnalysis"),
          source (sourceIn), channels (channelsIn), sampleRate (sampleRateIn),
          state (std::move (stateIn)), publish (std::move (publishIn))
    {
    }

    ~FillWorker() override { stopThread (3000); }

    void run() override
    {
        if (source == nullptr || channels <= 0) return;
        if (state == nullptr) { araLog ("worker.run: no SessionState wired, aborting"); return; }
        auto& ss = *state;

        const long long total = (long long) source->getSampleCount();
        juce::ARAAudioSourceReader reader (source);

        // src / learnSrc are (re)filled by readSource: the analysed window. The cap is 4 min by
        // default, or the whole item when the user enables "Full". Re-runnable so the Full toggle
        // takes effect live (re-reads with the new cap).
        juce::AudioBuffer<float> src, learnSrc;
        int n = 0, effCh = channels;

        auto readSource = [&] (double capSec) -> bool
        {
            const int want = (int) juce::jmin (total, (long long) (capSec * sampleRate));
            if (want <= 0) return false;
            try { src.setSize (channels, want); }
            catch (...) { araLog ("readSource: allocation failed for " + juce::String (want) + " samples"); return false; }
            n = want;
            araLog ("readSource: total=" + juce::String (total) + " read=" + juce::String (n)
                    + " ch=" + juce::String (channels) + " sr=" + juce::String (sampleRate) + " cap=" + juce::String (capSec));

            bool gotAudio = false;
            for (int attempt = 0; attempt < 30 && ! threadShouldExit(); ++attempt)
            {
                src.clear();
                const bool readOk = reader.read (&src, 0, n, 0, true, true);
                double energy = 0.0;
                for (int c = 0; c < channels; ++c)
                {
                    const auto* d = src.getReadPointer (c);
                    for (int i = 0; i < n; ++i) energy += (double) d[i] * d[i];
                }
                if (attempt < 3 || energy > 1.0e-9)
                    araLog ("  read attempt " + juce::String (attempt) + " ok=" + juce::String ((int) readOk)
                            + " energy=" + juce::String (energy));
                if (energy > 1.0e-9) { gotAudio = true; break; }
                wait (200);
            }
            if (! gotAudio) return false;

            // Mono detection: near-identical channels -> render one correlated channel.
            effCh = channels;
            if (channels >= 2)
            {
                double diff = 0.0, ref = 0.0;
                const float* a = src.getReadPointer (0);
                const float* b = src.getReadPointer (1);
                for (int i = 0; i < n; ++i) { const double d = a[i] - b[i]; diff += d * d; ref += (double) a[i] * a[i]; }
                if (ref > 0.0 && diff / ref < 1.0e-4) effCh = 1;
            }
            learnSrc.setSize (effCh, n);
            for (int ch = 0; ch < effCh; ++ch) learnSrc.copyFrom (ch, 0, src, ch, 0, n);
            ss.sourceSamples.store (n);
            araLog ("readSource: effCh=" + juce::String (effCh) + " (src ch=" + juce::String (channels) + ")");
            return true;
        };

        bool lastWhole = ss.wholeFile.load();
        if (! readSource (lastWhole ? 900.0 : 240.0) || threadShouldExit()) return;

        tonefill::core::DiagnosticsLogger diag;
        engine::analysis::AnalysisSession session (diag);
        std::atomic<bool> cancel { false };

        // (Re)analyze with the current Threshold; updates the UI status. Re-runs when Threshold
        // moves (it changes WHICH source material is learned, so it needs fresh analysis).
        auto analyze = [&] (float threshold, const juce::AudioBuffer<float>& learnInput, bool useManual)
            -> engine::model::AmbienceModelPtr
        {
            engine::analysis::AnalysisContext ctx;
            ctx.leftContext.makeCopyOf (learnInput);
            ctx.rightContext.makeCopyOf (learnInput);
            ctx.analysisSampleRate = sampleRate;
            ctx.numChannels = effCh;
            ctx.leftEnabled = true;
            ctx.useManualSelection = useManual;
            ctx.cleanThreshold = threshold;
            ctx.speechReject = ss.speechReject.load();
            ctx.sourceContentHash = (std::uint64_t) n;
            ss.phase.store (1);
            auto r = session.run (ctx, cancel);
            if (! r.ok()) { ss.phase.store (0); return nullptr; }
            const auto m = r.value();
            const int nP = m->tonalPerChannel.empty() ? 0 : (int) m->tonalPerChannel[0].partials.size();
            const float lvl = (m->noisePerChannel.empty() || m->noisePerChannel[0].targetRms <= 0.0f)
                                  ? -120.0f : 20.0f * std::log10 (m->noisePerChannel[0].targetRms);
            ss.numPartials.store (nP);
            ss.learnSeconds.store (m->learnMaterialSeconds);
            ss.levelDb.store (lvl);
            ss.phase.store (2);
            araLog ("analyze: thr=" + juce::String (threshold) + " partials=" + juce::String (nP)
                    + " learnSec=" + juce::String (m->learnMaterialSeconds) + " levelDb=" + juce::String (lvl));

            // Waveform for the UI: peak per bin + an (approximate, level-based) clean flag.
            const int bins = 220;
            tonefill::plugin::SessionState::WaveData wd;
            wd.peak.assign ((std::size_t) bins, 0.0f);
            wd.clean.assign ((std::size_t) bins, 0);
            const int per = juce::jmax (1, n / bins);
            const float* s0 = src.getReadPointer (0);
            for (int b = 0; b < bins; ++b)
            {
                float pk = 0.0f;
                const int from = b * per, to = juce::jmin (from + per, n);
                for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::fabs (s0[i]));
                wd.peak[(std::size_t) b] = pk;
            }
            // Exact clean overlay from the analysis's selected sample ranges.
            for (int b = 0; b < bins; ++b)
            {
                const int bs = b * per, be = bs + per;
                bool cl = false;
                for (const auto& r : m->cleanRanges)
                    if (r.first < be && r.second > bs) { cl = true; break; }
                wd.clean[(std::size_t) b] = cl ? 1 : 0;
            }
            ss.setWave (std::move (wd));

            return m;
        };

        // Build the learn material from the user-selected regions (manual mode) or the full source.
        auto buildManual = [&] (const std::vector<std::pair<int, int>>& ranges) -> juce::AudioBuffer<float>
        {
            const int xf = (int) (0.02 * sampleRate);
            long long tot = 0;
            for (const auto& r : ranges)
                tot += juce::jlimit (0, n, r.second) - juce::jlimit (0, n, r.first);
            if (tot < (long long) (0.05 * sampleRate)) return learnSrc; // nothing usable -> full source
            juce::AudioBuffer<float> out (effCh, (int) tot);
            int w = 0;
            for (const auto& r : ranges)
            {
                const int s = juce::jlimit (0, n, r.first);
                const int e = juce::jlimit (0, n, r.second);
                const int len = e - s;
                if (len <= 0) continue;
                for (int ch = 0; ch < effCh; ++ch)
                {
                    if (w > 0 && xf > 0) // tiny equal-power join crossfade
                    {
                        const int ov = juce::jmin (xf, juce::jmin (len, w));
                        float* d = out.getWritePointer (ch);
                        const float* sp = learnSrc.getReadPointer (ch);
                        for (int i = 0; i < ov; ++i)
                        {
                            const float g = (float) i / (float) juce::jmax (1, ov - 1) * 1.5707964f;
                            d[w - ov + i] = d[w - ov + i] * std::cos (g) + sp[s + i] * std::sin (g);
                        }
                        out.copyFrom (ch, w, learnSrc, ch, s + ov, len - ov);
                    }
                    else
                    {
                        out.copyFrom (ch, w, learnSrc, ch, s, len);
                    }
                }
                w += (w > 0 && xf > 0) ? len - juce::jmin (xf, juce::jmin (len, w)) : len;
            }
            out.setSize (effCh, juce::jmax (1, w), true);
            return out;
        };

        engine::synthesis::AmbienceRenderer renderer;
        engine::model::AmbienceModelPtr model;
        float lastThreshold = -1.0f, lastSpeech = -1.0f;
        int lastGen = -1, lastManualGen = -1;
        bool lastManual = false;

        // Last rendered seamless loop BEFORE any output/normalize gain, plus its measured levels.
        // Keeping it lets a Normalize/target change re-scale + re-publish without a full re-render.
        std::shared_ptr<std::vector<std::vector<float>>> unnorm;
        float measPeakDb = -120.0f, measLufs = -120.0f;
        bool  lastNorm = false, lastNormUnit = true;
        float lastNormTarget = -999.0f;

        auto applyAndPublish = [&] ()
        {
            if (unnorm == nullptr || unnorm->empty() || (*unnorm)[0].empty()) return;

            // Normalize on -> bake the loudness gain into the fill (audio thread runs at unity).
            // Off -> publish as-is; the manual Output gain is applied live in processBlock.
            float gain = 1.0f;
            if (ss.normalizeEnabled.load())
            {
                const bool  lufs   = ss.normalizeLufs.load();
                const float target = ss.normalizeTarget.load();
                const float meas   = lufs ? measLufs : measPeakDb;
                if (meas > -119.0f)
                {
                    float gainDb = target - meas;
                    // LUFS targets can push the peak over 0 dBFS; cap true peak at -1 dBFS.
                    if (lufs)
                    {
                        const float newPeak = measPeakDb + gainDb;
                        if (newPeak > -1.0f) gainDb -= (newPeak + 1.0f);
                    }
                    gain = std::pow (10.0f, gainDb / 20.0f);
                }
            }

            auto data = std::make_shared<FillData>();
            data->channels = *unnorm;
            if (std::fabs (gain - 1.0f) > 1.0e-4f)
                for (auto& c : data->channels)
                    for (auto& smp : c) smp *= gain;
            data->length = (long long) data->channels[0].size();

            ss.setExportFill (std::make_shared<const tonefill::plugin::SessionState::FillBuffer> (data->channels),
                              sampleRate);
            publish (std::move (data));
        };

        while (! threadShouldExit())
        {
            const int gen = ss.generation.load();
            const float thr = ss.threshold.load();
            const float spk = ss.speechReject.load();
            const bool manual = ss.manualMode.load();
            const int mGen = ss.manualGen.load();
            bool needRender = false;

            // "Full" toggle changed -> re-read the source window (whole item vs first 4 min) and
            // force a fresh analysis from it.
            const bool whole = ss.wholeFile.load();
            if (whole != lastWhole)
            {
                readSource (whole ? 900.0 : 240.0);
                lastWhole = whole;
                model = nullptr; // force re-analyze with the new window
            }

            // Re-analyze when an analysis input changes: Threshold, Speech Reject, manual on/off,
            // or the manual selection itself.
            if (model == nullptr || std::abs (thr - lastThreshold) > 1.0e-4f
                || std::abs (spk - lastSpeech) > 1.0e-4f || manual != lastManual || mGen != lastManualGen)
            {
                const auto ranges = manual ? ss.getManualRanges() : std::vector<std::pair<int, int>>{};
                const bool useManual = manual && ! ranges.empty();
                const juce::AudioBuffer<float> learnInput = useManual ? buildManual (ranges) : learnSrc;
                model = analyze (thr, learnInput, useManual);
                lastThreshold = thr; lastSpeech = spk; lastManual = manual; lastManualGen = mGen;
                needRender = true;
            }
            if (gen != lastGen) { lastGen = gen; needRender = true; }

            if (needRender && model != nullptr)
            {
                engine::model::RenderSettings s;
                s.mode = (engine::model::Mode) juce::jlimit (0, 3, ss.mode.load());
                s.tonalRetention = ss.tonalRetention.load();
                s.textureAmount = ss.textureAmount.load();
                s.movement = ss.movement.load();
                s.fragmentMs = 200.0f + ss.fragment.load() * 2800.0f;   // 200..3000 ms
                s.blendFrac  = 0.05f + ss.blend.load() * 0.45f;          // 5..50 %
                s.randomness = ss.randomness.load();
                s.seed = ss.seed.load();
                // Loop length grows with Variation so the exact repeat happens far less often
                // (the main cause of the "looped" feel on short samples).
                const double loopSec = 15.0 + (double) s.randomness * 45.0;  // 15..60 s
                const int loopLen = (int) (loopSec * sampleRate);
                const int xf = (int) (0.25 * sampleRate); // 250 ms seamless-loop crossfade
                s.targetSampleRate = sampleRate;
                s.targetChannels = effCh;
                s.targetDurationSamples = (long long) (loopLen + xf);

                auto out = renderer.render (*model, s, cancel);
                if (out.ok())
                {
                    auto chans = std::move (out.value().channels);

                    // Make the loop seamless: blend the continuation tail [loopLen, loopLen+xf)
                    // back over the head [0, xf), so wrapping loopLen-1 -> 0 has no click.
                    for (auto& c : chans)
                    {
                        if ((int) c.size() >= loopLen + xf)
                        {
                            for (int i = 0; i < xf; ++i)
                            {
                                const float g = (float) i / (float) (xf - 1) * 1.5707963f;
                                c[(std::size_t) i] = c[(std::size_t) (loopLen + i)] * std::cos (g)
                                                   + c[(std::size_t) i] * std::sin (g);
                            }
                            c.resize ((std::size_t) loopLen);
                        }
                    }

                    if (! chans.empty() && ! chans[0].empty())
                    {
                        unnorm = std::make_shared<std::vector<std::vector<float>>> (std::move (chans));
                        measPeakDb = tonefill::dsp::peakDbfs (*unnorm);
                        measLufs   = tonefill::dsp::integratedLufs (*unnorm, sampleRate);
                        ss.measuredPeakDb.store (measPeakDb);
                        ss.measuredLufs.store (measLufs);
                        applyAndPublish();
                    }
                }
            }
            else if (unnorm != nullptr
                     && (ss.normalizeEnabled.load() != lastNorm
                         || std::abs (ss.normalizeTarget.load() - lastNormTarget) > 1.0e-4f
                         || ss.normalizeLufs.load() != lastNormUnit))
            {
                applyAndPublish(); // cheap re-scale + re-publish; no re-analyze / re-render
            }

            lastNorm       = ss.normalizeEnabled.load();
            lastNormTarget = ss.normalizeTarget.load();
            lastNormUnit   = ss.normalizeLufs.load();
            wait (100);
        }
    }

private:
    juce::ARAAudioSource* source;
    int channels;
    double sampleRate;
    std::shared_ptr<plugin::SessionState> state; // co-owned: outlives the worker
    Publish publish;
};

//==============================================================================
ToneFillPlaybackRenderer::ToneFillPlaybackRenderer (ARA::PlugIn::DocumentController* dc,
                                                    ProcessingLockInterface& lock)
    : juce::ARAPlaybackRenderer (dc), lockInterface (lock)
{
}

ToneFillPlaybackRenderer::~ToneFillPlaybackRenderer()
{
    if (worker != nullptr)
        worker->stopThread (3000);
}

void ToneFillPlaybackRenderer::prepareToPlay (double sampleRateIn, int maxBlockIn, int numChannelsIn,
                                              juce::AudioProcessor::ProcessingPrecision,
                                              AlwaysNonRealtime)
{
    sampleRate = sampleRateIn;
    numChannels = numChannelsIn;
    maximumSamplesPerBlock = maxBlockIn;

    araLog ("prepareToPlay: regions=" + juce::String ((int) getPlaybackRegions().size())
            + " ch=" + juce::String (numChannels) + " started=" + juce::String ((int) analysisStarted.load()));

    // Kick off analysis once, from the first region's source (off the audio thread).
    if (! analysisStarted.load() && state_ != nullptr)
    {
        for (const auto playbackRegion : getPlaybackRegions())
        {
            if (auto* source = playbackRegion->getAudioModification()->getAudioSource())
            {
                analysisStarted.store (true);
                araLog ("prepareToPlay: starting worker");
                worker = std::make_unique<FillWorker> (
                    source, numChannels, sampleRate, state_,
                    [this] (std::shared_ptr<const FillData> data)
                    {
                        { const juce::SpinLock::ScopedLockType l (fillLock); fill = std::move (data); }
                        fillReady.store (true, std::memory_order_release);
                    });
                worker->startThread();
                break;
            }
        }
    }
}

void ToneFillPlaybackRenderer::releaseResources()
{
    // Keep the worker/fill alive across stop/start so we don't re-analyze on every transport stop.
}

bool ToneFillPlaybackRenderer::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::AudioProcessor::Realtime realtime,
                                             const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto lock = lockInterface.getProcessingLock();
    if (! lock.isLocked())
        return true;

    // Offline render / bounce: wait for the fill so the rendered file isn't silent. Blocking is
    // fine here because this is non-realtime; never block during realtime playback.
    if (realtime == juce::AudioProcessor::Realtime::no)
        for (int waited = 0; waited < 8000 && ! fillReady.load (std::memory_order_acquire); waited += 20)
            juce::Thread::sleep (20);

    static std::atomic<bool> loggedPB { false };
    if (! loggedPB.exchange (true))
        araLog ("processBlock first call: fillReady=" + juce::String ((int) fillReady.load())
                + " regions=" + juce::String ((int) getPlaybackRegions().size())
                + " playing=" + juce::String ((int) positionInfo.getIsPlaying()));

    buffer.clear(); // default: silence until the fill is ready / when not playing

    if (! fillReady.load (std::memory_order_acquire) || ! positionInfo.getIsPlaying())
        return true;

    std::shared_ptr<const FillData> data;
    {
        const juce::SpinLock::ScopedTryLockType l (fillLock);
        if (l.isLocked()) data = fill;
    }
    if (data == nullptr || data->length <= 0)
        return true;

    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback (0);
    const auto blockRange = juce::Range<juce::int64>::withStartAndLength (timeInSamples, (juce::int64) numSamples);

    for (const auto playbackRegion : getPlaybackRegions())
    {
        const auto songRange =
            playbackRegion->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
        const auto renderRange = blockRange.getIntersectionWith (songRange);
        if (renderRange.isEmpty())
            continue;

        const int startInBuffer = (int) (renderRange.getStart() - blockRange.getStart());
        const int count = (int) renderRange.getLength();
        const auto regionStart = songRange.getStart();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto& srcCh = data->channels[(std::size_t) juce::jmin (ch, (int) data->channels.size() - 1)];
            auto* dst = buffer.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
            {
                long long fpos = (renderRange.getStart() + i - regionStart) % data->length;
                if (fpos < 0) fpos += data->length;
                dst[startInBuffer + i] = srcCh[(std::size_t) fpos];
            }
        }
    }

    // Output gain + meter. When Normalize is on the fill is already baked to target, so the
    // live manual gain is bypassed (unity).
    const float gain = (state_ != nullptr && ! state_->normalizeEnabled.load())
                           ? state_->outputGain.load() : 1.0f;
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            d[i] *= gain;
            peak = juce::jmax (peak, std::fabs (d[i]));
        }
    }
    if (state_ != nullptr)
        state_->outMeterDb.store (peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f);

    return true;
}
} // namespace tonefill::plugin::ara

#endif // TONEFILL_ARA_AVAILABLE
