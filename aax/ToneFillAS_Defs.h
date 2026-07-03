#pragma once

#include "AAX.h" // AAX_CTypeID

// Native AAX AudioSuite (HostProcessor) build of ToneFill. Separate plugin client from the JUCE
// VST3/AU/ARA build; shares the same tonefill_engine. IDs are Cactuzz Sound's.
namespace tonefill_aax
{
const AAX_CTypeID kManufactureID = 'Catz'; // Cactuzz Sound
const AAX_CTypeID kProductID     = 'TfAS'; // ToneFill AudioSuite
const AAX_CTypeID kPlugInID_AS   = 'TFas'; // AudioSuite plug-in type id

// Parameter IDs (strings).
constexpr const char* kParamClean     = "clean";   // Clean Level
constexpr const char* kParamVoice     = "voice";   // Voice Reject
constexpr const char* kParamVariation = "variat";  // Variation
constexpr const char* kParamGain      = "gain";    // Output gain (dB)
constexpr const char* kParamMovement  = "moveme";  // Movement (Complex)
constexpr const char* kParamMode      = "mode";    // 0 Ambience, 1 Complex
constexpr const char* kParamLearn     = "learn";   // 1 = learn this selection, 0 = generate
} // namespace tonefill_aax
