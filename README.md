# ToneFill ARA — scaffold

ARA-first, offline room-tone / ambience fill plugin for dialogue post (JUCE + ARA + CMake).
This repository is an **architecture scaffold**: real DSP and ARA host integration are
marked with `TODO(dsp ...)` / `TODO(ARA ...)`.

## Layout

- `src/core/`     — host-agnostic primitives (Result, Ids, AudioBufferView, DiagnosticsLogger)
- `src/dsp/`      — low-level DSP utilities (SeededRng now; FFT/LTAS/grain ops later)
- `src/engine/`   — analysis, synthesis, render, data model. **No ARA, no host.** Unit-testable.
- `src/plugin/`   — JUCE AudioProcessor/Editor, ParameterState, and the ARA boundary (`ara/`).
- `tests/`        — Catch2 unit + determinism tests for the engine.

The `tonefill_engine` static library compiles and is testable independently of ARA.

## Build

Dependency setup and exact version pins are in **[docs/SETUP.md](docs/SETUP.md)**.
Mandatory submodules are **JUCE + ARA_SDK** only (VST3 support ships inside JUCE; Catch2 is
fetched by the test target).

```sh
git submodule update --init               # JUCE + ARA_SDK at pinned commits (see SETUP.md)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

## Offline harness (develop the DSP without a host)

`tonefill_offline` drives the whole engine on WAV files — no plugin host, no ARA. It decouples
DSP work (Sprints 2–7) from ARA integration, so the engine can be built and regression-tested
on files in parallel. With the DSP still stubbed it writes silence of the exact requested
length; that becomes real output as each stage lands.

```sh
./build/bin/tonefill_offline --in room_tone.wav --duration 3 --mode hybrid --seed 42 --out fill.wav
./build/bin/tonefill_offline --left pre.wav --right post.wav --duration 2 --mode static --out fill.wav
./build/bin/tonefill_offline --in room_tone.wav --analyze-only      # prints diagnostics only
```

Disable with `-DTONEFILL_BUILD_TOOLS=OFF`.

## What compiles today vs. what needs local SDKs

| Component | State |
|---|---|
| `tonefill_engine` (core/dsp/engine) | Compiles & links with **JUCE only**. |
| `tonefill_offline` (tools/) | Compiles & runs with **JUCE only**; end-to-end engine driver on WAV. |
| `tonefill_tests` (Catch2) | Compiles & passes once JUCE is present (Catch2 auto-fetched). |
| `ToneFillPlugin` (VST3/AU) | Compiles once **JUCE present**; ARA binding is functional only after the ARA SDK path is wired in `cmake/ConfigureARA.cmake` (`tonefill_configure_ara`). |

- **Will NOT build until JUCE is configured**: anything under `src/plugin/` (needs the JUCE
  plugin client) and the tests (need JUCE via the engine). Point CMake at JUCE via the
  `external/JUCE` submodule or `-DJUCE_DIR=/path/to/JUCE`.
- **Will NOT be a functional ARA plugin until the ARA SDK is wired**: the `IS_ARA_EFFECT`
  target builds, but `ARAIntegrationFacade` / `DocumentController` return
  `HostUnsupported` / empty capabilities until `TODO(ARA ...)` items are implemented.
  This is deliberate: the plugin degrades to Manual-Capture + WAV-export rather than
  hallucinating host behaviour.

### Corrected ARA assumptions (read before implementing `ara/`)

- The plugin can read only the **AudioSource(s) referenced by its own playback region**, not
  arbitrary timeline audio — fine for same-clip room tone, fails over to Manual Capture
  otherwise. Check `isSampleAccessEnabled` before reading.
- **Output is pulled by an `ARAPlaybackRenderer`**, not pushed to the host. Add that subclass
  plus `juce::AudioProcessorARAExtension` on the processor when wiring ARA. There is no
  "commit to host" call; WAV export is only the manual fallback.
- The render **seed is plain processor state**, not an APVTS parameter (not automatable, must
  round-trip exactly). "Regenerate" = processor bumps the seed and calls `requestRender`.

## Threading model (enforced by structure)

- **Message thread**: ParameterState edits, UI, job dispatch.
- **Analysis worker**: `AnalysisSession` → immutable `AmbienceModel`.
- **Render worker**: `RenderManager`/`RenderJob` → immutable rendered fill (currently inline;
  swap to a real worker in Phase 5, see `TODO(threading)`).
- **Audio thread**: lock-free read of the immutable preview buffer only.
