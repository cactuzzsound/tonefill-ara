#include "plugin/ara/PlaybackRenderer.h"

#if TONEFILL_ARA_AVAILABLE

#include <juce_audio_formats/juce_audio_formats.h> // juce::ARAAudioSourceReader

#include "core/DiagnosticsLogger.h"
#include "dsp/Loudness.h"
#include "engine/analysis/AnalysisSession.h"
#include "engine/synthesis/AmbienceRenderer.h"
#include "engine/model/RenderSettings.h"
#include "dsp/SpectralBands.h"
#include "plugin/SessionState.h"
#include "plugin/ara/DocumentControllerImpl.h"

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

namespace
{
// Bypass A/B: write the analysed SOURCE into the region range (not looped) instead of the fill, so
// the user can compare the room tone against the original. Shared by both renderers.
void tileSourcePreview (juce::AudioBuffer<float>& buffer, double sampleRate,
                        const std::vector<juce::ARAPlaybackRegion*>& regions,
                        const juce::AudioPlayHead::PositionInfo& positionInfo,
                        const plugin::SessionState::FillBuffer& src)
{
    if (src.empty() || src[0].empty()) return;
    const long long len = (long long) src[0].size();
    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback (0);
    const auto blockRange = juce::Range<juce::int64>::withStartAndLength (timeInSamples, (juce::int64) numSamples);

    for (const auto region : regions)
    {
        const auto songRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
        const auto renderRange = blockRange.getIntersectionWith (songRange);
        if (renderRange.isEmpty()) continue;

        const int startInBuffer = (int) (renderRange.getStart() - blockRange.getStart());
        const int count = (int) renderRange.getLength();
        const auto regionStart = songRange.getStart();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto& srcCh = src[(std::size_t) juce::jmin (ch, (int) src.size() - 1)];
            auto* dst = buffer.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
            {
                const long long spos = renderRange.getStart() + i - regionStart; // aligned, not looped
                if (spos >= 0 && spos < len) dst[startInBuffer + i] = srcCh[(std::size_t) spos];
            }
        }
    }
}
} // namespace

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
            // Quality-first: analyse as much of the source as fits a memory budget rather than a short
            // fixed window -- more material means better clean-tone selection on long takes. Degrades
            // gracefully (halve on allocation failure) instead of aborting on a huge file.
            const long long kByteBudget    = 1500LL * 1024 * 1024;                 // ~1.5 GB for src + learn
            const long long bytesPerSample = (long long) juce::jmax (1, channels) * 4 * 2;
            const long long maxByMem       = juce::jmax ((long long) (sampleRate * 5), kByteBudget / bytesPerSample);
            long long want = juce::jmin (total, (long long) (capSec * sampleRate));
            want = juce::jmin (want, maxByMem);
            if (want <= 0) return false;

            bool alloc = false;
            for (int tries = 0; tries < 8 && want > (long long) (sampleRate * 5); ++tries)
            {
                try { src.setSize (channels, (int) want); alloc = true; break; }
                catch (...) { araLog ("readSource: alloc failed at " + juce::String (want) + ", halving"); want /= 2; }
            }
            if (! alloc) { try { src.setSize (channels, (int) juce::jmax ((long long) 1, want)); } catch (...) { return false; } }
            n = (int) want;
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
            ss.sourceSampleRate.store (sampleRate);

            // Keep a copy of the source for the Bypass A/B monitor.
            {
                auto sp = std::make_shared<tonefill::plugin::SessionState::FillBuffer>();
                sp->resize ((std::size_t) effCh);
                for (int ch = 0; ch < effCh; ++ch)
                    (*sp)[(std::size_t) ch].assign (learnSrc.getReadPointer (ch), learnSrc.getReadPointer (ch) + n);
                ss.setSourcePreview (std::move (sp), sampleRate);
            }
            araLog ("readSource: effCh=" + juce::String (effCh) + " (src ch=" + juce::String (channels) + ")");
            return true;
        };

        bool lastWhole = ss.wholeFile.load();
        // Default = first 10 min (was 4); Full = the whole item, memory-bounded. Quality over speed.
        if (! readSource (lastWhole ? 1.0e9 : 600.0) || threadShouldExit()) return;

        tonefill::core::DiagnosticsLogger diag;
        engine::analysis::AnalysisSession session (diag);
        std::atomic<bool> cancel { false };

        // (Re)analyze with the current Threshold; updates the UI status. Re-runs when Threshold
        // moves (it changes WHICH source material is learned, so it needs fresh analysis).
        auto analyze = [&] (float threshold, const juce::AudioBuffer<float>& learnInput, bool useManual,
                            bool updateStatus = true)
            -> engine::model::AmbienceModelPtr
        {
            engine::analysis::AnalysisContext ctx;
            ctx.leftContext.makeCopyOf (learnInput);
            ctx.rightContext.makeCopyOf (learnInput);
            ctx.analysisSampleRate = sampleRate;
            ctx.numChannels = juce::jmax (1, learnInput.getNumChannels()); // 1 for a spectral band pass
            ctx.leftEnabled = true;
            ctx.useManualSelection = useManual;
            ctx.cleanThreshold = threshold;
            ctx.speechReject = ss.speechReject.load();
            ctx.flatness = ss.flatness.load();
            ctx.minFillSeconds = ss.minFill.load();
            ctx.statisticalSelection = ss.statisticalMode.load();
            ctx.sourceContentHash = (std::uint64_t) n;
            if (updateStatus) ss.phase.store (1);
            auto r = session.run (ctx, cancel);
            if (! r.ok()) { if (updateStatus) ss.phase.store (0); return nullptr; }
            const auto m = r.value();
            if (! updateStatus) return m; // per-band spectral pass: audio only, don't touch the UI
            const int nP = m->tonalPerChannel.empty() ? 0 : (int) m->tonalPerChannel[0].partials.size();
            const float lvl = (m->noisePerChannel.empty() || m->noisePerChannel[0].targetRms <= 0.0f)
                                  ? -120.0f : 20.0f * std::log10 (m->noisePerChannel[0].targetRms);
            ss.numPartials.store (nP);
            ss.learnSeconds.store (m->learnMaterialSeconds);
            ss.cleanChunks.store ((int) m->cleanRanges.size());
            ss.availSeconds.store (m->availableCleanSeconds);
            ss.seamRiskDb.store (m->joinRoughnessDb);
            ss.levelDb.store (lvl);
            ss.phase.store (2);
            araLog ("analyze: thr=" + juce::String (threshold) + " partials=" + juce::String (nP)
                    + " learnSec=" + juce::String (m->learnMaterialSeconds) + " levelDb=" + juce::String (lvl));

            // Waveform for the UI: peak per bin + clean flag. Higher resolution now so the large
            // waveform window can zoom in and still show detail.
            const int bins = 2000;
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
        juce::AudioBuffer<float> learnKept; // the audio the model was built from (for the spectral path)
        float lastThreshold = -1.0f, lastSpeech = -1.0f, lastFlat = -1.0f, lastMinFill = -1.0f;
        int lastGen = -1, lastManualGen = -1;
        bool lastManual = false, lastStat = false;

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
                readSource (whole ? 1.0e9 : 600.0);
                lastWhole = whole;
                model = nullptr; // force re-analyze with the new window
            }

            // Re-analyze when an analysis input changes: Threshold, Speech Reject, Flatness, Min
            // Fill, manual on/off, or the manual selection itself.
            const float flat = ss.flatness.load();
            const float minf = ss.minFill.load();
            const bool stat = ss.statisticalMode.load();
            if (model == nullptr || std::abs (thr - lastThreshold) > 1.0e-4f
                || std::abs (spk - lastSpeech) > 1.0e-4f || std::abs (flat - lastFlat) > 1.0e-4f
                || std::abs (minf - lastMinFill) > 1.0e-4f || manual != lastManual || mGen != lastManualGen
                || stat != lastStat)
            {
                const auto ranges = manual ? ss.getManualRanges() : std::vector<std::pair<int, int>>{};
                const bool useManual = manual && ! ranges.empty();
                const juce::AudioBuffer<float> learnInput = useManual ? buildManual (ranges) : learnSrc;
                learnKept.makeCopyOf (learnInput); // spectral path re-reads this to band-split
                model = analyze (thr, learnInput, useManual);
                lastThreshold = thr; lastSpeech = spk; lastFlat = flat; lastMinFill = minf;
                lastManual = manual; lastManualGen = mGen; lastStat = stat;
                needRender = true;
            }
            if (gen != lastGen) { lastGen = gen; needRender = true; }

            if (needRender && model != nullptr)
            {
                engine::model::RenderSettings s;
                s.mode = engine::model::Mode::Ambience; // only exposed mode
                s.paulStretch = ss.paulStretch.load();  // Enhance
                // Chunk Size was removed from the UI: the grain-join machinery (join cost, zero-cross
                // anchoring, Hann overlap, anti-repeat) hides grain boundaries, so grain length was
                // inaudible for stationary room tone, and grainCloud caps it to srcLen/4 anyway. Pin a
                // sensible length; grainCloud still adapts it down for short captures.
                s.fragmentMs = 700.0f;
                s.blendFrac  = 0.05f + ss.blend.load() * 0.45f;          // 5..50 %
                s.randomness = ss.randomness.load();
                s.seed = ss.seed.load();
                // Loop length is DECOUPLED from Smoothness (S7.1). It now scales with how much clean
                // material we actually have: more source -> a longer loop before the exact repeat, so
                // the "looped" feel dissolves on rich captures while short ones stay compact. Turning
                // Smoothness no longer changes the loop length.
                const double availSec = (double) model->learnMaterialSeconds;
                // Quality over speed: allow a longer non-repeating loop when there's material for it
                // (scales with clean seconds, so short captures still stay compact). Was capped at 60 s.
                const double loopSec  = juce::jlimit (15.0, 120.0, availSec * 4.0);
                const int loopLen = (int) (loopSec * sampleRate);
                const int xf = (int) (0.25 * sampleRate); // 250 ms seamless-loop crossfade
                s.targetSampleRate = sampleRate;
                s.targetChannels = effCh;
                s.targetDurationSamples = (long long) (loopLen + xf);

                std::vector<std::vector<float>> chans;
                bool haveChans = false;

                if (ss.spectralMode.load())
                {
                    // Spectral Mosaic: split the analysed source into Linkwitz-Riley bands and select
                    // + synthesize each band independently, then sum. Room tone is near-stationary
                    // per band, so per-band loops are steadier than one broadband loop, and each band
                    // (especially the low one, where a room shift is most audible) gets its own clean
                    // selection instead of needing every band clean at the same instant.
                    // Band count is user-set (the "Bands" knob): fewer = wider bands, more = narrower.
                    // Edges are geometric across 150..8000 Hz (default 7 bands ~ the standard split),
                    // with the sub-150 and >8000 anchor bands kept at the ends.
                    const int nBands = juce::jlimit (3, 12, ss.spectralBands.load());
                    std::vector<double> edges;
                    {
                        const int nEdges = nBands - 1; // 150 and 8000 are the first/last edges
                        const double lo = 150.0, hi = 8000.0;
                        for (int e = 0; e < nEdges; ++e)
                        {
                            const double t = nEdges > 1 ? (double) e / (double) (nEdges - 1) : 0.0;
                            edges.push_back (lo * std::pow (hi / lo, t));
                        }
                    }
                    const int outN = (int) s.targetDurationSamples;
                    const int srcN = learnKept.getNumSamples();
                    if (srcN > 0 && outN > 0)
                    {
                        // Mono-collapse the analysed source, then band-split it.
                        std::vector<float> mono ((std::size_t) srcN, 0.0f);
                        const int lc = juce::jmax (1, learnKept.getNumChannels());
                        for (int c = 0; c < lc; ++c)
                        {
                            const float* p = learnKept.getReadPointer (c);
                            for (int i = 0; i < srcN; ++i) mono[(std::size_t) i] += p[i];
                        }
                        if (lc > 1) for (auto& v : mono) v /= (float) lc;

                        auto bands = tonefill::dsp::splitBands (mono.data(), srcN, sampleRate, edges);
                        std::vector<float> accMono ((std::size_t) outN, 0.0f);

                        // Target timbre = the CLEAN room tone's spectral balance, measured full-band on
                        // the audio the broadband analysis already selected -- NOT the whole source's
                        // band energy, which is dominated by dialogue in the mid bands and would pump
                        // those bands up (too loud / wrong colour). Split the clean reference into the
                        // same bands and match each synthesized band to its clean-reference level.
                        std::vector<float> targetBandRms ((std::size_t) nBands, 0.0f);
                        bool haveCleanRef = false;
                        {
                            const auto& cap = model->cleanAudioPerChannel;
                            if (! cap.empty() && ! cap[0].empty())
                            {
                                const int cn = (int) cap[0].size();
                                std::vector<float> cleanMono ((std::size_t) cn, 0.0f);
                                for (const auto& cc : cap)
                                    for (int i = 0; i < juce::jmin (cn, (int) cc.size()); ++i)
                                        cleanMono[(std::size_t) i] += cc[(std::size_t) i];
                                if (cap.size() > 1) for (auto& v : cleanMono) v /= (float) cap.size();

                                auto cleanBands = tonefill::dsp::splitBands (cleanMono.data(), cn, sampleRate, edges);
                                for (std::size_t bi = 0; bi < cleanBands.size() && bi < targetBandRms.size(); ++bi)
                                {
                                    double e = 0.0; for (float v : cleanBands[bi]) e += (double) v * v;
                                    targetBandRms[bi] = (float) std::sqrt (e / juce::jmax (1, (int) cleanBands[bi].size()));
                                }
                                haveCleanRef = true;
                            }
                        }

                        for (std::size_t bi = 0; bi < bands.size() && ! cancel.load(); ++bi)
                        {
                            const auto& band = bands[bi];

                            // Gain target: the clean room tone's level in this band. Fall back to the
                            // whole-band level only if no clean reference was found.
                            float target = (bi < targetBandRms.size()) ? targetBandRms[bi] : 0.0f;
                            if (! haveCleanRef)
                            {
                                double se = 0.0; for (float v : band) se += (double) v * v;
                                target = (float) std::sqrt (se / juce::jmax (1, srcN));
                            }
                            if (target < 1.0e-7f) continue; // this band is silent in the room tone

                            juce::AudioBuffer<float> bandBuf (1, srcN);
                            std::copy (band.begin(), band.end(), bandBuf.getWritePointer (0));
                            auto bandModel = analyze (thr, bandBuf, false, /*updateStatus*/ false);
                            if (bandModel == nullptr) continue;

                            engine::model::RenderSettings sb = s;
                            sb.targetChannels = 1;
                            sb.seed = s.seed ^ (0x9E3779B97F4A7C15ULL * (std::uint64_t) (bi + 1));
                            auto br = renderer.render (*bandModel, sb, cancel);
                            if (! br.ok() || br.value().channels.empty()) continue;
                            const auto& bc = br.value().channels[0];

                            // Scale the synthesized band to the clean-reference band level, then sum ->
                            // the composite reproduces the room tone's spectral balance (LR bands sum flat).
                            double oe = 0.0; for (float v : bc) oe += (double) v * v;
                            const float outRms = (float) std::sqrt (oe / juce::jmax (1, (int) bc.size()));
                            const float g = outRms > 1.0e-9f ? target / outRms : 0.0f;
                            const int m2 = juce::jmin (outN, (int) bc.size());
                            for (int i = 0; i < m2; ++i) accMono[(std::size_t) i] += bc[(std::size_t) i] * g;
                        }

                        if (! cancel.load()) { chans.assign ((std::size_t) effCh, accMono); haveChans = true; }
                    }
                    araLog ("spectral render: bands=" + juce::String ((int) edges.size() + 1)
                            + " outN=" + juce::String (outN));
                }
                else
                {
                    auto out = renderer.render (*model, s, cancel);
                    if (out.ok()) { chans = std::move (out.value().channels); haveChans = true; }
                }

                if (haveChans)
                {
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
    : juce::ARAPlaybackRenderer (dc), lockInterface (lock), documentController_ (dc)
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
                // Publish to the state the document controller shares for this source, so the
                // editor sees us even when the host put it on a different instance (the usual
                // case outside Reaper). Falls back to whatever didBindToARA handed us.
                if (auto* dc = specialisedDocumentController (documentController_))
                    if (auto shared = dc->stateForSource (source))
                        state_ = std::move (shared);

                if (state_ == nullptr)
                    break;

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

    if (! positionInfo.getIsPlaying())
        return true;

    // Bypass A/B: play the source instead of the fill.
    if (state_ != nullptr && state_->bypass.load())
    {
        double psr = sampleRate;
        if (auto sp = state_->getSourcePreview (psr))
            tileSourcePreview (buffer, sampleRate, getPlaybackRegions(), positionInfo, *sp);
        return true;
    }

    if (! fillReady.load (std::memory_order_acquire))
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

    // Enhance-only HISS FILTER: a live low-pass that removes the HF hiss PaulStretch adds. Only when
    // Enhance AND Hiss Filter are on. Coefficients are rebuilt only when the knobs move.
    if (state_ != nullptr && state_->hissFilter.load() && state_->paulStretch.load())
    {
        const int nCh = buffer.getNumChannels();
        if ((int) hissFilters_.size() < nCh) hissFilters_.resize ((std::size_t) nCh);
        const float freq = juce::jlimit (3000.0f, 15000.0f, state_->hissFreq.load());
        const float q    = juce::jlimit (0.3f, 2.0f, state_->hissQ.load());
        if (std::abs (freq - hissLastFreq_) > 0.5f || std::abs (q - hissLastQ_) > 1.0e-3f)
        {
            const auto co = juce::IIRCoefficients::makeLowPass (sampleRate, freq, q);
            for (auto& f : hissFilters_) f.setCoefficients (co);
            hissLastFreq_ = freq; hissLastQ_ = q;
        }
        for (int ch = 0; ch < nCh; ++ch)
            hissFilters_[(std::size_t) ch].processSamples (buffer.getWritePointer (ch), numSamples);
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

//==============================================================================
ToneFillEditorRenderer::ToneFillEditorRenderer (ARA::PlugIn::DocumentController* dc,
                                                ProcessingLockInterface& lock)
    : juce::ARAEditorRenderer (dc), lockInterface (lock), documentController_ (dc)
{
}

void ToneFillEditorRenderer::prepareToPlay (double sampleRateIn, int, int,
                                            juce::AudioProcessor::ProcessingPrecision,
                                            AlwaysNonRealtime)
{
    sampleRate = sampleRateIn;
    araLog ("editorRenderer prepareToPlay: regions=" + juce::String ((int) getPlaybackRegions().size()));
}

bool ToneFillEditorRenderer::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::AudioProcessor::Realtime,
                                           const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    const auto lock = lockInterface.getProcessingLock();
    if (! lock.isLocked())
        return true;

    // Resolve the shared per-source state lazily (the region attaches after binding).
    if (state_ == nullptr)
        if (auto* dc = specialisedDocumentController (documentController_))
            for (const auto region : getPlaybackRegions())
                if (auto* mod = region->getAudioModification())
                    if (auto* src = mod->getAudioSource())
                        { state_ = dc->stateForSource (src); break; }

    buffer.clear(); // replace the source: silence until the fill exists, never pass the source through
    if (state_ == nullptr || ! positionInfo.getIsPlaying())
        return true;

    // Bypass A/B: play the source instead of the fill.
    if (state_->bypass.load())
    {
        double psr = sampleRate;
        if (auto sp = state_->getSourcePreview (psr))
            tileSourcePreview (buffer, sampleRate, getPlaybackRegions(), positionInfo, *sp);
        return true;
    }

    double fillSr = sampleRate;
    auto fill = state_->getExportFill (fillSr); // same loop the playback renderer publishes
    if (fill == nullptr || fill->empty() || (*fill)[0].empty())
        return true;

    const long long len = (long long) (*fill)[0].size();
    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples().orFallback (0);
    const auto blockRange = juce::Range<juce::int64>::withStartAndLength (timeInSamples, (juce::int64) numSamples);

    for (const auto region : getPlaybackRegions())
    {
        const auto songRange = region->getSampleRange (sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
        const auto renderRange = blockRange.getIntersectionWith (songRange);
        if (renderRange.isEmpty())
            continue;

        const int startInBuffer = (int) (renderRange.getStart() - blockRange.getStart());
        const int count = (int) renderRange.getLength();
        const auto regionStart = songRange.getStart();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto& srcCh = (*fill)[(std::size_t) juce::jmin (ch, (int) fill->size() - 1)];
            auto* dst = buffer.getWritePointer (ch);
            for (int i = 0; i < count; ++i)
            {
                long long fpos = (renderRange.getStart() + i - regionStart) % len;
                if (fpos < 0) fpos += len;
                dst[startInBuffer + i] = srcCh[(std::size_t) fpos];
            }
        }
    }

    // Live Output gain (Normalize bakes its gain into the published loop; unity when it's on).
    const float gain = (! state_->normalizeEnabled.load()) ? state_->outputGain.load() : 1.0f;
    if (std::fabs (gain - 1.0f) > 1.0e-4f)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch), gain, numSamples);

    return true;
}
} // namespace tonefill::plugin::ara

#endif // TONEFILL_ARA_AVAILABLE
