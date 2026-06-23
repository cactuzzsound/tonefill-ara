# ToneFill ARA — Dependency Setup & Version Pinning

Mandatory submodules: **JUCE** and **ARA_SDK**. Nothing else.
- **VST3 SDK** is *not* a submodule — JUCE bundles VST3 support in its own tree.
- **Catch2** is fetched by `tests/CMakeLists.txt` via `FetchContent` (pinned to a tag there).

Pinning strategy: each submodule's exact revision is recorded as a gitlink in *this* repo's
tree (not in `.gitmodules`). `.gitmodules` only records URL + path. So "pinning" = committing
the gitlink after checking out the chosen tag.

---

## ARA SDK ↔ JUCE version matching (important)

JUCE expects a specific ARA SDK snapshot; recent ARA SDK `main` renamed APIs that older JUCE
calls. For **JUCE 8.0.12** (incl. the `_headless` variant), pin the ARA_Library submodule to a
commit *before* the 2026 renames (`isTimestretch…`→`isTimeStretch…` and
`ARAChannelArrangement`→`ARAChannelFormat`):

```sh
cd external/ARA_SDK/ARA_Library && git checkout 063a8f6   # "Improve channel arrangement validation"
```

Then enable ARA and build:
```sh
cmake -B build -DJUCE_DIR=/path/to/JUCE -DTONEFILL_ENABLE_ARA=ON
cmake --build build
```
Symptoms of a wrong ARA pin: `no member named 'isTimeStretchReflectingTempo'` or
`'ARA_Library/Utilities/ARAChannelArrangement.cpp' file not found`. ARA is opt-in
(`TONEFILL_ENABLE_ARA`, default OFF) so a mismatched SDK never breaks the default build.

## Recommended pins

| Dependency | Repo | Pin | Why |
|---|---|---|---|
| JUCE | `juce-framework/JUCE` | **tag `7.0.12`** | last JUCE 7.0.x; has `IS_ARA_EFFECT` + `juce_set_ara_sdk_path()` |
| ARA SDK | `Celemony/ARA_SDK` | **an ARA 2.x release tag** | JUCE 7.0.x targets ARA 2.x; pick the tag your JUCE expects (verify, see below) |

> The ARA SDK tag must match what your JUCE version was built/tested against. Don't assume —
> verify with the steps in "Choosing the ARA SDK tag" before committing the pin.

---

## Scenario A — first-time setup (you, authoring the repo)

Run from the repo root. These commands create the `.gitmodules` entries and clone.

```sh
# 1. JUCE, pinned to 7.0.12
git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
git -C external/JUCE fetch --tags
git -C external/JUCE checkout 7.0.12

# 2. ARA SDK
git submodule add https://github.com/Celemony/ARA_SDK.git external/ARA_SDK
git -C external/ARA_SDK fetch --tags
# pick the tag (see "Choosing the ARA SDK tag" below), e.g.:
git -C external/ARA_SDK checkout 2.2.0
# ARA_SDK's own nested submodules are only needed to build ARA's bundled examples, NOT for
# JUCE. Skip unless you want them:
# git -C external/ARA_SDK submodule update --init --recursive

# 3. Record the pinned gitlinks in this repo
git add .gitmodules external/JUCE external/ARA_SDK
git commit -m "Pin JUCE 7.0.12 + ARA SDK 2.x"
```

## Scenario B — someone cloning the repo later

```sh
git clone --recurse-submodules <your-repo-url>
# or, if already cloned without submodules:
git submodule update --init           # JUCE + ARA_SDK at the pinned commits
# (add --recursive only if you need ARA_SDK's example submodules)
```

---

## Choosing the ARA SDK tag (verify, don't guess)

```sh
# List available ARA SDK release tags:
git -C external/ARA_SDK tag

# Sanity-check the marker file ConfigureARA.cmake validates against exists:
ls external/ARA_SDK/ARA_API/ARAInterface.h
```

How to confirm compatibility with your JUCE pin:
1. Check JUCE's ARA example (`examples/Plugins/ARAPluginDemo` and its docs) and the JUCE
   release notes for the JUCE version you pinned — they reference the expected ARA SDK.
2. If `cmake` configure later errors inside JUCE's ARA setup, bump the ARA SDK tag to the
   next 2.x release and reconfigure. The error surfaces at configure time, not build time,
   so iteration is cheap.
3. `2.2.0` is a reasonable starting point for JUCE 7.0.x; adjust per (1)/(2) if needed.

---

## Configure & build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

What you should see at configure time:
- `ConfigureARA.cmake` finds `external/ARA_SDK/ARA_API/ARAInterface.h` → sets
  `TONEFILL_ARA_AVAILABLE = TRUE` and calls `juce_set_ara_sdk_path(...)`.
- `src/plugin/CMakeLists.txt` prints: `ToneFillPlugin: ARA effect build (SDK present).`

If the ARA SDK is absent or unpinned:
- You get a `WARNING` (not an error); the engine and a **plain-insert** plugin still build.
- `src/plugin/CMakeLists.txt` prints: `plain insert build (no ARA SDK) — degraded but functional.`

---

## Overriding paths without submodules

Both can point at external checkouts instead of `external/`:

```sh
cmake -B build \
  -DJUCE_DIR=/abs/path/to/JUCE \
  -DTONEFILL_ARA_SDK_DIR=/abs/path/to/ARA_SDK
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `JUCE not found` (FATAL) | submodules not initialized | `git submodule update --init` or `-DJUCE_DIR=` |
| `ARA SDK not found` (WARNING) | ARA_SDK missing/unpinned | checkout an ARA tag, or `-DTONEFILL_ARA_SDK_DIR=` |
| `juce_set_ara_sdk_path() is not available` (FATAL) | JUCE too old, or ConfigureARA ran before FindOrFetchJUCE | use JUCE ≥ 7.0; keep include order in top `CMakeLists.txt` |
| configure error inside JUCE ARA setup | ARA SDK tag incompatible with JUCE pin | bump ARA SDK to next 2.x tag, reconfigure |
| plugin builds but host shows no ARA | built as plain insert (SDK absent) or TF-005 binding not yet written | pin ARA SDK; implement `DocumentController` (TF-005) |

---

## Updating a pin later

```sh
git -C external/JUCE fetch --tags && git -C external/JUCE checkout <new-tag>
git add external/JUCE && git commit -m "Bump JUCE to <new-tag>"
```
Same pattern for `external/ARA_SDK`. Always re-run `ctest` after a JUCE/ARA bump.
