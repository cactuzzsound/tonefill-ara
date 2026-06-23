# ToneFill ARA — Sprint Backlog

Assumptions: ~2-week sprints, one strong solo JUCE/C++ dev. Points: 1≈½ day, 2≈1 day,
3≈2 days, 5≈3–4 days. Task IDs are stable references for dependencies. Copy blocks directly
into a tracker. "TA-#" = test-audio asset defined in the sprint.

Legend: `deps:` blocking task IDs · `risk:` main hazard · `val:` how to validate ·
`DoD` = sprint definition of done.

---

## Sprint 0 — Build bring-up & ARA plumbing
Goal: the scaffold compiles to VST3/AU, loads in two hosts, passes audio through.

- [ ] **TF-001** Add JUCE, ARA SDK, VST3 SDK as pinned submodules under `external/`. (2) — deps: none
- [ ] **TF-002** Make `tonefill_engine` build standalone (JUCE-only link). (1) — deps: TF-001
- [ ] **TF-003** Implement `tonefill_configure_ara()` for the pinned JUCE version (real ARA SDK path injection). (3) — deps: TF-001 — risk: JUCE-version-specific helper name/mechanism
- [ ] **TF-004** Build `ToneFillPlugin` VST3+AU; resolve all scaffold compile/link errors. (3) — deps: TF-002, TF-003
- [ ] **TF-005** Add `juce::AudioProcessorARAExtension` to `PluginProcessor`; derive `DocumentController` from `juce::ARADocumentControllerSpecialisation`; export ARA factory. (5) — deps: TF-004 — risk: ARA factory wiring is the trickiest plumbing
- [ ] **TF-006** Add `ARAPlaybackRenderer` subclass (returns silence for now). (3) — deps: TF-005
- [ ] **TF-007** CI: configure + build engine + plugin headless on mac/win; run `pluginval` (strictness 5). (3) — deps: TF-004
- [ ] **TF-008** `ctest` runs existing determinism/RNG/hash tests in CI. (1) — deps: TF-002

Risks: ARA factory registration; JUCE/ARA version drift. Keep TF-003/005 timeboxed; if blocked, ship Sprint 0 as non-ARA insert and carry ARA wiring into Sprint 1.
Validation: plugin scans in Reaper (ARA) + one of Logic/Studio One; `pluginval` green; passthrough audible.
Test audio: **TA-0** any 48k/24-bit stereo dialogue clip (sanity only).
DoD: VST3+AU load in 2 hosts, pass audio, `pluginval` + `ctest` green in CI, ARA factory recognized (even if renderer is silent).

---

## Sprint 1 — ARA source reading + export safety net
Goal: read clean ambience around a region via ARA and write it to WAV.

- [ ] **TF-101** Implement `ARAIntegrationFacade::detectCapabilities()` → populate `CapabilityProfile`. (3) — deps: TF-005
- [ ] **TF-102** `acquireContext()`: create `juce::ARAAudioSourceReader` for the region's source; check `isSampleAccessEnabled`; read left/right learn windows + boundary buffers. (5) — deps: TF-101 — risk: sample access disabled / host timing of reader availability
- [ ] **TF-103** Resample reads to `analysisSampleRate = min(srcSR,48k)`; compute `sourceContentHash`. (3) — deps: TF-102
- [ ] **TF-104** Implement `exportToWav()` end-to-end (already drafted) + a "dump acquired context" debug action. (2) — deps: TF-103
- [ ] **TF-105** Non-ARA / no-reader fallback path → Manual Capture stub + manual duration entry; UI reads `CapabilityProfile`. (3) — deps: TF-101
- [ ] **TF-106** Wire `DiagnosticsLogger` warnings: sample-rate mismatch, host unsupported, insufficient window. (1) — deps: TF-103

Risks: hosts differ on when readers/sample-access are valid; reading on the wrong thread. Always read on the analysis worker.
Validation: export the acquired left/right context to WAV; confirm by ear + waveform it is the audio flanking the selected region; verify resample correctness (sine sweep in → spectrogram unchanged shape).
Test audio: **TA-1** 10 s room-tone-only clip (no speech); **TA-2** dialogue clip with a clean 3 s ambience handle before and after a line; **TA-3** a 44.1k clip (to exercise resample path).
DoD: from a real ARA host, acquired flanking audio round-trips to a correct WAV; resample verified; fallback path shows correct UI without crashing.

---

## Sprint 2 — DSP foundation (STFT + features)
Goal: spectral front-end and per-frame features with unit tests.

- [ ] **TF-201** `dsp/FFTWrapper` (juce::dsp::FFT) + `Windows` (Hann/Tukey) + COLA helper. (3) — deps: TF-002
- [ ] **TF-202** `StftScratch` + forward/inverse STFT with overlap-add; assert COLA sums to unity. (3) — deps: TF-201
- [ ] **TF-203** `computeFeatures()` → `FrameFeatures` (rms, centroid, flatness, flux, crest, lowBandRatio). (5) — deps: TF-202
- [ ] **TF-204** Unit tests: FFT round-trip; flatness≈1 for white noise, ≈0 for sine; centroid of known tones; flux spike on impulse. (3) — deps: TF-203
- [ ] **TF-205** Micro-benchmark STFT+features on 10 s @48k; record baseline ms. (1) — deps: TF-203

Risks: window/COLA mistakes silently corrupt everything downstream — test first.
Validation: TF-204 green; spectrogram of ISTFT(STFT(x)) ≈ x within tolerance.
Test audio: **TA-4** synthetic: white noise, pink noise, 1 kHz sine, impulse train, 50/60 Hz hum, swept sine (generated in test fixtures, checked into `tests/fixtures/`).
DoD: STFT round-trips within −60 dB error; all feature unit tests pass; baseline perf recorded.

---

## Sprint 3 — Candidate selection (ambience / speech / transient)
Goal: pick clean learn material; reject speech and transients; visualize it.

- [ ] **TF-301** `scoreSpeech()` V1: autocorrelation pitch salience (80–400 Hz) → `harmonicity`/`speechScore`. (5) — deps: TF-203 — risk: false-negatives bake phonemes into the bed; bias conservative
- [ ] **TF-302** Transient rejection: crest > 12 dB OR flux z-score > 2.5 → `rejectedTransient`. (2) — deps: TF-203
- [ ] **TF-303** `scoreAmbience()` weighted scorer (flatness/stationarity/proximity − speech/transient/level-outlier). (3) — deps: TF-301, TF-302
- [ ] **TF-304** `selectSegments()` contiguous grouping, min 150 ms, left/right bias, best-first. (3) — deps: TF-303
- [ ] **TF-305** `CleanMaterial` assembly (concat accepted segments per channel). (2) — deps: TF-304
- [ ] **TF-306** `TimelineView` overlay: green/yellow/red per-frame scoring on the mini-waveform. (5) — deps: TF-304
- [ ] **TF-307** Diagnostics: emit `INSUFFICIENT_MATERIAL`, `SPEECH_CONTAMINATION` from real thresholds. (1) — deps: TF-305

Risks: speech detector tuning is subjective; over-rejection starves the corpus. Make thresholds params (already in `ParameterState`).
Validation: on TA-2, selector picks the ambience handles and rejects the spoken line; overlay matches a hand-labeled reference within ~±100 ms.
Test audio: **TA-2** (reuse); **TA-5** dialogue with mouth clicks/plosives in the handle (transient reject); **TA-6** wall-to-wall speech (insufficient-material warning fires).
DoD: selector + overlay agree with hand labels on TA-2/TA-5; warnings fire correctly on TA-6; thresholds adjustable from UI.

---

## Sprint 4 — Tonal estimation + residual
Goal: extract stable hums/HVAC and produce a clean residual.

- [ ] **TF-401** `pickPeaks()`: per-frame peak picking + parabolic interpolation (freq/amp/phase) over noise floor +9 dB. (5) — deps: TF-202 — risk: peak threshold vs hum harmonics
- [ ] **TF-402** `linkPeaks()`: greedy nearest-frequency linking (≤3%), birth/death tracks. (3) — deps: TF-401
- [ ] **TF-403** Presence/std filter → `model::Partial[]` (presence ≥0.6, low freq std). (2) — deps: TF-402
- [ ] **TF-404** Spectral-notch residual: attenuate ±2 bins per partial per frame → ISTFT. (3) — deps: TF-403 — risk: notch coloration / phasiness near partials
- [ ] **TF-405** Unit test: synthetic 60 Hz + harmonics + noise → recovered freqs within ±0.5 Hz; residual hum energy −20 dB vs input. (3) — deps: TF-404
- [ ] **TF-406** Capture per-partial reference phase for boundary docking. (2) — deps: TF-403

Risks: this is the make-or-break for inaudible hum seams. Budget extra tuning time.
Validation: TF-405 numeric test; spectrogram of residual shows hum lines removed without broadband gouging.
Test audio: **TA-7** room tone with strong 50 Hz hum + harmonics; **TA-8** HVAC drone (broadband + tonal); **TA-1** (clean, should yield ~no partials).
DoD: tonal tracker recovers hum within tolerance on TA-7; residual hum suppressed ≥20 dB with no audible broadband artifact; reference phases stored.

---

## Sprint 5 — Static mode (first audible output) + boundaries
Goal: render believable tonal+noise fill of exact length with inaudible seams.

- [ ] **TF-501** `buildNoiseProfile()`: residual/full LTAS (1/6-oct), 3-band energies, targetRms. (3) — deps: TF-404
- [ ] **TF-502** `synthNoiseBed()`: seeded white → STFT-shaped to residual LTAS → ISTFT, length = target+tails. (5) — deps: TF-501
- [ ] **TF-503** `synthTonalLayer()`: additive oscillators, phase-docked to `Boundary.pre`. (3) — deps: TF-406
- [ ] **TF-504** `captureBoundary()`: seam snippet, RMS, tilt, per-partial seam phase. (3) — deps: TF-406
- [ ] **TF-505** `applyEdgeConditioning()`: equal-power crossfade + tonal phase align + seam RMS match. (5) — deps: TF-503, TF-504 — risk: seam audibility
- [ ] **TF-506** `matchOutputLtas()` (gentle) + `normalizeEnergy()` + exact-duration trim. (3) — deps: TF-502
- [ ] **TF-507** Wire `AmbienceRenderer::renderStatic()` end-to-end; `RenderJob` returns real audio. (3) — deps: TF-502, TF-503, TF-505, TF-506
- [ ] **TF-508** Boundary transparency test: [real pre | fill | real post] seam RMS/flux delta below threshold. (3) — deps: TF-507
- [ ] **TF-509** Update golden render regression to assert determinism on real output. (2) — deps: TF-507

Risks: seam clicks from phase/level mismatch; layer double-counting energy.
Validation: TF-508 automated seam test; blind A/B vs real tone on TA-1/TA-7.
Test audio: **TA-1**, **TA-7**, **TA-8**; **TA-9** a hand-cut gap with known-good real tone on both sides (ground truth for seam test).
DoD: Static fill on TA-1/TA-7 is transparent in informal blind A/B; seam test passes on TA-9; output is exact-length and deterministic (golden test green).

---

## Sprint 6 — Hybrid mode (granular texture)
Goal: add lifelike textural movement without loopiness.

- [ ] **TF-601** `extractGrains()`: Tukey grains (120 ms/50%) from residual over accepted segments; decimate to budget. (5) — deps: TF-404, TF-305
- [ ] **TF-602** `computeGrainFeatures()` + z-normalization across corpus. (3) — deps: TF-601
- [ ] **TF-603** `grainDistance()` + `pickNextGrain()` brute-force with anti-repeat (M=8) + seeded jitter. (3) — deps: TF-602
- [ ] **TF-604** `synthGrainLayer()`: overlap-add concat, start grain matched to `Boundary.pre` features. (5) — deps: TF-603
- [ ] **TF-605** Wire `renderHybrid()`: tonal + reduced noise + grains, balanced by `textureAmount`. (3) — deps: TF-604, TF-507
- [ ] **TF-606** Loopiness detector: autocorrelation/periodicity score on output; assert below threshold. (3) — deps: TF-605
- [ ] **TF-607** Short-corpus graceful degradation: auto-reduce grain prominence, lean on noise when usableCount low. (2) — deps: TF-605

Risks: audible looping on short source; comb filtering at grain seams; grain/tonal phase fighting.
Validation: TF-606 on long fills from short source; A/B Hybrid vs Static on textured rooms.
Test audio: **TA-10** street/exterior tone (moving texture); **TA-11** room with intermittent distant events; **TA-12** very short (3 s) source to force loop stress.
DoD: Hybrid beats Static on TA-10/TA-11 in informal listening; loopiness score below threshold on TA-12 60 s render; degradation path verified.

---

## Sprint 7 — Complex mode + render pipeline UX (regenerate/preview/cache)
Goal: macro movement, RT-safe preview, instant regenerate.

- [ ] **TF-701** `applyMacroEnvelope()`: seeded random-walk level + HF tilt, endpoints anchored to seam levels. (5) — deps: TF-605 — risk: over-modulation / endpoint mismatch
- [ ] **TF-702** Wire `renderComplex()` (Hybrid + macro env), gated by `movement`. (2) — deps: TF-701
- [ ] **TF-703** Move `RenderJob` execution onto a single render worker; marshal `DoneCallback` to message thread. (5) — deps: TF-507
- [ ] **TF-704** Render cache hit on (modelId, settings.hash); Regenerate = processor bumps seed → requestRender. (3) — deps: TF-703
- [ ] **TF-705** RT-safe preview handoff: `atomic<const PreviewBuffer*>` + message-thread retire-list; preview playback in `processBlock`. (5) — deps: TF-703 — risk: real-time safety
- [ ] **TF-706** Preview/Regenerate/Cancel UI wiring + progress reporting. (3) — deps: TF-704, TF-705
- [ ] **TF-707** Stereo: independent noise seeds + grain decorrelation + `stereoWidth`; coherence test. (3) — deps: TF-605

Risks: audio-thread RT-safety (no locks/allocs/shared_ptr); cancellation races.
Validation: thread sanitizer clean; preview never xruns; regenerate returns a different-but-coherent take instantly from cache path.
Test audio: **TA-8**, **TA-10** (movement audible); **TA-13** stereo room tone with real L/R decorrelation.
DoD: Complex adds audible life without artifacts; preview is RT-safe (TSan clean, no dropouts); regenerate < 1 s; stereo coherence within target on TA-13.

---

## Sprint 8 — Persistence, capability fallbacks, UX polish
Goal: survive reopen, degrade cleanly, present warnings well.

- [ ] **TF-801** ARA archive store/restore of `AmbienceModel` (versioned, `modelVersion` check). (5) — deps: TF-605 — risk: archive format drift / source moved
- [ ] **TF-802** Re-analyze-on-version-mismatch + "source changed" detection path. (3) — deps: TF-801
- [ ] **TF-803** `WarningsView`: drain `DiagnosticsLogger::snapshot()` on a timer; flag offending learn region on timeline. (3) — deps: TF-307
- [ ] **TF-804** `ControlsPanel`: all §L params + analysis-affecting params highlight the "Analyze" button. (3) — deps: TF-704
- [ ] **TF-805** Manual Capture mode fully functional (record buffer → same analysis path). (5) — deps: TF-105, TF-507
- [ ] **TF-806** Export-only commit path when `!canRequestRender` (drag-out / save dialog). (2) — deps: TF-104
- [ ] **TF-807** Quality default presets + "poor material → suggest Static" gating. (2) — deps: TF-803

Risks: archive/restore across host sessions; param-change → cache invalidation correctness.
Validation: save session, reopen → model restored, no re-analysis; corrupt/missing source handled; warnings actionable.
Test audio: reuse TA-2/TA-6/TA-7; **TA-14** a session file saved in each target host.
DoD: reopen restores model in all target hosts; every warning state reachable and clear; Manual Capture + export-only paths work end-to-end.

---

## Sprint 9 — Hardening & release
Goal: ship-quality across the host matrix.

- [ ] **TF-901** Host matrix pass: Reaper, Studio One, Cubase/Nuendo, Logic (AU) — document quirks. (5) — deps: TF-801
- [ ] **TF-902** Perf budgets: analysis < X s/10 s audio; render < 1 s/30 s fill; enforce in CI bench. (3) — deps: TF-205
- [ ] **TF-903** Memory caps: grain corpus ≤ 64 MB/ch, target duration warn > 60 s; verify under stress. (3) — deps: TF-601
- [ ] **TF-904** Long-duration stress: 60 s fill from 3 s source — no clip/drift/excess repetition, bounded memory. (3) — deps: TF-606
- [ ] **TF-905** Error handling sweep: every `Status` path surfaces a user message, never a crash. (3) — deps: TF-803
- [ ] **TF-906** Code signing + notarization (mac), installers (mac/win). (5) — deps: TF-901 — risk: signing/notarization friction
- [ ] **TF-907** User manual + quick-start; in-plugin tooltips for warnings. (3) — deps: TF-804
- [ ] **TF-908** Subjective listening panel: scored A/B vs real tone across TA library; log results. (3) — deps: TF-702

Risks: notarization; rare-host ARA bugs found late.
Validation: full matrix green; bench within budget; signed installers verified on clean machines.
Test audio: full **TA-1…TA-14** library + 2–3 real production DX scenes.
DoD: stable across host matrix, perf/memory within budget, signed installers, manual shipped, listening panel ≥ target transparency score → **V1 release**.

---

## Test-audio library (build once, reuse across sprints)

| ID | Content | First needed |
|---|---|---|
| TA-0 | Any 48k stereo dialogue (sanity) | S0 |
| TA-1 | 10 s clean room tone, no speech | S1 |
| TA-2 | Dialogue line with clean 3 s handles either side | S1 |
| TA-3 | 44.1k clip (resample path) | S1 |
| TA-4 | Synthetic: white/pink/sine/impulse/hum/sweep (fixtures) | S2 |
| TA-5 | Handles with mouth clicks/plosives | S3 |
| TA-6 | Wall-to-wall speech (insufficient material) | S3 |
| TA-7 | Room tone + strong 50 Hz hum + harmonics | S4 |
| TA-8 | HVAC drone (tonal + broadband) | S4 |
| TA-9 | Hand-cut gap with ground-truth tone both sides | S5 |
| TA-10 | Street/exterior moving texture | S6 |
| TA-11 | Room with intermittent distant events | S6 |
| TA-12 | Very short (3 s) source (loop stress) | S6 |
| TA-13 | Stereo room tone, real L/R decorrelation | S7 |
| TA-14 | Saved session per target host | S8 |

---

## Cross-sprint dependency spine (critical path)

```
S0 build/ARA ─► S1 ARA read ─► S2 STFT/features ─► S3 selection ─► S4 tonal/residual
                                                                        │
                         ┌──────────────────────────────────────────────┤
                         ▼                                              ▼
                   S5 Static + boundaries ──► S6 Hybrid (grains) ──► S7 Complex + RT preview
                                                                        │
                                                                        ▼
                                                       S8 persistence/UX ─► S9 hardening/release
```
Hard gates: TF-202 (COLA) blocks all DSP; TF-404 (residual) blocks both noise and grains;
TF-505 (edge conditioning) gates perceived quality; TF-703/705 (worker + RT preview) gate
shippable UX.
