#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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
    std::atomic<float>         movement { 0.2f };
    std::atomic<float>         fragment { 0.4f };
    std::atomic<float>         blend { 0.3f };
    std::atomic<float>         threshold { 0.3f };
    std::atomic<float>         outputGain { 1.0f };   // linear, applied on the audio thread
    std::atomic<std::uint64_t> seed { 1 };
    std::atomic<int>           generation { 0 };

    // worker / audio thread -> UI (status, meter)
    std::atomic<int>   phase { 0 };          // 0 idle, 1 analyzing, 2 ready
    std::atomic<int>   numPartials { 0 };
    std::atomic<float> learnSeconds { 0.0f };
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

    static SessionState& get() { static SessionState s; return s; }

private:
    std::mutex exportMutex_;
    std::shared_ptr<const FillBuffer> exportFill_;
    double exportSampleRate_ { 48000.0 };

    std::mutex waveMutex_;
    WaveData wave_;
};
} // namespace tonefill::plugin
