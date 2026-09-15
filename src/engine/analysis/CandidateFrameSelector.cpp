#include "engine/analysis/CandidateFrameSelector.h"

#include "dsp/Stft.h"
#include "dsp/Features.h"
#include "dsp/VoiceDetect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tonefill::engine::analysis
{
juce::AudioBuffer<float>
CandidateFrameSelector::selectLearnBuffer (const juce::AudioBuffer<float>& src, double sr,
                                           const Params& p,
                                           std::vector<std::pair<int, int>>* rangesOut,
                                           float* availSecOut,
                                           float* roughnessOut) const
{
    if (rangesOut != nullptr) rangesOut->clear();
    const int N = p.frameSize, H = p.hopSize;
    const int numCh = src.getNumChannels();
    const int len = src.getNumSamples();
    if (numCh <= 0 || len < 4 * N) { juce::AudioBuffer<float> c; c.makeCopyOf (src); return c; }

    const int numFrames = (len - N) / H + 1;
    const int bins = N / 2 + 1;
    const double binHz = sr / (double) N;
    const float eps = 1.0e-9f;
    const float srj = p.speechReject;

    // Per-frame features (averaged across channels). rms/flat/flux/crest/lowBand from
    // computeFrameFeatures; band RMS (bb/mid/hi) from the magnitude spectrum for tilt + join cost.
    std::vector<float> rms (numFrames, 0.0f), flat (numFrames, 0.0f), flux (numFrames, 0.0f),
                       crest (numFrames, 0.0f), lowBand (numFrames, 0.0f),
                       bbR (numFrames, 0.0f), midR (numFrames, 0.0f), hiR (numFrames, 0.0f);

    const int kLow = juce::jmax (1, (int) (300.0 / binHz));        // 300 Hz
    const int kHi  = juce::jmax (kLow + 1, (int) (2000.0 / binHz)); // 2 kHz
    const float invCh = 1.0f / (float) numCh;

    for (int ch = 0; ch < numCh; ++ch)
    {
        dsp::Stft stft (dsp::StftConfig { N, H, dsp::WindowType::Hann });
        std::vector<float> mag (bins), phase (bins), prevMag (bins, 0.0f);
        const float* data = src.getReadPointer (ch);

        for (int f = 0; f < numFrames; ++f)
        {
            stft.analyze (data, len, f * H, mag, phase);
            const auto& raw = stft.lastRawFrame();
            const auto feat = dsp::computeFrameFeatures (raw.data(), (int) raw.size(),
                                                         mag.data(), prevMag.data(), bins, binHz);
            std::copy (mag.begin(), mag.end(), prevMag.begin()); // copy-back for real flux next frame

            double eLo = 0.0, eMi = 0.0, eHi = 0.0;
            for (int k = 0; k < bins; ++k)
            {
                const double e = (double) mag[(std::size_t) k] * mag[(std::size_t) k];
                if (k < kLow) eLo += e; else if (k < kHi) eMi += e; else eHi += e;
            }
            const float bb  = (float) std::sqrt (eLo + eMi + eHi);
            const float mid = (float) std::sqrt (eMi + eHi); // energy above 300 Hz (~Classic HP300)
            const float hi  = (float) std::sqrt (eHi);       // energy above 2 kHz (~Classic HP2k)

            const auto uf = (std::size_t) f;
            rms[uf]     += feat.rms          * invCh;
            flat[uf]    += feat.flatness     * invCh;
            flux[uf]    += feat.flux         * invCh;
            crest[uf]   += feat.crestDb      * invCh;
            lowBand[uf] += feat.lowBandRatio * invCh;
            bbR[uf]     += bb  * invCh;
            midR[uf]    += mid * invCh;
            hiR[uf]     += hi  * invCh;
        }
    }

    // Speech probability per frame: HP-300 copy built ONCE per channel, libfvad, averaged.
    const int vadMode = juce::jlimit (0, 3, (int) std::lround ((1.0f - srj) * 3.0f));
    std::vector<float> speechProb (numFrames, 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        std::vector<float> hp (src.getReadPointer (ch), src.getReadPointer (ch) + len);
        juce::IIRFilter f; f.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 300.0));
        f.processSamples (hp.data(), len);
        const auto voiced = dsp::detectVoice (hp.data(), len, sr, vadMode);
        if (voiced.empty()) continue;
        const double vs = (double) voiced.size();
        for (int fr = 0; fr < numFrames; ++fr)
        {
            const int s = fr * H, e = juce::jmin (len, s + N);
            int cnt = 0, on = 0;
            for (int i = s; i < e; ++i)
            {
                const int vi = juce::jlimit (0, (int) voiced.size() - 1, (int) ((double) i * vs / (double) len));
                on += voiced[(std::size_t) vi] ? 1 : 0; ++cnt;
            }
            if (cnt > 0) speechProb[(std::size_t) fr] += (float) on / (float) cnt * invCh;
        }
    }

    // Colour reference: median tilt of the quietest ~30% of frames (INDEPENDENT of the knobs), so
    // the "dominant colour" never shifts when a knob moves - the fix that keeps selection monotonic.
    auto tiltOf = [&] (int f, float& a, float& b)
    {
        a = std::log (midR[(std::size_t) f] + eps) - std::log (bbR[(std::size_t) f] + eps);
        b = std::log (hiR[(std::size_t) f] + eps)  - std::log (midR[(std::size_t) f] + eps);
    };
    float cT1 = 0, cT2 = 0, sT1 = 1, sT2 = 1; bool haveCluster = false;
    {
        std::vector<float> lv (rms.begin(), rms.end());
        std::sort (lv.begin(), lv.end());
        const float refFloor = lv.empty() ? 1.0e9f : lv[(std::size_t) (lv.size() * 3 / 10)];
        std::vector<float> T1, T2;
        for (int f = 0; f < numFrames; ++f)
            if (rms[(std::size_t) f] <= refFloor) { float a, b; tiltOf (f, a, b); T1.push_back (a); T2.push_back (b); }
        if (T1.size() >= 8)
        {
            auto median = [] (std::vector<float> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
            auto mad = [] (const std::vector<float>& v, float m)
            { std::vector<float> d; for (float x : v) d.push_back (std::fabs (x - m)); std::sort (d.begin(), d.end());
              return juce::jmax (1.0e-3f, d[d.size() / 2]); };
            cT1 = median (T1); cT2 = median (T2); sT1 = mad (T1, cT1); sT2 = mad (T2, cT2);
            haveCluster = true;
        }
    }
    const float clusterK = 3.5f - srj * 2.0f;

    // Per-run references for the level + stationarity scores.
    float floorRms = 0.0f;
    {
        std::vector<float> lv (rms.begin(), rms.end());
        std::sort (lv.begin(), lv.end());
        floorRms = lv.empty() ? 0.0f : lv[(std::size_t) (lv.size() / 10)]; // 10th percentile
    }
    float refFlux = 1.0e-6f;
    {
        std::vector<int> idx (numFrames); for (int i = 0; i < numFrames; ++i) idx[(std::size_t) i] = i;
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return rms[(std::size_t) a] < rms[(std::size_t) b]; });
        const int q = juce::jmax (1, numFrames / 4);
        std::vector<float> lf; for (int i = 0; i < q; ++i) lf.push_back (flux[(std::size_t) idx[(std::size_t) i]]);
        std::sort (lf.begin(), lf.end());
        if (! lf.empty()) refFlux = juce::jmax (1.0e-6f, lf[lf.size() / 2]);
    }

    // Weighted score per frame (normalised by the weight sum) + soft lowBand penalty + hard gates.
    const float sumW = p.wFlatness + p.wStationarity + p.wProximity + p.wSpeech + p.wTransient + p.wColour;
    const float minScore = 0.35f + p.flatness * 0.25f;      // Flatness raises the acceptance bar
    const float proxDen  = 2.0f + p.cleanThreshold * 6.0f;   // Clean Level widens level tolerance
    const float speechThresh = 0.60f - srj * 0.35f;          // Voice Reject lowers the speech bar
    std::vector<char> accept (numFrames, 0);
    std::vector<char> speechD (numFrames, 0);

    for (int f = 0; f < numFrames; ++f)
    {
        const auto uf = (std::size_t) f;
        const float sFlat = juce::jlimit (0.0f, 1.0f, flat[uf] * 1.2f);
        const float ratioFlux = flux[uf] / refFlux;
        const float sStat = juce::jlimit (0.0f, 1.0f, 1.0f - (ratioFlux - 1.0f) * 0.5f);
        const float ratioLvl = floorRms > eps ? rms[uf] / floorRms : 1.0f;
        const float sProx = juce::jlimit (0.0f, 1.0f, 1.0f - (ratioLvl - 1.0f) / proxDen);
        const float sSpeech = juce::jlimit (0.0f, 1.0f, 1.0f - speechProb[uf]);
        const float sTrans = juce::jlimit (0.0f, 1.0f, 1.0f - (crest[uf] - 6.0f) / 12.0f);
        float sColour = 1.0f;
        if (haveCluster)
        {
            float a, b; tiltOf (f, a, b);
            const float dist = 0.5f * (std::fabs (a - cT1) / sT1 + std::fabs (b - cT2) / sT2);
            sColour = juce::jlimit (0.0f, 1.0f, 1.0f - dist / juce::jmax (0.5f, clusterK));
        }

        float score = (p.wFlatness * sFlat + p.wStationarity * sStat + p.wProximity * sProx
                       + p.wSpeech * sSpeech + p.wTransient * sTrans + p.wColour * sColour) / sumW;
        // Soft lowBand penalty (hum/voice suspect) instead of a hard reject - LF-dominated beds
        // (HVAC, traffic) are legitimate room tone, so only DISCOUNT them.
        score *= 1.0f - 0.5f * juce::jlimit (0.0f, 1.0f, (lowBand[uf] - 0.55f) / 0.45f);

        speechD[uf] = (speechProb[uf] > speechThresh) ? 1 : 0;
        accept[uf] = (score >= minScore && crest[uf] <= p.hardRejectCrestDb && ! speechD[uf]) ? 1 : 0;
    }

    // Dilate the speech mask (remove onsets/tails), then hard-clear accepted speech-adjacent frames.
    const int guard = (int) std::lround (srj * 3.0f);
    if (guard > 0)
    {
        std::vector<char> sd (speechD);
        for (int f = 0; f < numFrames; ++f)
            if (sd[(std::size_t) f])
                for (int k = juce::jmax (0, f - guard); k <= juce::jmin (numFrames - 1, f + guard); ++k)
                    speechD[(std::size_t) k] = 1;
        for (int f = 0; f < numFrames; ++f) if (speechD[(std::size_t) f]) accept[(std::size_t) f] = 0;
    }

    // Bridge SMALL non-speech holes so scoring flicker doesn't shred a clean stretch (mirrors Classic).
    {
        const int maxHole = juce::jmax (1, (int) (0.15 * sr / H));
        int f = 0;
        while (f < numFrames)
        {
            if (accept[(std::size_t) f]) { ++f; continue; }
            int g = f; bool hasSpeech = false;
            while (g < numFrames && ! accept[(std::size_t) g]) { if (speechD[(std::size_t) g]) hasSpeech = true; ++g; }
            const bool bounded = (f > 0 && accept[(std::size_t) (f - 1)]) && (g < numFrames);
            if (bounded && ! hasSpeech && (g - f) <= maxHole)
                for (int k = f; k < g; ++k) accept[(std::size_t) k] = 1;
            f = g;
        }
    }

    if (availSecOut != nullptr)
    {
        int cnt = 0; for (int f = 0; f < numFrames; ++f) if (accept[(std::size_t) f]) ++cnt;
        *availSecOut = (float) (cnt * H / sr);
    }

    // Min Fill = direct floor on contiguous accepted runs; guarantee at least the longest.
    std::vector<std::pair<int, int>> allRuns;
    {
        int runStart = -1;
        for (int f = 0; f <= numFrames; ++f)
        {
            const bool ok = (f < numFrames) && accept[(std::size_t) f];
            if (ok && runStart < 0) runStart = f;
            else if (! ok && runStart >= 0) { if (f - runStart >= 2) allRuns.push_back ({ runStart, f - 1 }); runStart = -1; }
        }
    }
    auto runLenS = [&] (const std::pair<int, int>& r)
    { return juce::jmin ((long long) ((r.second - r.first) * H + N), (long long) (len - r.first * H)); };
    const long long floorS = (long long) (p.minFillSeconds * (float) sr);
    std::vector<std::pair<int, int>> runs;
    for (const auto& r : allRuns) if (runLenS (r) >= floorS) runs.push_back (r);
    if (runs.empty() && ! allRuns.empty())
        runs.push_back (*std::max_element (allRuns.begin(), allRuns.end(),
            [&] (const std::pair<int, int>& a, const std::pair<int, int>& b) { return runLenS (a) < runLenS (b); }));

    long long total = 0; for (const auto& r : runs) total += runLenS (r);
    if (runs.empty() || total < (long long) (0.05 * sr)) { juce::AudioBuffer<float> c; c.makeCopyOf (src); return c; }

    if (rangesOut != nullptr)
        for (const auto& r : runs) rangesOut->push_back ({ r.first * H, juce::jmin (r.second * H + N, len) });

    // Adaptive join crossfade from the per-frame band dB mismatch (An4), + mean mismatch = seam risk.
    auto bandDbAt = [&] (int frame, float& lb, float& lm, float& lh)
    {
        const int a = juce::jlimit (0, numFrames - 1, frame - 2), b = juce::jlimit (0, numFrames - 1, frame + 2);
        double sb = 0, sm = 0, sh = 0; int c = 0;
        for (int k = a; k <= b; ++k) { sb += bbR[(std::size_t) k]; sm += midR[(std::size_t) k]; sh += hiR[(std::size_t) k]; ++c; }
        const double inv = 1.0 / (double) juce::jmax (1, c);
        lb = 20.0f * std::log10 ((float) (sb * inv) + eps);
        lm = 20.0f * std::log10 ((float) (sm * inv) + eps);
        lh = 20.0f * std::log10 ((float) (sh * inv) + eps);
    };
    auto joinMismatchDb = [&] (const std::pair<int, int>& pr, const std::pair<int, int>& nx)
    {
        float pb, pm, ph, nb, nm, nh; bandDbAt (pr.second - 1, pb, pm, ph); bandDbAt (nx.first, nb, nm, nh);
        return std::sqrt (((pb - nb) * (pb - nb) + (pm - nm) * (pm - nm) + (ph - nh) * (ph - nh)) / 3.0f);
    };

    const float halfPi = 1.5707963267948966f;
    juce::AudioBuffer<float> clean (numCh, (int) total);
    int w = 0; double roughAcc = 0.0; int roughCnt = 0;
    for (std::size_t ri = 0; ri < runs.size(); ++ri)
    {
        const int s = runs[ri].first * H;
        const int span = juce::jmin ((runs[ri].second - runs[ri].first) * H + N, len - s);
        if (w == 0) { for (int ch = 0; ch < numCh; ++ch) clean.copyFrom (ch, 0, src, ch, s, span); w = span; }
        else
        {
            const float mismatchDb = joinMismatchDb (runs[ri - 1], runs[ri]);
            roughAcc += mismatchDb; ++roughCnt;
            const float t = juce::jlimit (0.0f, 1.0f, mismatchDb / 6.0f);
            const int joinXf = (int) ((0.020f + t * 0.100f) * (float) sr);
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
    if (roughnessOut != nullptr) *roughnessOut = roughCnt > 0 ? (float) (roughAcc / (double) roughCnt) : 0.0f;
    return clean;
}
} // namespace tonefill::engine::analysis
