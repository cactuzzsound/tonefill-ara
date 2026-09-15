#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace tonefill::engine::analysis
{
// Owned, host-agnostic inputs handed from the ARA facade to the analysis worker.
// The facade resamples to analysisSampleRate before constructing this, so the engine
// never deals with source sample-rate mismatch.
//
// Lifetime: owned by the caller for the duration of one AnalysisSession::run.
struct AnalysisContext
{
    // Concatenated clean-ish neighbour audio is NOT pre-trimmed here; the session scores
    // and selects frames. These buffers hold the raw learn windows + boundary buffers.
    juce::AudioBuffer<float> leftContext;   // audio before the target span
    juce::AudioBuffer<float> rightContext;  // audio after the target span

    double analysisSampleRate = 48000.0;
    int    numChannels        = 1;

    // Learn-region / analysis parameters that, when changed, force re-analysis.
    float  learnWindowSeconds = 4.0f;
    float  grainSizeMs        = 120.0f;
    float  tonalSensitivity   = 0.5f;   // 0..1
    float  speechReject       = 0.5f;   // 0..1 -> libfvad aggressiveness (more = exclude speech harder)
    float  cleanThreshold     = 0.3f;   // 0..1 -> 0..30 dB above the noise floor to accept as
                                        //         clean ambience. Lower = stricter (excludes
                                        //         dialogue/claps/clicks more aggressively).
    float  leftRightBias      = 0.0f;   // -1..+1
    bool   leftEnabled        = true;
    bool   rightEnabled       = true;

    // Manual mode: leftContext already holds ONLY the user-selected material. The level/speech
    // gates are trusted-off, but the flatness + min-fill selection still runs on it.
    bool   useManualSelection = false;

    // Stability selection: keep only locally-flat (stationary) runs of at least minFillSeconds,
    // so the fill uses long consistent chunks and avoids audible crossfades / jumps.
    float  flatness        = 0.7f;   // 0..1 strictness (higher = only very flat frames)
    float  minFillSeconds  = 2.0f;   // minimum contiguous stable run

    // AnalysisModel::Statistical (Design §E): use CandidateFrameSelector's weighted per-frame
    // scoring instead of the Classic hand-tuned gate stack. Default false = Classic (unchanged).
    bool   statisticalSelection = false;

    // Identifies the source range so the current-model cache can key on it. Computed by the facade.
    std::uint64_t sourceContentHash = 0;
};
} // namespace tonefill::engine::analysis
