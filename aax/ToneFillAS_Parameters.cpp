#include "ToneFillAS_Parameters.h"
#include "ToneFillAS_Defs.h"

#include "AAX_CParameter.h"
#include "AAX_CLinearTaperDelegate.h"
#include "AAX_CNumberDisplayDelegate.h"
#include "AAX_CBinaryTaperDelegate.h"
#include "AAX_CBinaryDisplayDelegate.h"
#include "AAX_CString.h"

using namespace tonefill_aax;

AAX_CEffectParameters* AAX_CALLBACK ToneFillAS_Parameters::Create()
{
    return new ToneFillAS_Parameters();
}

AAX_Result ToneFillAS_Parameters::EffectInit()
{
    // Master bypass (required by the AAX spec).
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
    auto addBool = [&] (const char* id, const char* name, bool def,
                        const char* offText, const char* onText)
    {
        auto* p = new AAX_CParameter<bool> (
            id, AAX_CString (name), def,
            AAX_CBinaryTaperDelegate<bool>(),
            AAX_CBinaryDisplayDelegate<bool> (offText, onText), true);
        p->SetNumberOfSteps (2);
        p->SetType (AAX_eParameterType_Discrete);
        mParameterManager.AddParameter (p);
    };

    // Detection (defaults match the JUCE plugin's APVTS defaults).
    addFloat (kParamClean,    "Clean Level",  0.30f, 0.0f, 1.0f);
    addFloat (kParamVoice,    "Voice Reject", 0.50f, 0.0f, 1.0f);
    addFloat (kParamMinFill,  "Min Fill",     2.00f, 0.2f, 5.0f);
    // Structure.
    addFloat (kParamFlatness, "Flatness",     0.70f, 0.0f, 1.0f);
    addFloat (kParamChunk,    "Chunk Size",   0.40f, 0.0f, 1.0f);
    addFloat (kParamXfade,    "Crossfade",    0.30f, 0.0f, 1.0f);
    // Texture.
    addFloat (kParamSmooth,   "Smoothness",   0.40f, 0.0f, 1.0f);
    addFloat (kParamGain,     "Output",       0.00f, -24.0f, 24.0f);
    // Modes.
    addBool (kParamEnhance,  "Enhance",      false, "off", "on");
    addBool (kParamExperim,  "Experimental", false, "classic", "experimental");
    // Hiss filter (Enhance only; baked into the offline render).
    addBool  (kParamHissOn,   "Hiss Filter", false, "off", "on");
    addFloat (kParamHissFreq, "Hiss Freq",   9000.0f, 3000.0f, 15000.0f);
    addFloat (kParamHissQ,    "Hiss Q",      0.707f, 0.3f, 2.0f);
    // Normalize (measured on the actual rendered output).
    addBool  (kParamNormOn,     "Normalize",   false, "off", "on");
    addFloat (kParamNormTarget, "Norm Target", -16.0f, -60.0f, 0.0f);
    addBool  (kParamNormLufs,   "Norm Unit",   true, "dBFS", "LUFS");
    // Variation seed: nudging it = "Regenerate".
    addFloat (kParamSeed, "Seed", 1.0f, 1.0f, 100.0f);

    return AAX_SUCCESS;
}
