#pragma once

#include "engine/analysis/AnalysisContext.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace tonefill::engine::analysis
{
// Scores short frames for "clean ambience" and groups accepted frames into learn segments.
// Rejects speech-contaminated and transient frames (design §E).
//
// Thread affinity: ANALYSIS WORKER. Stateless aside from scratch; safe to construct per run.
class CandidateFrameSelector
{
public:
    struct Segment
    {
        int channel    = 0;
        long long start = 0;   // sample offset within the concatenated learn buffer
        long long length = 0;
        float score    = 0.0f; // mean ambience score over the segment
    };

    struct Params
    {
        int   frameSize        = 2048;
        int   hopSize          = 512;
        float speechReject     = 0.6f;
        float minSegmentMs     = 150.0f;
        // Scoring weights (design §E defaults).
        float wFlatness        = 0.30f;
        float wStationarity    = 0.25f;
        float wProximity       = 0.15f;
        float wSpeech          = 0.20f;
        float wTransient       = 0.15f;
        float wLevelOutlier    = 0.10f;
    };

    // Returns accepted learn segments ordered best-first.
    std::vector<Segment> select (const juce::AudioBuffer<float>& context,
                                 double sampleRate,
                                 const Params& params) const;

private:
    // TODO(dsp §E): per-frame features (RMS, centroid, flatness, flux, ZCR, low-band ratio).
    // TODO(dsp §E): voicing/speech score (cepstral pitch salience + syllabic-rate modulation).
    // TODO(dsp §E): transient rejection (crest factor / flux z-score).
    // TODO(dsp §E): weighted scoring + contiguous-run grouping with min-length gate.
};
} // namespace tonefill::engine::analysis
