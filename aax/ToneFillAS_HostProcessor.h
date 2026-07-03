#pragma once

#include "AAX_CHostProcessor.h"
#include <vector>
#include <cstdint>

// AudioSuite HostProcessor: LEARN accumulates the selection (or whole file) and analyses it in
// PostRender; GENERATE outputs room tone synthesised from the most-recently learned model.
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
    double readNorm (const char* paramID) const; // normalized [0,1] parameter value
    double sampleRate() const;
    void   analyzeAndBuild (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize);

    int    mChannels = 0;
    double mSampleRate = 48000.0;
    bool   mAnalyzed = false;

    std::vector<std::vector<float>> mFill;     // generated loop tiled over the selection
    long long                       mFillLen  = 0;
    long long                       mGenPos   = 0;
};
