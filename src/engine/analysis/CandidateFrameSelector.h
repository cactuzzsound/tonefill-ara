#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <utility>
#include <vector>

namespace tonefill::engine::analysis
{
// Design §E — weighted per-frame scoring as an ALTERNATIVE to the hand-tuned gate stack in
// selectCleanAmbience. Gated behind AnalysisModel::Statistical; Classic stays the default and is
// left byte-identical. This class is self-contained: it returns the SAME outputs as
// selectCleanAmbience (a concatenated learn buffer + selected sample ranges + diagnostics), so the
// rest of AmbienceModelBuilder::assemble is unchanged.
//
// Fixes vs the original sketch: STFT run once before the loops, correct binHz = sampleRate/N,
// prevMag copy-back for real flux, a colour-consistency term (quietest-~30% tilt reference) so
// monotonicity does not regress, scoring normalised by the weight sum, level scored vs the per-run
// noise floor (not a rolling max), lowBandRatio as a SOFT penalty (not a hard reject), the HP-300
// VAD copy built once per channel, and output ranges in the single learn-buffer coordinate system.
//
// Thread affinity: ANALYSIS WORKER only (uses dsp::Stft scratch; not thread-safe).
class CandidateFrameSelector
{
public:
    struct Params
    {
        int   frameSize      = 2048;
        int   hopSize        = 512;
        float cleanThreshold = 0.3f;  // Clean Level: proximity permissiveness (higher = more)
        float speechReject   = 0.5f;  // Voice Reject: VAD aggressiveness + speech weight/threshold
        float flatness       = 0.7f;  // Flatness: overall acceptance strictness
        float minFillSeconds = 2.0f;  // minimum contiguous accepted run

        // Scoring weights (normalised by their sum, so absolute magnitudes are free).
        float wFlatness     = 0.25f;  // Wiener entropy: noise-like is good
        float wStationarity = 0.25f;  // low spectral flux vs per-run reference
        float wProximity    = 0.15f;  // level near the per-run noise floor
        float wSpeech       = 0.20f;  // low voicing probability
        float wTransient    = 0.15f;  // low crest factor
        float wColour       = 0.20f;  // close to the dominant room-tone colour (tilt cluster)

        float hardRejectCrestDb = 18.0f; // crestDb above this is always a transient
    };

    // Returns the concatenated clean learn buffer and fills the same out-params as
    // selectCleanAmbience: selected sample ranges (source coords), available clean seconds
    // (pre Min Fill), and the mean band-dB join mismatch ("seam risk").
    juce::AudioBuffer<float> selectLearnBuffer (const juce::AudioBuffer<float>& src,
                                                double sampleRate,
                                                const Params& params,
                                                std::vector<std::pair<int, int>>* rangesOut = nullptr,
                                                float* availSecOut = nullptr,
                                                float* roughnessOut = nullptr) const;
};
} // namespace tonefill::engine::analysis
