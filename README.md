# ToneFill

**Seamless room-tone / ambience fill for dialogue post.** ToneFill analyses a dialogue clip,
finds the clean room tone hiding between the words, and synthesizes a seamless, loopable bed of
that exact room — to fill gaps, extend handles, or smooth edits. Built on JUCE + ARA, with a
native AAX AudioSuite build for Pro Tools.

Current version: **0.9.3** (pre-1.0). Vendor: **cactuzz sound**.

---

## Formats & hosts

| Format | Hosts | Notes |
|---|---|---|
| **VST3 (ARA)** | Nuendo, Cubase, Reaper, Studio One | The main path. ARA lets it read the clip and replace the region in place, live. |
| **Audio Unit (ARA)** | Logic Pro | Same engine as VST3. |
| **AAX (AudioSuite)** | Pro Tools | Native offline AudioSuite build (`aax/`), not JUCE-hosted. Preview / Render / Whole-File are Pro Tools' own. |

Windows ships VST3 only (AU and AAX are macOS).

---

## What it does

1. **Analyse** the clip and detect clean room-tone regions — a gate stack over per-band levels,
   speech rejection (libfvad VAD + harmonicity + non-stationarity), stationarity/flatness, and a
   colour cluster anchored to the quietest material. Low frequencies (<500 Hz) are weighted so the
   room's low end stays rock-solid across joins.
2. **Select** enough clean material (Min Fill sets the floor).
3. **Synthesize** a seamless bed by grain-clouding the clean fragments (or PaulStretch in Enhance),
   with join-cost matching, zero-cross anchoring and LF-aware costs so the seams are inaudible.
4. **Loop** it: the fill is a seamless loop, tiled across the region (ARA) or written to a file (AAX
   Render / Export WAV).

### Controls

- **Auto / Manual** — auto-find clean tone, or drag the room-tone regions yourself on the waveform
  (**Expand** opens a large, zoomable waveform for precise selection).
- **Detection** — Clean Level, Voice Reject, Min Fill.
- **Structure** — Flatness, Bands (Spectral), Crossfade.
- **Texture** — Smoothness (Enhance only), Output, Export Len.
- **Mode: Classic / Experimental / Spectral**
  - *Classic* — the tuned gate stack (default).
  - *Experimental* — weighted per-frame statistical scoring.
  - *Spectral* — **per-band mosaic**: splits the source into power-complementary bands, finds clean
    room tone in each band independently, and recombines. Steadier low end, more usable material.
    The **Bands** knob sets how many bands.
- **Advanced (Spectral)** — opens the **Spectral Band Editor**: a spectrogram of the source
  (log-frequency, colour-mapped, RX-style) with **draggable band-edge lines** so you place each
  band's frequency range by hand. Zoom (H/V buttons), pan (wheel / side-wheel / drag), Speed and
  Bright controls.
- **Enhance** — PaulStretch resynthesis for when fragments won't blend into a clean bed. Enables
  **Smoothness** and the Enhance-only **Hiss Filter** (live HF de-hiss).
- **Normalize** — bake the output to a LUFS or dBFS-peak target (true-peak capped at −1 dBFS).
- **Bypass** — A/B: monitor the original source instead of the fill.
- **Full** — analyse the whole item (not just the first 10 minutes) to find clean tone scattered
  across a long take.
- **Regenerate** — a new random variation with the same settings.
- **Export WAV** (VST3/AU) — write the seamless fill to a file. In AAX, use AudioSuite **Render**.

Per-clip parameters (and Spectral band edges) are stored **per audio source** and persisted in the
ARA archive, so each clip keeps its own settings across a project save/reload.

---

## Build

Dependencies (macOS/Windows):

- **JUCE 8.0.12** — via the `external/JUCE` submodule *or* `-DJUCE_DIR=/abs/path/to/JUCE`.
- **ARA SDK** — `external/ARA_SDK` (pinned submodule).
- **libfvad** *(optional)* — `external/libfvad`; enables speech-aware selection (otherwise level-only).
  `git clone https://github.com/dpirch/libfvad external/libfvad`
- **AAX SDK** *(optional, for the Pro Tools build)* — `-DAAX_SDK_PATH=/abs/path/to/aax-sdk`.

```sh
git submodule update --init --recursive        # ARA_SDK (+ JUCE if used as a submodule)

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DJUCE_DIR=/abs/path/to/JUCE \
  -DTONEFILL_ENABLE_ARA=ON \
  -DAAX_SDK_PATH=/abs/path/to/aax-sdk-2-9-0        # omit to skip the AAX build

cmake --build build --target ToneFillPlugin_VST3 ToneFillPlugin_AU -j   # VST3 + AU (ARA)
cmake --build build --target ToneFillAS -j                              # AAX (AudioSuite)
ctest --test-dir build                                                  # engine unit + determinism tests
```

> CMake does **not** expand `~`, and zsh does not expand `~` after `=` — always pass **absolute**
> paths to `-DJUCE_DIR` / `-DAAX_SDK_PATH`. Moving the JUCE path requires a fresh `build/` (CMake
> refuses a cache whose JUCE source dir changed). Keep SDKs out of TCC-protected folders
> (`~/Downloads`, `~/Desktop`) so the toolchain can always read them.

`COPY_PLUGIN_AFTER_BUILD` installs the VST3/AU to `~/Library/Audio/Plug-Ins`. Hosts also scan the
system-wide `/Library/Audio/Plug-Ins`; if both exist the host may load the system copy, so deploy
there when testing.

### Offline harness (develop the DSP without a host)

`tonefill_offline` drives the whole engine on WAV files — no plugin host, no ARA:

```sh
./build/bin/tonefill_offline --in room_tone.wav --duration 3 --seed 42 --out fill.wav
./build/bin/tonefill_offline --in room_tone.wav --analyze-only     # diagnostics only
```

Disable with `-DTONEFILL_BUILD_TOOLS=OFF`.

---

## Installers & signing

**macOS** — a signed `.pkg` with a Customize pane to pick **AAX / VST3 / AU**:

```sh
# build VST3 + AU first, and sign the AAX (below); then:
scripts/build_installer.sh 0.9.3            # -> dist/ToneFill-0.9.3.pkg
scripts/build_installer.sh 0.9.3 --no-aax   # VST3 + AU only
```

VST3/AU are codesigned with *Developer ID Application* (hardened runtime); the package with
*Developer ID Installer*. Notarization is an optional step (see the script footer) needed only to
distribute to other machines.

**AAX signing** — the AAX is signed by **PACE wraptool** (never by `codesign`, which would break the
wrap). Sign it before packaging:

```sh
scripts/sign_aax.sh <ilok-account-id>       # iLok connected; installs to the Avid plug-ins folder
```

**Windows** — built in the cloud by GitHub Actions on `v*` tags
(`.github/workflows/build-windows.yml`): CMake VS 2022 build of the VST3, packaged by Inno Setup,
attached to the GitHub Release as `ToneFill-<ver>-Windows.exe` (+ a raw VST3 zip).

```sh
git tag -a v0.9.3 -m "ToneFill 0.9.3" && git push origin v0.9.3   # triggers the Windows CI + Release
```

---

## Repository layout

- `src/core/`   — host-agnostic primitives (Result, Ids, DiagnosticsLogger).
- `src/dsp/`    — low-level DSP (FFT, grain/concatenation, Linkwitz-Riley / power-complementary band
  split, VAD, loudness).
- `src/engine/` — analysis, synthesis, render, data model. **No ARA, no host.** Unit-testable.
- `src/plugin/` — JUCE AudioProcessor/Editor, ParameterState, the ARA boundary (`ara/`), and the UI
  (`ui/`, including the shared host-agnostic `SpectralEditor`).
- `aax/`        — native AAX AudioSuite HostProcessor + its GUI (reuses `SpectralEditor`).
- `tools/`      — the offline WAV harness.
- `tests/`      — Catch2 engine unit + determinism tests.
- `installer/`, `scripts/` — packaging & signing.

`tonefill_engine` (core/dsp/engine) is a static library that compiles and is testable independently
of ARA and of any host; both the JUCE plugin and the AAX build link it, so the DSP is identical
across all three formats.

---

## Architecture notes

- **ARA output is pulled, not pushed.** A `ToneFillPlaybackRenderer` supplies the fill when the host
  pulls audio; a `ToneFillEditorRenderer` does the same during Sample-Editor audition (without it,
  Nuendo/Cubase play the source through unaltered). Analysis + render run once on a background
  worker; the audio thread only copies the immutable fill.
- **State is shared per ARA audio source**, owned by the `DocumentController` (not per plug-in
  instance) — hosts split the editor and renderer across separate instances, so the editor and the
  worker must meet on the source they share. This is also what makes each clip keep its own settings.
- **The seed is plain processor state**, not an APVTS parameter (not automatable; round-trips
  exactly). Regenerate bumps it and re-renders.
- **AAX AudioSuite recomputes only during Preview/Render** (there is no live background pass like
  ARA), so the GUI shows a *recomputing…* hint while it catches up after a change.

### Threading

- **Message thread** — parameter edits, UI, job dispatch.
- **Analysis + render worker** — `AnalysisSession` → immutable `AmbienceModel` → rendered fill.
- **Audio thread** — lock-free read of the immutable fill only; never allocates or reads the source.
