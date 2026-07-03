cmake_minimum_required(VERSION 3.22)

project(ToneFillARA
    VERSION 0.1.0
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ToneFillOptions)     # C++ standard, build options, default build type
include(FindOrFetchJUCE)     # add_subdirectory(external/JUCE) or fail with guidance
include(ConfigureARA)        # validate ARA SDK location, define tonefill_configure_ara()

# ---------------------------------------------------------------------------
# tonefill_engine : host-agnostic static library.
# Knows nothing about ARA, the plugin client, or the host. Unit-testable headless.
# Depends only on juce_core / juce_dsp / juce_audio_basics / juce_audio_formats.
# ---------------------------------------------------------------------------
add_library(tonefill_engine STATIC
    src/core/DiagnosticsLogger.cpp

    src/dsp/FFTWrapper.cpp
    src/dsp/Stft.cpp
    src/dsp/Features.cpp
    src/dsp/NoiseShaper.cpp
    src/dsp/TonalDetect.cpp
    src/dsp/GranularSynth.cpp
    src/dsp/MacroEnvelope.cpp
    src/dsp/AmbienceConcat.cpp
    src/dsp/VoiceDetect.cpp
    src/dsp/Loudness.cpp
    src/dsp/NoiseModel.cpp
    src/dsp/SpectralResynth.cpp
    src/dsp/PaulStretch.cpp

    src/engine/analysis/AnalysisSession.cpp
    src/engine/analysis/AmbienceModelBuilder.cpp
    src/engine/analysis/CandidateFrameSelector.cpp
    src/engine/analysis/TonalModelBuilder.cpp
    src/engine/analysis/NoiseProfileBuilder.cpp
    src/engine/analysis/GrainCorpusBuilder.cpp

    src/engine/synthesis/AmbienceRenderer.cpp

    src/engine/render/RenderJob.cpp
    src/engine/render/RenderManager.cpp)

target_include_directories(tonefill_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(tonefill_engine PUBLIC cxx_std_17)

target_link_libraries(tonefill_engine
    PUBLIC
        juce::juce_core
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_audio_formats
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# libfvad (WebRTC VAD, BSD-3) — speech-aware clean-ambience selection. Auto-detected:
# if external/libfvad is present it's compiled in; otherwise the engine falls back to the
# level-only selection. Clone: git clone https://github.com/dpirch/libfvad external/libfvad
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libfvad/include/fvad.h")
    add_subdirectory(external/libfvad EXCLUDE_FROM_ALL)
    target_link_libraries(tonefill_engine PRIVATE fvad)
    target_include_directories(tonefill_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/libfvad/include")
    target_compile_definitions(tonefill_engine PRIVATE TONEFILL_USE_FVAD=1)
    message(STATUS "libfvad found -> speech-aware clean selection enabled")
else()
    message(STATUS "libfvad not found -> level-only clean selection")
endif()

# ---------------------------------------------------------------------------
# Plugin target (ARA-enabled VST3 + AU). Defined in its own list file.
# ---------------------------------------------------------------------------
add_subdirectory(src/plugin)

# ---------------------------------------------------------------------------
# Native AAX AudioSuite (HostProcessor) client. Self-guarded: builds only when
# -DAAX_SDK_PATH is set. Shares tonefill_engine with the JUCE client above.
# ---------------------------------------------------------------------------
add_subdirectory(aax)

# ---------------------------------------------------------------------------
# Offline harness: runs the engine on WAV files with no host / no ARA.
# Decouples DSP development (Sprints 2-7) from ARA integration.
# ---------------------------------------------------------------------------
if(TONEFILL_BUILD_TOOLS)
    add_subdirectory(tools/offline)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(TONEFILL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
