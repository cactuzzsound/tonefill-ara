# ToneFill ARA — DSP Implementation Plan

Code-oriented plan: data structures, defaults, function-level APIs, end-to-end pseudocode,
the analysis→synthesis dependency graph, and the V1 / V1.1 / later split. No final DSP code.

Conventions:
- All analysis runs at `analysisSampleRate = min(sourceSR, 48000)`. Tonal frequencies are
  stored in Hz (SR-independent). Render happens at `targetSampleRate`.
- Defaults below are quoted at 48 kHz; scale sample counts by `sr/48000`.
- "Frame" = STFT frame; "grain" = textural time-domain chunk. Different sizes, don't conflate.

---

## 1. Internal data structures

These are working/intermediate types (NOT serialized). The serialized model types
(`TonalLayer`, `NoiseProfile`, `GrainCorpus`, `BoundaryConditionProfile`, `AmbienceModel`)
already exist in `engine/model/`. New analysis-internal types live in `engine/analysis/detail/`,
synthesis scratch in `engine/synthesis/detail/`.

### 1.1 STFT + spectral working set (`dsp/`)
```
struct StftConfig {
    int   fftSize   = 2048;
    int   hopSize   = 512;          // 75% overlap
    WindowType window = Hann;
    bool  zeroPadTo4096 = false;    // V1.1 tonal refinement
};

struct StftFrame {                  // one analysis hop, one channel
    long long frameIndex = 0;
    long long startSample = 0;
    std::vector<float> magnitude;   // size fftSize/2 + 1
    std::vector<float> phase;       // size fftSize/2 + 1 (only needed for tonal + residual)
};

// Reusable FFT scratch so we don't allocate per frame. Owned by a worker, never shared.
struct StftScratch {
    juce::dsp::FFT fft;             // size = fftSize
    std::vector<float> window;      // precomputed analysis window
    std::vector<float> timeBuf;     // fftSize
    std::vector<float> freqBuf;     // 2*fftSize (juce interleaved real/imag)
    float windowSumSquares = 0;     // for COLA normalization on resynthesis
};
```

### 1.2 Per-frame features (scoring + gating)
```
struct FrameFeatures {
    long long startSample = 0;
    float rmsDb        = -120;
    float centroidHz   = 0;
    float flatness     = 0;     // 0..1 Wiener entropy (1 = noise-like)
    float flux         = 0;     // half-wave-rectified spectral flux vs previous frame
    float crestDb      = 0;     // peak/RMS within the frame
    float lowBandRatio = 0;     // energy <200Hz / total (hum presence hint)
    float harmonicity  = 0;     // 0..1 pitch salience in 80-400Hz (speech/voiced hint)
    float speechScore  = 0;     // 0..1 fused speech-likelihood (filled by scorer)
    float ambienceScore= 0;     // 0..1 fused ambience suitability (filled by scorer)
    bool  rejectedTransient = false;
    bool  rejectedSpeech    = false;
};

using FeatureTrack = std::vector<FrameFeatures>;  // one per channel context buffer
```

### 1.3 Tonal estimation internals
```
struct SpectralPeak { float freqHz; float amp; float phase; int bin; };

struct PartialTrack {               // a linked peak across frames
    std::vector<float> freqHz;      // per frame where alive
    std::vector<float> amp;
    std::vector<float> phase;
    int   birthFrame = 0;
    int   deathFrame = 0;
    float presence   = 0;           // alive frames / total frames
    float freqStdHz  = 0;
};
// PartialTrack[] -> filtered -> model::Partial[] (TonalLayer).
```

### 1.4 Learn-material assembly
```
struct LearnSegment {               // accepted contiguous run (from CandidateFrameSelector)
    int channel; long long start; long long length; float score;
    enum class Origin { Left, Right } origin;
};

struct CleanMaterial {              // concatenated accepted audio, per channel
    juce::AudioBuffer<float> audio;             // full (tonal present)
    juce::AudioBuffer<float> residual;          // tonal-removed (fill later)
    std::vector<LearnSegment> provenance;       // for diagnostics / weighting
    double sampleRate;
};
```

### 1.5 Synthesis scratch
```
struct RenderScratch {
    std::vector<float> noise;       // per channel, length = target + tails
    std::vector<float> tonal;       // additive osc accumulator
    std::vector<float> grains;      // granular accumulator
    std::vector<float> mixL, mixR;  // pre-trim accumulators
    dsp::SeededRng rngNoise, rngGrainPick, rngJitter, rngMacro; // sub-streams from seed
};

struct GrainCursor {                // anti-loop state for concatenation
    int   current = -1;
    std::deque<int> recent;         // last M grains (anti-repeat)
    long long writePos = 0;
};
```

---

## 2. Default sizes (the numbers you tune from)

| Parameter | Default @48k | Range | Notes |
|---|---|---|---|
| Analysis SR | min(src, 48000) | — | tonal in Hz, SR-independent |
| STFT fftSize | **2048** (~42.7 ms) | 1024–4096 | resolves 50/60 Hz with parabolic interp |
| STFT hop | **512** (75% OL) | 256–1024 | features + LTAS + residual |
| Window | **Hann** | — | COLA-valid at 75% |
| Tonal zero-pad | off (V1) → **4096** (V1.1) | — | finer peak freq estimate |
| Tonal peak threshold | **+9 dB** over local noise floor | 6–15 dB | floor via 1/3-oct median |
| Tonal min presence | **0.60** | 0.4–0.8 | keep steady tones only |
| Tonal freq-link tol | **3%** | 1–5% | greedy nearest linking |
| LTAS smoothing | **1/6 octave** | 1/3–1/12 | residual + full |
| Band splits | **200 Hz / 4 kHz** (3 bands) | — | 4th band later |
| Grain size | **120 ms** (5760 smp) | 40–300 ms | Tukey α=0.5 |
| Grain hop (extract) | **50%** (60 ms) | 25–50% | more grains = less loop |
| Grain window (synth) | **Tukey α=0.5**, 50% OL | — | COLA-safe overlap-add |
| Anti-repeat window M | **8 grains** | 4–16 | no reuse within M |
| Grain jitter | start ±15 ms, gain ±0.5 dB | — | seeded |
| Boundary snippet | **50 ms** | 20–80 ms | seam capture |
| Crossfade | **40 ms** equal-power | 5–250 ms | longer if strongly tonal |
| Macro env rate (Complex) | **0.05–0.3 Hz** | — | random-walk |
| Macro env depth | **±1.5 dB** level, ±1 dB HF tilt | — | endpoints anchored |
| Min learn material | **2.0 s** total | — | below → warn / Static-only |
| Min usable grains | **20** | — | below → loopRiskHigh |
| Speech reject (default) | score > **0.5** rejects | 0–1 | conservative: discard freely |
| Transient reject | crest > **12 dB** OR flux z > **2.5** | — | excludes plosives/clicks |

---

## 3. Function-level API proposals

Signatures only; pure where possible (testable). All take explicit params structs so defaults
live in one place. No allocation in hot inner loops — pass scratch in.

### 3.1 Speech contamination scoring
```
namespace tonefill::engine::analysis {

struct SpeechScoreParams {
    float pitchMinHz = 80, pitchMaxHz = 400;
    float harmonicityWeight = 0.6f;     // V1 primary cue
    float syllabicWeight    = 0.4f;     // V1.1 (4-8 Hz modulation in 1-4kHz)
    float rejectThreshold   = 0.5f;
};

// Fills FrameFeatures.harmonicity + speechScore + rejectedSpeech in place.
// V1: harmonicity only (autocorrelation pitch salience). V1.1: add syllabic modulation.
void scoreSpeech (FeatureTrack& frames,
                  const juce::AudioBuffer<float>& contextMono,
                  double sampleRate,
                  const SpeechScoreParams& p);
} // namespace
```

### 3.2 Ambience candidate scoring
(Already stubbed as `CandidateFrameSelector`. Concrete contract:)
```
struct AmbienceScoreParams { /* weights as in §2 + scaffold defaults */ };

// Pure: features + boundary distance -> ambienceScore per frame.
void scoreAmbience (FeatureTrack& frames,
                    long long targetBoundarySample,   // for proximity term
                    const AmbienceScoreParams& p);

// Group accepted frames (ambienceScore>thr && !rejected*) into contiguous LearnSegments,
// honoring minSegmentMs; ordered best-first; applies left/right bias.
std::vector<LearnSegment> selectSegments (const FeatureTrack& left,
                                          const FeatureTrack& right,
                                          const AmbienceScoreParams& p,
                                          float leftRightBias);
```

### 3.3 Tonal partial estimation
```
struct TonalParams { /* fftSize, hop, peakThresholdDb, minPresence, linkTolPct, sensitivity */ };

struct TonalResult {
    model::TonalLayer layer;                 // kept stable partials
    juce::AudioBuffer<float> residual;       // input minus partials (spectral-notch method)
};

// Pipeline: STFT -> per-frame peaks (parabolic) -> greedy linking -> presence/std filter ->
// build Partials -> residual via spectral notch + ISTFT.
TonalResult estimateTonal (const juce::AudioBuffer<float>& cleanMono,
                           double sampleRate,
                           const TonalParams& p,
                           StftScratch& scratch);

// Helpers (unit-testable in isolation):
std::vector<SpectralPeak> pickPeaks (const StftFrame&, float thresholdDb, double sr);
void linkPeaks (std::vector<PartialTrack>& tracks,
                const std::vector<SpectralPeak>& framePeaks, int frameIdx, float linkTolPct);
```
Residual method (V1 decision): **spectral notch**, not time-domain oscillator subtraction.
Zero (or attenuate by the tracked amplitude) the ±2 bins around each partial in each frame's
complex spectrum, ISTFT with COLA. Avoids oscillator phase-matching error and is cheaper.

### 3.4 LTAS / noise profile estimation
```
struct LtasParams { int fftSize, hop; float smoothingOctaveFrac = 1.0f/6.0f; };

// Mean magnitude spectrum over accepted frames, fractional-octave smoothed.
std::vector<float> computeLtas (const juce::AudioBuffer<float>& mono,
                                double sampleRate, const LtasParams& p, StftScratch&);

// Assemble NoiseProfile: residualLtas (from residual), fullLtas (from full),
// bandEnergies (3 bands), targetRms (broadband RMS of accepted ambience).
model::NoiseProfile buildNoiseProfile (const juce::AudioBuffer<float>& residualMono,
                                       const juce::AudioBuffer<float>& fullMono,
                                       double sampleRate, const LtasParams& p, StftScratch&);
```

### 3.5 Grain extraction
```
struct GrainParams { float grainSizeMs=120, hopFraction=0.5f; int minUsable=20; size_t maxBytes; };

// Cut Tukey-windowed grains from residual over accepted segments only; compute features;
// normalize features across corpus; decimate to maxBytes; set usableCount.
model::GrainCorpus extractGrains (const juce::AudioBuffer<float>& residualMono,
                                  const std::vector<LearnSegment>& accepted,
                                  double sampleRate, const GrainParams& p);

model::GrainFeatures computeGrainFeatures (const float* grain, int n, double sr);  // pure
```

### 3.6 Grain similarity analysis
```
// Weighted Euclidean over z-normalized features. Pure, cheap, branch-free.
float grainDistance (const model::GrainFeatures& a, const model::GrainFeatures& b);

// Pick next grain: nearest in feature space to `targetFeat`, excluding recent[] (anti-repeat),
// with seeded tie-break/jitter. Brute force over corpus (V1; corpus is small).
int pickNextGrain (const model::GrainCorpus& corpus,
                   const model::GrainFeatures& targetFeat,
                   const GrainCursor& cursor,
                   dsp::SeededRng& rng);
```

### 3.7 Boundary matching
```
// Capture seam conditions from real neighbour audio at the gap edges.
model::BoundaryConditionProfile captureBoundary (const juce::AudioBuffer<float>& neighbourMono,
                                                 long long seamSample,
                                                 model::BoundaryConditionProfile::Side side,
                                                 const model::TonalLayer& tonal,
                                                 double sampleRate);

// Apply at render time: equal-power crossfade fill<->seam, tonal phase alignment,
// seam RMS match (short gain ramp), optional 1st-order tilt shelf (V1.1).
void applyEdgeConditioning (float* fill, long long n,
                            const model::BoundaryConditionProfile& pre,
                            const model::BoundaryConditionProfile& post,
                            float crossfadeMs, double sampleRate);
```

### 3.8 Hybrid ambience rendering
```
// Concrete layer functions (no virtual interface — called directly by mode dispatch).
void synthTonalLayer (float* out, long long n, const model::TonalLayer&,
                      const model::BoundaryConditionProfile& pre,  // phase dock
                      float retention, double sr);

void synthNoiseBed   (float* out, long long n, const model::NoiseProfile&,
                      dsp::SeededRng& rng, double sr, StftScratch&);   // STFT-shaped noise

void synthGrainLayer (float* out, long long n, const model::GrainCorpus&,
                      const model::GrainFeatures& startFeat,
                      float amount, float randomness,
                      dsp::SeededRng& rngPick, dsp::SeededRng& rngJitter, GrainCursor&);

void applyMacroEnvelope (float* buf, long long n, float depth, float rate,
                         float endLevelAnchorPre, float endLevelAnchorPost,
                         dsp::SeededRng& rngMacro, double sr);          // Complex only

void matchOutputLtas (float* buf, long long n, const std::vector<float>& targetLtas,
                      double sr, StftScratch&);                          // gentle corrective EQ

void normalizeEnergy (float* buf, long long n, float targetRms);
```

---

## 4. End-to-end analysis pass (pseudocode)

```
AmbienceModel analyze(AnalysisContext ctx):
    model.numChannels = ctx.numChannels
    model.analysisSampleRate = ctx.analysisSampleRate
    StftScratch scratch(fftSize=2048)

    for ch in channels:
        # --- 1. Per-frame features on left & right context ---
        Lfeat = computeFeatures(ctx.leftContext[ch], scratch)
        Rfeat = computeFeatures(ctx.rightContext[ch], scratch)
        scoreSpeech(Lfeat, ...); scoreSpeech(Rfeat, ...)
        markTransients(Lfeat, crest>12dB || fluxZ>2.5)
        markTransients(Rfeat, ...)
        scoreAmbience(Lfeat, boundary=leftSeam); scoreAmbience(Rfeat, boundary=rightSeam)
        checkCancel()

        # --- 2. Select & assemble clean material ---
        segs = selectSegments(Lfeat, Rfeat, params, ctx.leftRightBias)
        if totalSeconds(segs) < 2.0: model.flags.insufficient = true   # warn; Static-only
        clean = concatSegments(ctx, segs)            # full audio

        # --- 3. Tonal estimation (produces residual) ---
        tonal = estimateTonal(clean.audio, sr, params, scratch)   # spectral-notch residual
        model.tonalPerChannel[ch] = tonal.layer
        clean.residual = tonal.residual
        checkCancel()

        # --- 4. Noise profile from residual + full ---
        model.noisePerChannel[ch] = buildNoiseProfile(clean.residual, clean.audio, sr, ...)

        # --- 5. Grain corpus from residual ---
        corpus = extractGrains(clean.residual, segs, sr, params)
        if corpus.usableCount < 20: model.flags.loopRisk = true
        model.grainsPerChannel[ch] = corpus
        checkCancel()

        # --- 6. Boundary capture (both seams) ---
        model.boundariesPerChannel[ch].pre  = captureBoundary(ctx.leftContext[ch],  leftSeam,  Pre,  tonal.layer, sr)
        model.boundariesPerChannel[ch].post = captureBoundary(ctx.rightContext[ch], rightSeam, Post, tonal.layer, sr)

    model.modelId = hash(ctx.sourceContentHash, analysisParams)
    setDiagnosticsFlags(model)            # speechContaminationHigh, loopRiskHigh, learnSeconds
    return model      # immutable from here
```

---

## 5. End-to-end render pass (pseudocode)

```
RenderOutput render(AmbienceModel m, RenderSettings s):
    require(s.targetDurationSamples > 0)
    n      = s.targetDurationSamples
    tail   = msToSamples(s.crossfadeMs, s.targetSampleRate)
    nWork  = n + 2*tail                                   # render long, trim later
    StftScratch scratch
    seeds  = splitSeed(s.seed)   # {noise, pick, jitter, macro} per channel

    for ch in channels:
        scratch.zero()
        # --- Tonal: phase-docked to the pre-gap seam, free-run across gap ---
        synthTonalLayer(scratch.tonal, nWork, m.tonalPerChannel[ch],
                        m.boundariesPerChannel[ch].pre, s.tonalRetention, sr)

        # --- Noise bed: seeded white -> STFT-shaped to residual LTAS ---
        synthNoiseBed(scratch.noise, nWork, m.noisePerChannel[ch],
                      seeds.noise[ch], sr, scratch)
        noiseGain = (s.mode==Static) ? 1.0 : (1.0 - 0.5*s.textureAmount)  # leave room for grains

        # --- Grains: Hybrid/Complex only ---
        if s.mode != Static:
            startFeat = featuresOf(m.boundariesPerChannel[ch].pre)   # match the incoming texture
            GrainCursor cur
            synthGrainLayer(scratch.grains, nWork, m.grainsPerChannel[ch], startFeat,
                            s.textureAmount, s.randomness, seeds.pick[ch], seeds.jitter[ch], cur)

        # --- Mix layers ---
        mix = scratch.tonal + noiseGain*scratch.noise + s.textureAmount*scratch.grains

        # --- Macro movement: Complex only, endpoints anchored to seam levels ---
        if s.mode == Complex:
            applyMacroEnvelope(mix, nWork, depthFrom(s.movement), rateFrom(s.movement),
                               preRms, postRms, seeds.macro[ch], sr)

        # --- Spectral + energy matching ---
        matchOutputLtas(mix, nWork, m.noisePerChannel[ch].fullLtas, sr, scratch)  # gentle
        normalizeEnergy(mix, nWork, m.noisePerChannel[ch].targetRms)

        # --- Edge conditioning: phase-align + equal-power crossfade into real seams ---
        applyEdgeConditioning(mix, nWork, m.boundariesPerChannel[ch].pre,
                              m.boundariesPerChannel[ch].post, s.crossfadeMs, sr)

        # --- Trim to exact duration (drop the tails) ---
        out.channels[ch] = mix[tail : tail+n]

    # --- Stereo decorrelation (independent noise seeds already; widen grains) ---
    if channels==2: applyStereoWidth(out, s.stereoWidth)
    return out      # deterministic for (m, s)
```

Determinism note: the same `(modelId, s.hash())` returns the cached buffer; otherwise the
seed sub-streams guarantee bit-identical re-render. Regenerate = processor bumps `seed` → new
hash → new render.

---

## 6. Dependency graph (analysis outputs → synthesis inputs)

```
                         AnalysisContext (ARA reads, resampled)
                                      │
                          ┌───────────┴───────────┐
                          ▼                       ▼
                   FrameFeatures(L)        FrameFeatures(R)
                     (rms,flat,flux,         (+ speechScore,
                      crest,harmonicity)       transient flags)
                          └───────────┬───────────┘
                                      ▼
                              selectSegments ──► LearnSegments ──► CleanMaterial.audio
                                                                        │
                                              ┌─────────────────────────┤
                                              ▼                         │
                                       estimateTonal ───► TonalLayer ───┼─────────────┐
                                              │                         │             │
                                              └─► residual ─────────────┤             │
                                                        ┌───────────────┤             │
                                                        ▼               ▼             │
                                               buildNoiseProfile   extractGrains      │
                                                        │               │             │
                                                   NoiseProfile     GrainCorpus       │
                                                        │               │             │
   CleanMaterial.audio + TonalLayer ──► captureBoundary ──► BoundaryConditionProfile  │
                                                        │               │      │      │
                                                        ▼               ▼      ▼      ▼
   ════════════════════════ AmbienceModel (immutable) ═══════════════════════════════
                                                        │
                 RenderSettings ──────────────────────► │
                                                        ▼
         synthTonal ◄── TonalLayer + Boundary.pre(phase dock)
         synthNoise ◄── NoiseProfile.residualLtas + seed
         synthGrain ◄── GrainCorpus + Boundary.pre(startFeat) + seeds      (Hybrid/Complex)
         macroEnv   ◄── movement + Boundary pre/post levels                (Complex)
              │
              ▼  mix → matchOutputLtas(fullLtas) → normalize(targetRms)
              ▼  applyEdgeConditioning(Boundary.pre, Boundary.post)
              ▼  trim → stereo width
            RenderOutput
```

Key consumption facts:
- `Boundary.pre` is consumed **twice**: tonal phase docking AND grain start-feature seeding.
- `residual` feeds **both** noise (LTAS) and grains; tonal feeds synthesis AND boundary phase.
- Nothing in synthesis re-reads `AnalysisContext` — the model is the complete contract.

---

## 7. V1 / V1.1 / Later split

### V1 (must ship — narrow, credible)
- STFT (2048/512/Hann), FrameFeatures (rms, centroid, flatness, flux, crest, lowBandRatio).
- Speech reject via **harmonicity only** + transient reject (crest/flux).
- Ambience scoring + segment selection + left/right bias.
- Tonal: parabolic peaks + greedy linking + presence filter; **spectral-notch residual**.
- LTAS (1/6-oct) → NoiseProfile (residual/full, 3 bands, targetRms).
- Grain extract + features + **brute-force similarity** concat with anti-repeat + seeded jitter.
- **Static + Hybrid** render; energy normalize; gentle LTAS match.
- Boundary capture + **phase dock + equal-power crossfade + seam RMS match**.
- Deterministic seed, render cache, exact-duration trim, mono + stereo (indep noise, widened grains).

### V1.1 (fast follow — depth & realism)
- **Complex mode**: macro envelope (level + HF tilt, anchored endpoints).
- Tonal zero-pad to 4096 for finer freq; slow per-partial amplitude envelopes.
- Speech cue 2: **4–8 Hz syllabic-rate modulation** in 1–4 kHz.
- Boundary **tilt-match shelf**; per-band (3–4) slow noise modulation.
- Preview-in-context (splice fill between real neighbour snippets).
- 4th frequency band; corpus-size-aware grain prominence auto-scaling.

### Later (V2+ — only if justified by use)
- kNN adjacency + **Viterbi least-cost concatenation path** (replaces greedy nearest).
- Cepstral/autocorr pitch refinement for speech; optional tiny learned VAD.
- True **multichannel** beds (per-channel + inter-channel correlation matrix, 5.1/7.1).
- ARA formal analysis-task lifecycle (host progress/persistence) instead of private worker.
- Spectral-repair-style transient interpolation across the boundary.
- Match-EQ to an external reference clip; reusable project-wide noise-print library.

---

## Implementation order (maps to scaffold + roadmap)

1. `dsp/FFTWrapper` + `Windows` + `StftScratch` + COLA test.
2. `computeFeatures` + `FrameFeatures` (+ unit tests vs sine/noise/impulse).
3. `scoreAmbience` + `selectSegments` (timeline overlay uses these scores).
4. `estimateTonal` (peaks → link → notch residual) — the make-or-break for hum seams.
5. `buildNoiseProfile` + `synthNoiseBed` → **Static mode end-to-end** (first audible output).
6. `extractGrains` + `grainDistance` + `synthGrainLayer` → **Hybrid mode**.
7. `captureBoundary` + `applyEdgeConditioning` (phase dock first; it dominates transparency).
8. `applyMacroEnvelope` → **Complex mode** (V1.1).
```
```
