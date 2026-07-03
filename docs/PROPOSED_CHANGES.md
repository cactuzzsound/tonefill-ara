# ToneFill — Proposed Changes (analysis & synthesis)

This document is a **design pool**, not a spec to implement as-is. It collects the incoming
analysis/synthesis proposals, an engineering triage against how ToneFill actually works, the bugs
found in the original sketches, and a phased build plan. It sits alongside [`TECHNICAL.md`](TECHNICAL.md).

---

## 1. Engineering triage — proposals vs reality

### High-confidence wins (no new deps)

- **A2 — Context-aware grain selection at joins**
  *Adopt.* Textbook concatenative / unit-selection move: choose grain starts by local descriptor
  similarity (RMS / spectral tilt) between the previous grain's tail and the candidate's head.
  Directly attacks audible seams; cheap.

- **A3 — Phase / zero-crossing anchoring**
  *Adopt.* Minimal cost, reduces OLA click/transient risk. Needs per-run anchor indexing and a small
  snap window.

- **S7.1 — Decouple loop length from Smoothness**
  *Adopt.* Known bug (`loopLen = 15 + smoothness·45`). Fixed by a real Loop Length parameter;
  Smoothness then controls only PaulStretch time/spectral smoothing.

- **S7.3 — Loudness normalize on the export buffer**
  *Adopt.* Already flagged in TECHNICAL.md — loudness must be measured on the actual deliverable
  (export-length crop), not the internal loop.

- **An4 — LTAS-based adaptive crossfade**
  *Adopt with dB fix.* Longer crossfades when neighbouring runs are further apart in spectrum is
  correct, but the mismatch must be computed in **log/dB** space. As written, the "3 dB" mapping over
  linear-RMS differences is mathematically meaningless.

- **An5 — Join-roughness diagnostic**
  *Adopt with flux front-end.* Useful "seamlessness risk" signal for the UI, but must use flux over a
  **smoothed spectrogram / band representation** to avoid the raw-noise jitter that killed the first
  spectral-flux attempt.

### Product framing / later wiring

- **S1 — FillCharacter (NaturalBed / InvisibleFill)**
  *Adopt later.* Correct user-visible framing, but it is a **bundle** of the primitives above
  (A2, A3, An4, An5, S7.1, S7.3, maybe S5). Land and audition those first, then wire them into preset
  profiles rather than baking presets into core logic prematurely.

### Cautious / experimental

- **S5 — Micro-modulation (slow level / spectral walk)**
  *Cautious.* Psychoacoustically right for "living" room tone, but can pump if depth is even slightly
  too high, and it fights strict loudness normalisation. Keep it ≤0.3 dB and glacially slow, or gate
  it behind NaturalBed only.

- **A2.1 — RNNoise-fused speech mask**
  *Reconsider.* RNNoise is trained to treat broadband ambience as **noise to suppress**, so it is
  fundamentally suspect on pure room tone — it will often flag the bed itself, especially at low SNR.
  It is also hard-locked to 48 kHz / 480-sample frames, clashing with the arbitrary-rate STFT and
  adding a second framing lattice. Worth an experiment behind a "Statistical mode (beta)" switch, not
  the primary path without long A/B listening.

### Rejected as written

- **S6 — Multi-run PaulStretch pre-mix (barycentric magnitude average)**
  *Reject.* Averaging magnitudes across **unaligned** runs does not add variety — it collapses toward
  the LTAS. Runs are not time-aligned, so per-frame magnitude averaging smears everything into the
  global average spectrum; log-domain averaging (geometric mean) further emphasises valleys, thinning
  the bed and removing the very micro-variation the idea was meant to add. If variety from multiple
  runs is wanted, either concatenate runs with better joins (An4) or cross-fade **whole
  runs/spectrograms over time** — do not average them into one static "super-run".

---

## 2. Review notes / known bugs in the original sketches

These sketches are conceptual; several would miscompile or misbehave as written:

1. **`FrameFeatures::mfcc`** — declared `float mfcc {};` (a single float). MFCCs are a fixed-length
   vector; should be `std::array<float, 13> mfcc {};` (or a sized `std::vector<float>`).

2. **`mixStableRuns` — vector vs element access** — uses `in.runs.numFrames` / `in.runs.numBins` as
   if `runs` were a struct, but it is a `std::vector<Spectrogram>`. Needs `in.runs[0].numFrames`, and
   must handle differing lengths (resample / pad), which the sketch glosses over. (Moot given S6 is
   rejected.)

3. **LTAS mismatch in linear space** — `computeLtasMismatch` subtracts `bandRms` directly while
   `chooseCrossfadeDuration` comments "3 dB RMS". If `bandRms` is linear amplitude, "3 dB" is
   meaningless. Compute the mismatch in log/dB first, then map to crossfade duration.

4. **`updateLevelMod` — static counter** — `static int counter` makes the function process-global.
   ToneFill's `SessionState` is deliberately per-instance to stop Reaper's per-item instantiation
   from clobbering other items. `counter` must live in a per-instance `LevelModState`.

5. **`SessionState` integration / atomics** — the sketches drop plain structs into `SessionState`
   (`AnalysisSettings analysisSettings;`), but the real `SessionState` uses `std::atomic` for every
   scalar on purpose: it is read lock-free from the audio thread and written from UI/worker with no
   mutex. New scalar params stay atomics; only truly worker-only bundles (classifier config, model
   handles) may live in a non-atomic struct guarded like `manualRanges` already is. Any new aggregate
   for cross-thread access needs a seqlock or explicit mutex.

6. **Dependencies / cost of Statistical mode** — RNNoise, GMM/MFCC and spectrogram pre-mix all imply
   a shipped model file and a real FFT / Mel front-end on every frame instead of the current
   FFT-free tilt proxy. That is a meaningful build / latency / maintenance step change and must be a
   separate **Statistical** pipeline, not "just alongside" the existing gates.

---

## 3. Phase 1 — Seamlessness slice (no ML, no new deps)

Goal: improve audible seamlessness of Enhance-OFF fills and loop joins without changing the core
analysis model or adding dependencies.

1. **Decouple loop length (S7.1)** — add a dedicated `Loop Length` parameter (5–120 s) to
   `SessionState`; the worker uses it for `loopLen`; remove the Smoothness coupling. Smoothness then
   controls only PaulStretch smoothing.

2. **Export-buffer loudness normalize (S7.3)** — measure LUFS/peak on the final export buffer
   (cropped to Export Len) and apply the gain there, so different export lengths share consistent
   loudness.

3. **Context-aware grain selection + zero-cross anchoring (A2 + A3)** — a small unit-selection cost
   at joins (RMS + tilt descriptors over ~40–80 ms of tail/head to pick grain starts); record
   zero-crossing / peak anchors during the `cleanAudioPerChannel` build and snap chosen starts to the
   nearest anchor within a small window. Keep heuristics simple and fast; tune by ear.

4. **LTAS-based adaptive crossfade (An4, dB fix)** — per-run band LTAS in log/dB; map neighbouring-run
   spectral mismatch to crossfade length (20–120 ms) instead of a fixed 20 ms. Concatenation joins
   only; Min Fill behaviour unchanged.

5. **Join-roughness diagnostic (An5, smoothed-flux front-end)** — flux over a band-smoothed
   spectrogram at joins vs away from joins; surface the ratio as a "seamlessness risk" hint in the UI.

Deliverable: a "seamlessness" release that makes Enhance-OFF beds and loops audibly smoother, with
zero ML and no new third-party libraries.

---

## 4. Phase 2 — Statistical mode experiment (separate branch)

Goal: explore RNNoise + classifier + MFCC front-end in a separate **Statistical** analysis pipeline,
A/B against Classic by ear, without risking the working path.

- Add `AnalysisModel::Statistical` and a UI toggle.
- Integrate RNNoise and a small classifier as an optional pipeline; keep FFT/MFCC work out of Classic.
- Measure CPU impact and audible behaviour on noisy dialogue beds and near-silence room tone.
- Merge only once it clearly beats Classic on both artefact rate and UX.

---

## 5. Deferred product framing

- **S1 FillCharacter** presets (NaturalBed / InvisibleFill) wired on top of the Phase 1 primitives.
- **S5 micro-modulation**, NaturalBed-only, tiny depth, decoupled from loudness normalisation.
