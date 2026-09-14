#pragma once

#include "AAX.h" // AAX_CTypeID

#include <string>

// Native AAX AudioSuite (HostProcessor) build of ToneFill. Separate plugin client from the JUCE
// VST3/AU/ARA build; shares the same tonefill_engine. Vendor identity MUST match the other
// formats (LostComzz precedent): "cactuzz sound" / 'Czsd'.
namespace tonefill_aax
{
const AAX_CTypeID kManufactureID = 'Czsd'; // cactuzz sound (same vendor code as LostComzz)
const AAX_CTypeID kProductID     = 'TfAS'; // ToneFill AudioSuite
const AAX_CTypeID kPlugInID_AS   = 'TFas'; // AudioSuite plug-in type id

// Parameter IDs (strings). Mirrors the JUCE plugin's parameter set 1:1 (Phase A parity);
// Export Len / Full / Audition are omitted because Pro Tools provides them natively
// (selection length, WHOLE FILE, Preview).
constexpr const char* kParamClean      = "clean";    // Clean Level        0..1
constexpr const char* kParamVoice      = "voice";    // Voice Reject       0..1
constexpr const char* kParamMinFill    = "minfill";  // Min Fill           0.2..5 s
constexpr const char* kParamFlatness   = "flatness"; // Flatness           0..1
constexpr const char* kParamChunk      = "chunk";    // Chunk Size         0..1 (-> 200..3000 ms)
constexpr const char* kParamXfade      = "xfade";    // Crossfade          0..1 (-> 5..50 %)
constexpr const char* kParamSmooth     = "smooth";   // Smoothness         0..1 (Enhance window)
constexpr const char* kParamGain       = "gain";     // Output             -24..+24 dB
constexpr const char* kParamEnhance    = "enhance";  // Enhance (PaulStretch) on/off
constexpr const char* kParamExperim    = "experi";   // Experimental (statistical) selection on/off
constexpr const char* kParamHissOn     = "hisson";   // Parametric EQ MASTER on/off (Enhance only)
constexpr const char* kParamHissFreq   = "hissfrq";  // (legacy, unused)
constexpr const char* kParamHissQ      = "hissq";    // (legacy, unused)

// Enhance-only parametric EQ: 5 fully flexible bands. Ids generated per band (1..5) + field.
// Type order matches SessionState::EqType / dsp::makeEqCoefficients: 0 Bell,1 LowShelf,2 HighShelf,
// 3 HighPass,4 LowPass,5 Notch.
constexpr int kEqBandsAAX    = 5;
constexpr int kEqNumTypesAAX = 6;
inline std::string eqAaxId (int band, const char* field) { return "eq" + std::to_string (band) + field; }
constexpr const char* kParamNormOn     = "normon";   // Normalize on/off
constexpr const char* kParamNormTarget = "normtgt";  // Normalize target   -60..0 (dBFS or LUFS)
constexpr const char* kParamNormLufs   = "normluf";  // 1 = LUFS, 0 = dBFS (peak)
constexpr const char* kParamSeed       = "seed";     // Variation seed     1..100 (nudge = Regenerate)
constexpr const char* kParamManual     = "manual";   // 1 = learn from manual regions on the waveform
constexpr const char* kParamSpectral   = "spectral"; // Spectral Mosaic: per-band select + synth on/off
constexpr const char* kParamBands      = "bands";    // Spectral: number of analysis bands  3..12
constexpr const char* kParamSpectralAdv= "specadv";  // Spectral Advanced: use editor band edges
constexpr const char* kParamBypass     = "bypass";   // A/B: pass the source through instead of the fill
} // namespace tonefill_aax
