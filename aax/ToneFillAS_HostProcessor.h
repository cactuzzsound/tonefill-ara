#pragma once

#include "AAX_CHostProcessor.h"

#include "engine/model/AmbienceModel.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <string>
#include <vector>

// AudioSuite HostProcessor: random-access reads the source (selection or WHOLE FILE), analyses it
// with tonefill_engine (same path as the JUCE FillWorker), renders a seamless loop, and tiles it
// across the rendered range. The model and fill are CACHED keyed on (source range + parameters),
// so Preview -> tweak render knob -> Render never re-analyses unless an analysis input changed.
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
    double readNorm (const char* paramID) const;      // normalized [0,1] parameter value
    double readReal (const char* paramID, double lo, double hi) const { return lo + readNorm (paramID) * (hi - lo); }
    double sampleRate() const;

    // Re-analyse / re-render only when the relevant inputs changed (cached across passes).
    void ensureFill (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize);
    bool analyze (const float* const inAudioIns[], int32_t inAudioInCount, int32_t windowSize);
    void renderFill();

    int    mChannels = 0;
    double mSampleRate = 48000.0;

    tonefill::engine::model::AmbienceModelPtr mModel; // cached learned model
    std::string mAnalysisSig, mRenderSig;             // cache keys

    std::vector<std::vector<float>> mFill;            // seamless loop (pre hiss; normalize baked in)
    long long                       mFillLen = 0;
    long long                       mGenPos  = 0;     // tiling position within the pass

    // Hiss filter applied LIVE on the tiled output (identical to the ARA processBlock path):
    // stateful across the whole render pass, so there's no filter-state discontinuity at loop wraps.
    std::vector<juce::IIRFilter> mHiss;
    float mHissLastFreq = -1.0f, mHissLastQ = -1.0f;
};
