#pragma once

namespace tonefill::dsp
{
// Cheap per-frame spectral/temporal features (design §E). All V1, all pure functions.
// Higher-level fused scores (speechScore, ambienceScore) live in engine/analysis — these are
// the raw measurements those scorers combine.
struct SpectralFrameFeatures
{
    float rms          = 0.0f;
    float centroidHz   = 0.0f;
    float flatness     = 0.0f;  // 0..1 Wiener entropy (1 = noise-like)
    float flux         = 0.0f;  // half-wave-rectified spectral flux vs previous frame
    float crestDb      = 0.0f;  // 20*log10(peak/rms) of the frame
    float lowBandRatio = 0.0f;  // energy < 200 Hz / total (hum-presence hint)
};

// Spectral centroid in Hz. mag has nBins elements; binHz = sampleRate / fftSize.
float spectralCentroid (const float* mag, int nBins, double binHz);

// Spectral flatness (geometric mean / arithmetic mean of power), bins [1, nBins).
float spectralFlatness (const float* mag, int nBins);

// Positive spectral flux vs previous magnitude (prevMag may be null -> 0).
float spectralFlux (const float* mag, const float* prevMag, int nBins);

// Energy ratio below cutoffHz.
float lowBandRatio (const float* mag, int nBins, double binHz, double cutoffHz = 200.0);

// RMS and crest (dB) from raw (un-windowed) time-domain frame samples.
float frameRms   (const float* frame, int n);
float frameCrestDb (const float* frame, int n);

// Convenience: fill all features. `frame` = raw time samples (rms/crest); `mag` = magnitude
// spectrum; `prevMag` = previous frame magnitude or null (flux).
SpectralFrameFeatures computeFrameFeatures (const float* frame, int n,
                                            const float* mag, const float* prevMag,
                                            int nBins, double binHz);
} // namespace tonefill::dsp
