#pragma once

#include "AAX_CHostProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// AudioSuite HostProcessor. The whole-source read (GetAudio) stays on the render thread, but the
// heavy analyse + render runs on a BACKGROUND worker (like the ARA FillWorker), so Preview never
// stalls: it keeps playing the last-good fill and swaps in the new one when the worker finishes.
// During an offline Render (previewing == false) the render thread blocks for the correct fill so
// the written file is right.
class ToneFillAS_HostProcessor : public AAX_CHostProcessor
{
public:
    static AAX_CHostProcessor* AAX_CALLBACK Create();
    ToneFillAS_HostProcessor();
    ~ToneFillAS_HostProcessor() override;

    AAX_Result PreRender  (int32_t iAudioInCount, int32_t iAudioOutCount, int32_t iWindowSize) override;
    AAX_Result RenderAudio (const float* const inAudioIns[], int32_t inAudioInCount,
                            float* const inAudioOuts[], int32_t inAudioOutCount, int32_t* ioWindowSize) override;
    AAX_Result PostRender () override;

private:
    class Worker;

    double readNorm (const char* paramID) const;
    double readReal (const char* paramID, double lo, double hi) const { return lo + readNorm (paramID) * (hi - lo); }
    double sampleRate() const;
    void   ensureRawSource (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize);

    std::unique_ptr<Worker> mWorker;

    int    mChannels = 0;
    double mSampleRate = 48000.0;
    long long mGenPos = 0;

    // Whole analysed source (mono-collapsed), read on the render thread, handed to the worker.
    std::shared_ptr<const juce::AudioBuffer<float>> mRaw;
    std::string mRawSig, mLastSubmitSig;

    // Hiss filter applied LIVE on the tiled output (stateful across the pass), like ARA.
    std::vector<juce::IIRFilter> mHiss;
    float mHissLastFreq = -1.0f, mHissLastQ = -1.0f;
};
