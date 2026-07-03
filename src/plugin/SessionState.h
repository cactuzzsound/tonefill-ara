#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace tonefill::plugin
{
// Process-global bridge between the editor UI (parameters in) and the ARA playback renderer's
// background worker (render params in, analysis status + meter + export buffer out).
struct SessionState
{
    // UI -> worker (render controls)
    std::atomic<int>           mode { 3 };            // 0 static, 1 hybrid, 2 complex, 3 ambience
    std::atomic<float>         tonalRetention { 1.0f };
    std::atomic<float>         textureAmount { 0.5f };
    std::atomic<float>         minFill { 2.0f };      // min stable-fragment length (seconds)
    std::atomic<float>         flatness { 0.7f };     // stationarity strictness (0..1)
    std::atomic<bool>          paulStretch { false }; // Enhance: PaulStretch resynthesis on/off
    std::atomic<float>         fragment { 0.4f };
    std::atomic<float>         blend { 0.3f };
    std::atomic<float>         randomness { 0.4f };   // variation: loop length + per-fragment jitter
    std::atomic<float>         threshold { 0.3f };
    std::atomic<float>         speechReject { 0.5f };
    std::atomic<float>         outputGain { 1.0f };   // linear, applied on the audio thread
    std::atomic<bool>          normalizeEnabled { false }; // bake loudness-normalize into the fill
    std::atomic<float>         normalizeTarget { -16.0f }; // dBFS (peak) or LUFS, per unit below
    std::atomic<bool>          normalizeLufs { true };     // true = LUFS, false = dBFS (peak)
    std::atomic<float>         measuredLufs { -120.0f };   // worker -> UI (last render)
    std::atomic<float>         measuredPeakDb { -120.0f }; // worker -> UI (last render)
    std::atomic<float>         renderLength { 5.0f }; // seconds, for Export WAV
    std::atomic<bool>          manualMode { false };  // learn from user-selected regions
    std::atomic<bool>          wholeFile { false };   // analyze the whole item (vs first 4 min)
    std::atomic<bool>          statisticalMode { false }; // Statistical selection (Design §E) vs Classic
    std::atomic<int>           sourceSamples { 0 };   // length of the analysed source (UI mapping)
    std::atomic<double>        sourceSampleRate { 48000.0 }; // for the UI timecode ruler
    std::atomic<std::uint64_t> seed { 1 };
    std::atomic<int>           generation { 0 };

    // worker / audio thread -> UI (status, meter)
    std::atomic<int>   phase { 0 };          // 0 idle, 1 analyzing, 2 ready
    std::atomic<int>   numPartials { 0 };
    std::atomic<float> learnSeconds { 0.0f };
    std::atomic<int>   cleanChunks { 0 };    // number of stable fragments actually used
    std::atomic<float> availSeconds { 0.0f }; // total clean found before the Min Fill filter
    std::atomic<float> seamRiskDb { 0.0f };   // An5: mean band-dB mismatch across concat joins
    std::atomic<float> levelDb { -120.0f };  // analyzed source level (in)
    std::atomic<float> outMeterDb { -120.0f }; // live output level (out)

    // Current fill copied for "Export WAV". Guarded; small, set occasionally.
    using FillBuffer = std::vector<std::vector<float>>;
    void setExportFill (std::shared_ptr<const FillBuffer> f, double sr)
    {
        std::lock_guard<std::mutex> l (exportMutex_);
        exportFill_ = std::move (f);
        exportSampleRate_ = sr;
    }
    std::shared_ptr<const FillBuffer> getExportFill (double& srOut)
    {
        std::lock_guard<std::mutex> l (exportMutex_);
        srOut = exportSampleRate_;
        return exportFill_;
    }

    // Downsampled source waveform for the UI: peak per bin + clean flag (selected vs rejected).
    struct WaveData { std::vector<float> peak; std::vector<char> clean; };
    void setWave (WaveData w) { std::lock_guard<std::mutex> l (waveMutex_); wave_ = std::move (w); }
    WaveData getWave() { std::lock_guard<std::mutex> l (waveMutex_); return wave_; }

    // User-selected learn regions (manual mode), in SOURCE sample coordinates.
    std::atomic<int> manualGen { 0 }; // bumped when the selection changes -> worker re-analyzes
    void setManualRanges (std::vector<std::pair<int, int>> r)
    {
        std::lock_guard<std::mutex> l (rangesMutex_);
        manualRanges_ = std::move (r);
        manualGen.fetch_add (1);
    }
    std::vector<std::pair<int, int>> getManualRanges()
    {
        std::lock_guard<std::mutex> l (rangesMutex_);
        return manualRanges_;
    }

    // NOTE: deliberately NOT a process-global singleton. One instance is owned by each
    // PluginProcessor and shared (shared_ptr) with that instance's editor and ARA playback
    // renderer/worker, so multiple plugin instances (Reaper creates one per item/region) never
    // overwrite each other's waveform/selection/status.

private:
    std::mutex exportMutex_;
    std::shared_ptr<const FillBuffer> exportFill_;
    double exportSampleRate_ { 48000.0 };

    std::mutex waveMutex_;
    WaveData wave_;

    std::mutex rangesMutex_;
    std::vector<std::pair<int, int>> manualRanges_;
};
} // namespace tonefill::plugin
