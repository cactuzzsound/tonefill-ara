# Native AAX AudioSuite (HostProcessor) build of ToneFill. Separate plug-in client from the JUCE
# VST3/AU/ARA build; shares tonefill_engine. Built only when -DAAX_SDK_PATH points at the AAX SDK.

if(NOT AAX_SDK_PATH OR NOT EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
    message(STATUS "ToneFill AAX (native): disabled (no AAX_SDK_PATH).")
    return()
endif()

# Normalise to forward slashes: a Windows backslash path (D:\a\...) embedded in the add_library()
# source strings below is otherwise parsed as an invalid escape ('\a', '\t', ...).
file(TO_CMAKE_PATH "${AAX_SDK_PATH}" AAX_SDK_PATH)

message(STATUS "ToneFill AAX (native): enabled (SDK ${AAX_SDK_PATH}).")

include("${AAX_SDK_PATH}/cmake/AAX_SDKFunctions.cmake")

# --- AAX SDK targets the aax_plugin() helper expects (we use the prebuilt runtime library) ------
if(NOT TARGET AAX_SDK::AAX_SDK_Interface)
    add_library(AAX_SDK_Interface INTERFACE)
    target_include_directories(AAX_SDK_Interface INTERFACE
        "${AAX_SDK_PATH}/Interfaces" "${AAX_SDK_PATH}/Interfaces/ACF")
    add_library(AAX_SDK::AAX_SDK_Interface ALIAS AAX_SDK_Interface)
endif()

if(NOT TARGET AAX_SDK::AAX_Export)
    add_library(AAX_Export OBJECT "${AAX_SDK_PATH}/Interfaces/AAX_Exports.cpp")
    set_target_properties(AAX_Export PROPERTIES CXX_STANDARD 14)
    target_link_libraries(AAX_Export PUBLIC AAX_SDK_Interface)
    add_library(AAX_SDK::AAX_Export ALIAS AAX_Export)
endif()

if(NOT TARGET AAX_SDK::AAXLibrary)
    if(WIN32)
        # The SDK ships only a macOS prebuilt AAXLibrary (libcpp .a); on Windows build it from the
        # SDK's own CMake sources, which define the AAX_SDK::AAXLibrary target we link below.
        add_subdirectory("${AAX_SDK_PATH}/Libs/AAXLibrary" "${CMAKE_BINARY_DIR}/AAXLibrary")
    else()
        add_library(AAXLibrary STATIC IMPORTED GLOBAL)
        set_target_properties(AAXLibrary PROPERTIES
            IMPORTED_LOCATION "${AAX_SDK_PATH}/Libs/Release/libAAXLibrary_libcpp.a"
            INTERFACE_INCLUDE_DIRECTORIES "${AAX_SDK_PATH}/Interfaces;${AAX_SDK_PATH}/Interfaces/ACF")
        add_library(AAX_SDK::AAXLibrary ALIAS AAXLibrary)
    endif()
endif()

# --- The plug-in bundle ------------------------------------------------------------------------
aax_plugin(TARGET_NAME ToneFillAS
    OUTPUT_NAME "ToneFill"
    VERSION 1.0.0
    COPYRIGHT_STRING "Copyright 2026 Cactuzz Sound"
    BUNDLE_IDENTIFIER "com.cactuzzsound.tonefill.audiosuite")

target_sources(ToneFillAS PRIVATE
    ToneFillAS_Describe.cpp
    ToneFillAS_Parameters.cpp
    ToneFillAS_HostProcessor.cpp
    ToneFillAS_GUI.cpp
    ASView.cpp
    "${CMAKE_SOURCE_DIR}/src/plugin/ui/SpectralEditor.cpp"
    "${CMAKE_SOURCE_DIR}/src/plugin/ui/EqView.cpp"
    "${CMAKE_SOURCE_DIR}/src/licensing/LicenseManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/licensing/ActivationComponent.cpp")

target_include_directories(ToneFillAS PRIVATE
    "${AAX_SDK_PATH}/Interfaces" "${AAX_SDK_PATH}/Interfaces/ACF"
    "${CMAKE_SOURCE_DIR}/src")

# -Wno-multichar is a GCC/Clang flag (the AAX SDK uses multichar type-ID literals); MSVC rejects it
# as an invalid cl argument and accepts multichar literals anyway.
if(NOT MSVC)
    target_compile_options(ToneFillAS PRIVATE -Wno-multichar)
endif()

# GUI: host a JUCE component in PT's native view. Needs juce_gui_basics (pulls graphics/events) and
# the embedded logo (ToneFillAssets -> BinaryData.h). JUCE definitions match the plugin build so the
# ScopedJuceInitialiser_GUI / desktop hosting behave the same.
target_link_libraries(ToneFillAS PRIVATE
    tonefill_engine
    ToneFillAssets
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(ToneFillAS PRIVATE
    JUCE_STANDALONE_APPLICATION=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
