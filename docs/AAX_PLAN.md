# ToneFill — AAX AudioSuite build plan (Pro Tools)

Goal: a separate, native AAX **AudioSuite** build for Pro Tools that preserves every ToneFill
function. Companion to [`TECHNICAL.md`](TECHNICAL.md). The existing `aax/` scaffold loads in PT and
is PACE-signable, but is stale (pre-rework parameters) — this plan brings it to full parity.

---

## 1. How AAX AudioSuite actually works

**AudioSuite = Pro Tools' offline, file-based rendering.** The user selects a clip range, opens the
plugin from the AudioSuite menu, tweaks, optionally hits **Preview** (loops the selection through
the render path live), then **Render** — PT writes a new file that replaces the selection.

A native AAX effect is three cooperating objects, registered in `Describe`
(`GetEffectDescriptions`):

| Object | Role |
|---|---|
| `AAX_CEffectParameters` | Data model: parameters, save/restore chunks, **custom data blobs** |
| `AAX_CHostProcessor` | Offline engine: `PreRender → RenderAudio (window by window) → PostRender` |
| `AAX_CEffectGUI` | Optional window; without it PT auto-generates a generic slider panel |

Key affordances (all verified in aax-sdk-2-9-0):

1. **`AAX_eProperty_UsesRandomAccess = true`** → the processor may call **`GetAudio()`** to read
   ANY region of the source file at any time (not just the window being rendered), and PT shows the
   native **WHOLE FILE** button. This is the mechanism analysis plugins (e.g. iZotope RX Ambience
   Match) are built on, and exactly what ToneFill's analyse-then-generate model needs.
   `GetSrcStart()/GetSrcEnd()` give the selection bounds.
2. **Preview** drives the same `RenderAudio` path; `AAX_eNotificationEvent_ASPreviewState` tells the
   Parameters object when preview starts/stops.
3. **GUI**: `AAX_IViewContainer::GetPtr()` returns the native view handle (NSView on macOS, HWND on
   Windows). A `juce::Component` can be attached to that handle — the same technique JUCE's own AAX
   wrapper uses — so **our restyled JUCE UI can be reused nearly wholesale** inside a native AAX GUI.
4. **Processor ↔ GUI data**: both sides hold the Parameters object. Knobs go through the standard
   AAX packet system; larger state (waveform preview, used/avail/seam readouts, manual ranges) goes
   through `SetCustomData/GetCustomData` + notifications.
5. **Why not the JUCE AAX wrapper**: JUCE-built AAX plugins run in AudioSuite only as linear
   "online-style" processors — no `AAX_CHostProcessor`, no `GetAudio`, no WHOLE FILE. ToneFill needs
   random access, hence the native client. (Pro Tools' ARA support is partner-limited and JUCE does
   not expose AAX-ARA; not a viable route.)
6. **Signing**: AAX must be PACE-signed (wraptool). We already have the Wrap GUID
   (53B16D60-73F3-11F1-B005-005056920FF7), signid ("Developer ID Application: Jakub Juchniewicz
   (5LSJ6C76Q2)") and account (cactuzz); `scripts/sign_aax.sh` exists; the LostComzz Windows CI
   already contains a working wraptool flow to copy. iLok must be connected when signing.

## 2. Feature mapping — every function preserved

| Plugin (ARA/VST3/AU) | AudioSuite equivalent |
|---|---|
| ARA reads the clip for analysis | `GetAudio()` random-access reads (selection or whole file) |
| **Full** (first 4 min vs whole item) | native **WHOLE FILE** button (free with UsesRandomAccess) |
| **Audition** (waveform window) | native **Preview** button (free) |
| **Export Len** / Export WAV | render length = the selection; PT writes the file (free) |
| Loop + tiling in `processBlock` | tile the seamless loop across `RenderAudio` windows |
| Clean Level, Voice Reject, Min Fill, Flatness, Chunk Size, Crossfade, Smoothness, Output | same params in `EffectInit` (float, 0..1 / real ranges) |
| **Enhance**, **Classic/Experimental** | discrete (bool) params |
| **Normalize** (LUFS/dBFS + target + "now") | baked into the render, measured on the ACTUAL rendered length (exactly the S7.3 semantics); "now" via CustomData |
| **Hiss Filter** (live LP, Freq/Q) | baked into the render (offline), heard in Preview too |
| **Regenerate** (seed) | int "Seed" param; GUI button bumps it → re-render |
| **Manual** waveform selection | our GUI waveform (source read via GetAudio) + drag ranges stored via CustomData; processor honours them |
| used / avail / seam / in readouts | processor → CustomData → GUI |

Nothing in the current feature set fails to map; three items (Full, Audition, Export) actually get
**simpler** because Pro Tools provides them natively.

## 3. Phased plan

### Phase A — engine parity, no custom GUI (small)
Bring `aax/` to today's engine. PT's auto-generated panel is ugly but complete; the SOUND is right.
- Params: replace the stale set (Variation/Movement/Complex) with the full current set incl.
  minFill, flatness, enhance, statistical, hissFilter/hissFreq/hissQ, normalize on/target/unit, seed.
- `analyzeAndBuild`: mirror FillWorker exactly — `AnalysisContext` with flatness/minFill/statistical
  → render (Ambience + paulStretch flag, loop length from availSec 15–60 s) → seamless loop fold →
  hiss-filter bake (when enhance+hiss on) → normalize measured on the FULL rendered selection length.
- Model cache keyed on (src range, analysis params) so Preview → Render doesn't re-analyse.
- Known past AS bugs are already fixed at the engine level (grainLen ≤ srcLen/4 clamp; the native
  client reads params directly, no shared SessionState).

### Phase B — dedicated AudioSuite GUI (the big chunk)
Deliberately a SEPARATE view, not a port of MainView. The AudioSuite workflow differs enough
(no Export Len / Audition / Full — PT provides selection length, Preview and WHOLE FILE natively;
adds Preview→Render cycle + Seed) that sharing the main view would mean hiding a third of it behind
host checks. What IS shared: `tonefill_engine` (the sound) and `ToneFillLookAndFeel` (the look).
- `AAX_CEffectGUI` subclass: on `CreateViewContents`/`SetViewContainer`, attach a JUCE component to
  `GetPtr()`'s NSView/HWND (`addToDesktop` with the parent handle).
- New compact `ASView` (JUCE, same LookAndFeel/logo): Detection + Structure + Texture knobs, Enhance
  + Classic/Experimental + Hiss panel, Normalize, Seed/Regenerate, waveform with manual
  drag-selection and used/avail/seam readouts.
- Wire knobs → AAX params; waveform/readouts/manual-ranges ↔ CustomData + notifications.

### Phase C — packaging, signing, CI
- macOS: extend `scripts/build_installer.sh` with the signed `.aaxplugin`
  (`/Library/Application Support/Avid/Audio/Plug-Ins`).
- Windows: extend `installer/windows/ToneFill.iss` (`Common Files\Avid\Audio\Plug-Ins`) and
  `build-windows.yml` with the AAX build + wraptool signing (copy the LostComzz CI pattern:
  Eden SDK install, sign_only/use_signed variants; AAX SDK fetched from a private source — it must
  not be committed to the public repo).
- PT rescans plug-ins only on relaunch — document that in the manual.

## 4. Risks / notes
- Analysis runs inside the first `RenderAudio` call → blocks that pass for a few seconds on long
  sources. Offline this is fine (PT shows progress); Preview feels it once, then the model cache
  hides it.
- PACE signing needs the iLok connected (past "LICENSE ERROR" was environmental, not code).
- AAX GUI event handling differs per platform (mouse capture quirks with hosted views) — budget
  polish time in Phase B.
- Keep `PLUGIN_MANUFACTURER_CODE`-equivalent IDs consistent: AAX ManufacturerID should stay the
  'Czsd'-family so PT groups vendors consistently with Logic/VST3 (verify `kManufactureID` in
  `ToneFillAS_Defs.h` matches).
