# ToneFill plugin target.
#
# Single rule of this file: REACT to ConfigureARA's result (TONEFILL_ARA_AVAILABLE), never
# re-derive JUCE/ARA version details here. When the ARA SDK is present the target is built as
# an ARA effect; otherwise it degrades to a plain VST3/AU insert that still compiles and loads.

# ---------------------------------------------------------------------------
# Source lists
# ---------------------------------------------------------------------------
# Always compiled. NOTE: ara/ARAIntegrationFacade.cpp lives here on purpose — it is the
# host-agnostic seam (JUCE-only: WAV export + capability detection). Its ARA-SDK-specific
# bodies are guarded with #if TONEFILL_ARA_AVAILABLE, so it compiles with or without the SDK,
# and the processor can hold it unconditionally.
set(PLUGIN_SOURCES
    PluginProcessor.cpp
    PluginEditor.cpp
    ParameterState.cpp
    ui/MainView.cpp
    ui/WaveformWindow.cpp
    ara/ARAIntegrationFacade.cpp)

# Compiled ONLY when the ARA SDK is available. These are the real JUCE<->ARA bindings that
# derive from JUCE ARA base classes and will not compile without the SDK.
set(PLUGIN_ARA_SOURCES
    ara/DocumentController.cpp
    ara/PlaybackRenderer.cpp)
    # TODO(ARA): ara/EditorRenderer.cpp (audition while editing) when added.

# ---------------------------------------------------------------------------
# ARA effect flag — only when the SDK is registered (set by ConfigureARA).
# Expanding an empty/!empty variable into juce_add_plugin keeps a single call site.
# ---------------------------------------------------------------------------
set(_tonefill_ara_kw "")
if(TONEFILL_ARA_AVAILABLE)
    set(_tonefill_ara_kw IS_ARA_EFFECT TRUE)
endif()

# AAX (Pro Tools) - opt-in: needs Avid's AAX SDK (download from your Avid developer account).
# Point at it with -DAAX_SDK_PATH=/abs/path, or drop it at external/aax-sdk. Built unsigned for
# AudioSuite testing in a Developer build of Pro Tools; commercial release needs PACE signing.
set(_tonefill_aax_kw "")
if(NOT AAX_SDK_PATH AND EXISTS "${CMAKE_SOURCE_DIR}/external/aax-sdk/Interfaces")
    set(AAX_SDK_PATH "${CMAKE_SOURCE_DIR}/external/aax-sdk")
endif()
if(AAX_SDK_PATH AND EXISTS "${AAX_SDK_PATH}/Interfaces")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    set(_tonefill_aax_kw AAX)
    message(STATUS "ToneFillPlugin: AAX enabled (SDK at ${AAX_SDK_PATH}).")
else()
    message(STATUS "ToneFillPlugin: AAX disabled (set -DAAX_SDK_PATH=/path/to/aax-sdk to enable).")
endif()

juce_add_plugin(ToneFillPlugin
    PRODUCT_NAME             "ToneFill"
    COMPANY_NAME             "Cactuzz Sound"
    BUNDLE_ID                com.cactuzzsound.tonefill
    PLUGIN_MANUFACTURER_CODE Catz
    PLUGIN_CODE              Tfil
    FORMATS                  VST3 AU   # AAX is the native HostProcessor build under aax/ (not JUCE)
    ${_tonefill_ara_kw}                       # IS_ARA_EFFECT TRUE, or nothing
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_CATEGORIES          Fx Restoration
    AU_MAIN_TYPE             kAudioUnitType_Effect)

# ---------------------------------------------------------------------------
# Sources: base always; ARA bindings only when available.
# ---------------------------------------------------------------------------
target_sources(ToneFillPlugin PRIVATE ${PLUGIN_SOURCES})
if(TONEFILL_ARA_AVAILABLE)
    target_sources(ToneFillPlugin PRIVATE ${PLUGIN_ARA_SOURCES})
endif()

# Knob film-strip skin (Modern_4): embedded so the build needs no runtime asset path.
juce_add_binary_data(ToneFillAssets SOURCES ${CMAKE_SOURCE_DIR}/assets/knob_modern.png)
set_target_properties(ToneFillAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(ToneFillPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Central ARA hook: applies the TONEFILL_ARA_AVAILABLE=0/1 compile definition (the CMake->C++
# bridge) and is the single place for any future per-target ARA tweaks. Must come after the
# target exists.
tonefill_configure_ara(ToneFillPlugin)

target_link_libraries(ToneFillPlugin
    PRIVATE
        ToneFillAssets
        tonefill_engine
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(ToneFillPlugin)

if(TONEFILL_ARA_AVAILABLE)
    message(STATUS "ToneFillPlugin: ARA effect build (SDK present).")
else()
    message(STATUS "ToneFillPlugin: plain insert build (no ARA SDK) — degraded but functional.")
endif()
