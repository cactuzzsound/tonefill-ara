# Native AAX AudioSuite (HostProcessor) build of ToneFill. Separate plug-in client from the JUCE
# VST3/AU/ARA build; shares tonefill_engine. Built only when -DAAX_SDK_PATH points at the AAX SDK.

if(NOT AAX_SDK_PATH OR NOT EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
    message(STATUS "ToneFill AAX (native): disabled (no AAX_SDK_PATH).")
    return()
endif()

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
    add_library(AAXLibrary STATIC IMPORTED GLOBAL)
    set_target_properties(AAXLibrary PROPERTIES
        IMPORTED_LOCATION "${AAX_SDK_PATH}/Libs/Release/libAAXLibrary_libcpp.a"
        INTERFACE_INCLUDE_DIRECTORIES "${AAX_SDK_PATH}/Interfaces;${AAX_SDK_PATH}/Interfaces/ACF")
    add_library(AAX_SDK::AAXLibrary ALIAS AAXLibrary)
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
    ToneFillAS_HostProcessor.cpp)

target_include_directories(ToneFillAS PRIVATE
    "${AAX_SDK_PATH}/Interfaces" "${AAX_SDK_PATH}/Interfaces/ACF"
    "${CMAKE_SOURCE_DIR}/src")

target_compile_options(ToneFillAS PRIVATE -Wno-multichar)
target_link_libraries(ToneFillAS PRIVATE tonefill_engine)
