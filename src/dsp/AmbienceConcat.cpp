#include "dsp/AmbienceConcat.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace tonefill::dsp
{
void concatenateAmbience (float* out, int n,
                          const float* src, int srcLen,
                          int fragLen, int xfadeLen,
                          SeededRng& rng, int antiRepeat, float variation)
{
    if (n <= 0 || srcLen <= 0) return;
    std::fill (out, out + n, 0.0f);

    xfadeLen = std::max (1, std::min (xfadeLen, fragLen / 2));

    // Source too short to draw distinct fragments: loop it with a short crossfade at the seam.
    if (srcLen < fragLen + xfadeLen)
    {
        for (int i = 0; i < n; ++i) out[i] = src[i % srcLen];
        return;
    }

    const int maxStart = srcLen - fragLen;
    const int advance  = fragLen - xfadeLen;
    const float halfPi = 1.5707963267948966f;

    std::deque<int> recent;
    int pos = 0;
    bool first = true;

    while (pos < n)
    {
        int start = 0;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            start = (int) (rng.nextFloat() * (float) maxStart);
            bool tooClose = false;
            for (int r : recent)
                if (std::abs (start - r) < fragLen / 2) { tooClose = true; break; }
            if (! tooClose) break;
        }
        recent.push_back (start);
        if ((int) recent.size() > antiRepeat) recent.pop_front();

        // Occasionally play the fragment reversed: room tone is ~time-symmetric, so this adds
        // variety (breaks audible repetition) without needing more source material.
        const bool reverse = rng.nextFloat() < 0.5f;

        // Per-fragment level jitter (±~2.5 dB at variation=1): even reused material reads as
        // "different" each time, which dissolves the looped feel on short source samples.
        const float jitterDb = (rng.nextFloat() * 2.0f - 1.0f) * variation * 2.5f;
        const float fragGain = std::pow (10.0f, jitterDb / 20.0f);

        for (int i = 0; i < fragLen; ++i)
        {
            const int o = pos + i;
            if (o >= n) break;
            const float s = src[start + (reverse ? (fragLen - 1 - i) : i)] * fragGain;
            if (i < xfadeLen && ! first)
            {
                // Equal-power blend with the previous fragment's tail already in `out`.
                const float g = (float) i / (float) (xfadeLen - 1) * halfPi;
                out[o] = out[o] * std::cos (g) + s * std::sin (g);
            }
            else
            {
                out[o] = s;
            }
        }

        pos += advance;
        first = false;
    }
}

void grainCloud (float* out, int n,
                 const float* src, int srcLen,
                 int grainLen, int density,
                 SeededRng& rng, int antiRepeat, float variation,
                 const float* lfProf, int lfProfLen)
{
    if (n <= 0 || srcLen <= 0) return;
    std::fill (out, out + n, 0.0f);

    // Grain must be well SHORTER than the source so the cloud overlaps DIFFERENT material instead
    // of the same near-whole buffer shifted by a little (which combs into a metallic "noise" when
    // only a second or two of clean tone was captured). Cap grain to ~1/4 of the source, and cap
    // density to the number of distinct grain slots so we don't self-overlap.
    grainLen = std::max (256, std::min (grainLen, std::max (256, srcLen / 4)));
    const int slots = std::max (1, srcLen / std::max (1, grainLen));
    density  = std::max (1, std::min (density, std::max (2, slots)));
    const int hop = std::max (1, grainLen / density);
    const int maxStart = std::max (1, srcLen - grainLen);
    const float twoPi = 6.2831853071795864f;

    // Hann window (precomputed).
    std::vector<float> win ((std::size_t) grainLen);
    for (int i = 0; i < grainLen; ++i)
        win[(std::size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) (grainLen - 1));

    // #4 time-based anti-repeat: each recent grain is remembered with the OUTPUT position it was
    // placed at, and a source region may not recur until `recencyOut` output samples have passed
    // (not merely N grains ago). This stops the same excerpts from cycling back audibly on long
    // renders, where a count-based memory is too shallow.
    std::deque<std::pair<int, int>> recent; // (source start, output pos)
    const int recencyOut = std::max (srcLen, grainLen * std::max (1, antiRepeat));
    const int jitter = (int) (variation * (float) hop); // small grid jitter for less regularity

    // A2 (context-aware selection): compare window and candidate count. Instead of a purely random
    // start we draw a handful of candidates and keep the one whose HEAD best continues the previous
    // grain's source TAIL (min sum-of-squared-difference over `cw` samples). Both excerpts come from
    // `src` at full scale, so the SSD is a clean level+shape join cost - it favours grains that
    // begin like the last one ended, smoothing the overlap-add seams.
    const int cw = std::max (32, std::min (grainLen / 4, 512));
    const int kCandidates = 16;
    // A3 (zero-cross anchoring): snap the chosen start onto the nearest rising zero crossing within
    // a small window, so the grain begins near zero and the Hann taper has less to hide.
    const int snapWin = std::max (8, cw / 4);
    auto snapToZeroCross = [&] (int s) -> int
    {
        int best = s, bestDist = snapWin + 1;
        const int lo = std::max (1, s - snapWin), hi = std::min (maxStart, s + snapWin);
        for (int k = lo; k <= hi; ++k)
            if (src[k - 1] <= 0.0f && src[k] > 0.0f)
            {
                const int d = std::abs (k - s);
                if (d < bestDist) { bestDist = d; best = k; }
            }
        return best;
    };

    int prevStart = -1; // source start of the last placed grain (for the join-cost tail)

    for (int pos = -grainLen + hop; pos < n; pos += hop)
    {
        const int here = std::max (0, pos);
        while (! recent.empty() && recent.front().second < here - recencyOut) recent.pop_front();

        // Draw candidates and pick the lowest join-cost one that is NOT recently used; if every
        // candidate is recent (small source), fall back to the lowest-cost candidate overall.
        float bestFresh = 1.0e30f; int freshStart = -1;
        float bestAny   = 1.0e30f; int anyStart   = 0;
        for (int attempt = 0; attempt < kCandidates; ++attempt)
        {
            const int cand = (int) (rng.nextFloat() * (float) maxStart);

            // Join cost: SSD of the previous grain's tail against this candidate's head (0 -> random
            // pick on the very first grain, made deterministic by candidate order).
            float cost = (float) attempt;
            if (prevStart >= 0)
            {
                const int tail0 = prevStart + grainLen - cw;
                cost = 0.0f;
                for (int i = 0; i < cw; ++i) { const float d = src[tail0 + i] - src[cand + i]; cost += d * d; }
                // LF-aware: inflate the join cost when the candidate's low end (LP-500 running-RMS
                // profile) differs from the previous grain's tail, so grain joins keep the room's
                // low-frequency weight consistent (a sub-join LF shift reads as "different room").
                if (lfProf != nullptr && lfProfLen >= srcLen)
                {
                    double pl = 0.0, cl = 0.0;
                    for (int i = 0; i < cw; ++i) { pl += lfProf[tail0 + i]; cl += lfProf[cand + i]; }
                    const float prevLf = (float) (pl / cw) + 1.0e-9f, candLf = (float) (cl / cw) + 1.0e-9f;
                    float lfC = std::fabs (20.0f * std::log10 (candLf / prevLf)) / 4.0f; // 1.0 at >=4 dB
                    lfC = lfC < 0.0f ? 0.0f : (lfC > 1.0f ? 1.0f : lfC);
                    cost *= 1.0f + 2.0f * lfC; // up to 3x cost at a full LF mismatch
                }
            }

            bool recentlyUsed = false;
            for (const auto& r : recent)
                if (std::abs (cand - r.first) < grainLen / 2) { recentlyUsed = true; break; }

            if (cost < bestAny) { bestAny = cost; anyStart = cand; }
            if (! recentlyUsed && cost < bestFresh) { bestFresh = cost; freshStart = cand; }
            if (prevStart < 0 && ! recentlyUsed) { freshStart = cand; break; }
        }
        int start = snapToZeroCross (freshStart >= 0 ? freshStart : anyStart);
        recent.push_back ({ start, here });
        prevStart = start;

        const bool reverse = rng.nextFloat() < 0.5f * variation;
        const int  pj = jitter > 0 ? (int) (rng.nextFloat (-1.0f, 1.0f) * (float) jitter) : 0;

        for (int i = 0; i < grainLen; ++i)
        {
            const int o = pos + pj + i;
            if (o < 0) continue;
            if (o >= n) break;
            const float s = src[start + (reverse ? (grainLen - 1 - i) : i)];
            out[o] += s * win[(std::size_t) i];
        }
    }
}
} // namespace tonefill::dsp
