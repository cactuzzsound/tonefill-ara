#include "ToneFillAS_Parameters.h"
#include "ToneFillAS_Defs.h"

#include "AAX_CParameter.h"
#include "AAX_CLinearTaperDelegate.h"
#include "AAX_CNumberDisplayDelegate.h"
#include "AAX_CBinaryTaperDelegate.h"
#include "AAX_CBinaryDisplayDelegate.h"
#include "AAX_CString.h"
#include "AAX_Assert.h"

using namespace tonefill_aax;

AAX_CEffectParameters* AAX_CALLBACK ToneFillAS_Parameters::Create()
{
    return new ToneFillAS_Parameters();
}

AAX_Result ToneFillAS_Parameters::EffectInit()
{
    // Master bypass (required).
    {
        auto* bypass = new AAX_CParameter<bool> (
            cDefaultMasterBypassID, AAX_CString ("Bypass"), false,
            AAX_CBinaryTaperDelegate<bool>(),
            AAX_CBinaryDisplayDelegate<bool> ("bypass", "on"), true);
        bypass->SetNumberOfSteps (2);
        bypass->SetType (AAX_eParameterType_Discrete);
        mParameterManager.AddParameter (bypass);
    }

    auto addFloat = [&] (const char* id, const char* name, float def, float lo, float hi)
    {
        auto* p = new AAX_CParameter<float> (
            id, AAX_CString (name), def,
            AAX_CLinearTaperDelegate<float> (lo, hi),
            AAX_CNumberDisplayDelegate<float>(), true);
        p->SetNumberOfSteps (128);
        mParameterManager.AddParameter (p);
    };

    addFloat (kParamClean,     "Clean Level",  0.30f,   0.0f, 1.0f);
    addFloat (kParamVoice,     "Voice Reject", 0.70f,   0.0f, 1.0f);
    addFloat (kParamVariation, "Variation",    0.40f,   0.0f, 1.0f);
    addFloat (kParamMovement,  "Movement",     0.20f,   0.0f, 1.0f);
    addFloat (kParamGain,      "Output",       0.00f, -24.0f, 24.0f);
    addFloat (kParamMode,      "Complex",      0.00f,   0.0f, 1.0f); // 0 Ambience, 1 Complex

    // Learn (1) vs Generate (0).
    {
        auto* learn = new AAX_CParameter<bool> (
            kParamLearn, AAX_CString ("Learn"), false,
            AAX_CBinaryTaperDelegate<bool>(),
            AAX_CBinaryDisplayDelegate<bool> ("generate", "learn"), true);
        learn->SetNumberOfSteps (2);
        learn->SetType (AAX_eParameterType_Discrete);
        mParameterManager.AddParameter (learn);
    }

    return AAX_SUCCESS;
}
