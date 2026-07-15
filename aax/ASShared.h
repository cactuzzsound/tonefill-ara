#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <utility>
#include <vector>

namespace tonefill_aax
{
// Shared state between the HostProcessor (render thread) and the GUI (message thread), owned by the
// Parameters object (both sides reach it via GetEffectParameters()). The processor publishes the
// analysed waveform + diagnostics after each analyse; the GUI publishes the manual room-tone
// selection. Guarded by a SpinLock; copies under lock are small.
struct ASShared
{
    juce::SpinLock lock;

    // processor -> GUI (waveform overlay + status readouts)
    std::vector<float> peak;                 // downsampled |audio| per bin, 0..1
    std::vector<char>  clean;                // per-bin: is this bin selected clean ambience?
    int    sourceSamples = 0;                // length of the analysed source (for x mapping)
    double sampleRate    = 48000.0;
    float  usedSec       = 0.0f;             // learn material used (after Min Fill)
    float  availSec      = 0.0f;             // clean found before Min Fill
    float  seamDb        = 0.0f;             // mean join mismatch
    float  levelDb       = -120.0f;          // analysed source level
    int    chunks        = 0;
    bool   ready         = false;

    // GUI -> processor (manual room-tone regions, in source-sample coordinates)
    std::vector<std::pair<int, int>> manualRanges;

    // Set by the Parameters' ASPreviewState notification. Preview (realtime) -> the render thread
    // outputs the last-good fill and the worker recomputes in the background (no stall); when false
    // (offline Render) the render thread blocks for the correct fill so the written file is right.
    std::atomic<bool> previewing { false };
};
} // namespace tonefill_aax
