#include "plugin/ara/PlaybackRenderer.h"

#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_formats/juce_audio_formats.h> // juce::ARAAudioSourceReader

#include "core/DiagnosticsLogger.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"
#include "plugin/SessionState.h"

#include <cmath>
#include <functional>

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

    FillWorker (juce::ARAAudioSource* sourceIn, int channelsIn, double sampleRateIn, Publish publishIn)
        : juce::Thread ("ToneFillAnalysis"),
          source (sourceIn), channels (channelsIn), sampleRate (sampleRateIn),
          publish (std::move (publishIn))
    {
    }

    ~FillWorker() override { stopThread (3000); }

    void run() override
    {
        if (source == nullptr || channels <= 0) return;

        // Cap learn material at 30 s to bound time/memory.
        const long long total = (long long) source->getSampleCount();
        const int n = (int) juce::jmin (total, (long long) (30.0 * sampleRate));
        araLog ("worker.run: sourceSamples=" + juce::String (total) + " read=" + juce::String (n)
                + " ch=" + juce::String (channels) + " sr=" + juce::String (sampleRate));
        if (n <= 0) return;

        juce::ARAAudioSourceReader reader (source);
        juce::AudioBuffer<float> src (channels, n);

        // Sample access may not be enabled immediately; retry until we get non-silent audio.
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
        araLog ("worker.run: gotAudio=" + juce::String ((int) gotAudio));
        if (! gotAudio || threadShouldExit()) return;

        // Mono detection: if the two channels are (near) identical, treat as mono and render a
        // single correlated channel -> avoids the "weird stereo" from independent L/R synthesis.
        int effCh = channels;
        if (channels >= 2)
        {
            double diff = 0.0, ref = 0.0;
            const float* a = src.getReadPointer (0);
            const float* b = src.getReadPointer (1);
            for (int i = 0; i < n; ++i) { const double d = a[i] - b[i]; diff += d * d; ref += (double) a[i] * a[i]; }
            if (ref > 0.0 && diff / ref < 1.0e-4) effCh = 1;
        }
        juce::AudioBuffer<float> learnSrc (effCh, n);
        for (int ch = 0; ch < effCh; ++ch) learnSrc.copyFrom (ch, 0, src, ch, 0, n);
        araLog ("worker.run: effCh=" + juce::String (effCh) + " (src ch=" + juce::String (channels) + ")");

        tonefill::core::DiagnosticsLogger diag;
        engine::analysis::AnalysisSession session (diag);
        std::atomic<bool> cancel { false };
        auto& ss = tonefill::plugin::SessionState::get();

        // (Re)analyze with the current Threshold; updates the UI status. Re-runs when Threshold
        // moves (it changes WHICH source material is learned, so it needs fresh analysis).
        auto analyze = [&] (float threshold) -> engine::model::AmbienceModelPtr
        {
            engine::analysis::AnalysisContext ctx;
            ctx.leftContext.makeCopyOf (learnSrc);
            ctx.rightContext.makeCopyOf (learnSrc);
            ctx.analysisSampleRate = sampleRate;
            ctx.numChannels = effCh;
            ctx.leftEnabled = true;
            ctx.cleanThreshold = threshold;
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
            return m;
        };

        engine::synthesis::AmbienceRenderer renderer;
        engine::model::AmbienceModelPtr model;
        float lastThreshold = -1.0f;
        int lastGen = -1;

        while (! threadShouldExit())
        {
            const int gen = ss.generation.load();
            const float thr = ss.threshold.load();
            bool needRender = false;

            if (model == nullptr || std::abs (thr - lastThreshold) > 1.0e-4f)
            {
                model = analyze (thr);
                lastThreshold = thr;
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
                s.seed = ss.seed.load();
                const int loopLen = (int) (20.0 * sampleRate);
                const int xf = (int) (0.25 * sampleRate); // 250 ms seamless-loop crossfade
                s.targetSampleRate = sampleRate;
                s.targetChannels = effCh;
                s.targetDurationSamples = (long long) (loopLen + xf);

                auto out = renderer.render (*model, s, cancel);
                if (out.ok())
                {
                    auto data = std::make_shared<FillData>();
                    data->channels = std::move (out.value().channels);

                    // Make the loop seamless: blend the continuation tail [loopLen, loopLen+xf)
                    // back over the head [0, xf), so wrapping loopLen-1 -> 0 has no click.
                    for (auto& c : data->channels)
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
                    data->length = data->channels.empty() ? 0 : (long long) data->channels[0].size();
                    if (data->length > 0)
                    {
                        // Snapshot for "Export WAV" from the UI.
                        ss.setExportFill (std::make_shared<const tonefill::plugin::SessionState::FillBuffer> (data->channels),
                                          sampleRate);
                        publish (std::move (data));
                    }
                }
            }
            wait (100);
        }
    }

private:
    juce::ARAAudioSource* source;
    int channels;
    double sampleRate;
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
    if (! analysisStarted.load())
    {
        for (const auto playbackRegion : getPlaybackRegions())
        {
            if (auto* source = playbackRegion->getAudioModification()->getAudioSource())
            {
                analysisStarted.store (true);
                araLog ("prepareToPlay: starting worker");
                worker = std::make_unique<FillWorker> (
                    source, numChannels, sampleRate,
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

    // Output gain + meter.
    const float gain = tonefill::plugin::SessionState::get().outputGain.load();
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
    tonefill::plugin::SessionState::get().outMeterDb.store (peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f);

    return true;
}
} // namespace tonefill::plugin::ara

#endif // TONEFILL_ARA_AVAILABLE
