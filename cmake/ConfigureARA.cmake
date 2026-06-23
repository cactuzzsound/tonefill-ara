# Central ARA configuration. Include AFTER FindOrFetchJUCE (JUCE must be added first, since
# this module calls JUCE-provided CMake commands). Run-once: it registers the ARA SDK with
# JUCE and defines a single per-target hook.
include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Paths — the only things a user normally needs to set.
# Override on the command line (-DTONEFILL_ARA_SDK_DIR=/abs/path) or via the cache.
# ---------------------------------------------------------------------------
set(TONEFILL_ARA_SDK_DIR "${CMAKE_SOURCE_DIR}/external/ARA_SDK"
    CACHE PATH "Path to the ARA SDK checkout (Celemony/ARA_SDK).")

# JUCE bundles its own VST3 SDK and uses it automatically for VST3 targets, so this is
# normally NOT required. Exposed only for the rare case of an external/pinned VST3 SDK.
set(TONEFILL_VST3_SDK_DIR "" CACHE PATH
    "Optional: external VST3 SDK path. Leave empty to use the copy JUCE bundles.")

# ---------------------------------------------------------------------------
# Validate the ARA SDK checkout up front with an actionable message.
# Marker file is stable across ARA_SDK revisions.
# ---------------------------------------------------------------------------
set(_tonefill_ara_marker "${TONEFILL_ARA_SDK_DIR}/ARA_API/ARAInterface.h")
set(_tonefill_ara_sdk_present FALSE)
if(EXISTS "${_tonefill_ara_marker}")
    set(_tonefill_ara_sdk_present TRUE)
endif()

# ARA is enabled only when explicitly opted-in AND the SDK is present. A present-but-mismatched
# SDK must not silently flip the build into the ARA path (JUCE<->ARA-SDK version skew breaks it).
if(TONEFILL_ENABLE_ARA AND _tonefill_ara_sdk_present)
    set(TONEFILL_ARA_AVAILABLE TRUE CACHE INTERNAL "ARA enabled")
elseif(TONEFILL_ENABLE_ARA AND NOT _tonefill_ara_sdk_present)
    set(TONEFILL_ARA_AVAILABLE FALSE CACHE INTERNAL "ARA enabled")
    message(WARNING
        "TONEFILL_ENABLE_ARA=ON but ARA SDK not found at ${_tonefill_ara_marker}.\n"
        "  Clone it (see docs/SETUP.md) or pass -DTONEFILL_ARA_SDK_DIR=/abs/path.")
else()
    set(TONEFILL_ARA_AVAILABLE FALSE CACHE INTERNAL "ARA enabled")
    if(_tonefill_ara_sdk_present)
        message(STATUS "ARA SDK present but TONEFILL_ENABLE_ARA=OFF -> plain-insert build. "
                       "Pass -DTONEFILL_ENABLE_ARA=ON (with a JUCE-matched ARA SDK) for TF-005.")
    else()
        message(STATUS "ARA disabled (no SDK, TONEFILL_ENABLE_ARA=OFF) -> plain-insert build.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Register the ARA SDK with JUCE, once, if available.
# JUCE consumes the path via juce_set_ara_sdk_path() (JUCE 7+ CMake API). We DETECT the
# command instead of assuming a fixed JUCE version, so a version mismatch fails loudly
# here rather than miswiring silently.
# ---------------------------------------------------------------------------
if(TONEFILL_ARA_AVAILABLE)
    if(COMMAND juce_set_ara_sdk_path)
        juce_set_ara_sdk_path("${TONEFILL_ARA_SDK_DIR}")
        message(STATUS "ARA SDK registered with JUCE: ${TONEFILL_ARA_SDK_DIR}")
    else()
        message(FATAL_ERROR
            "juce_set_ara_sdk_path() is not available.\n"
            "  Causes: (1) ConfigureARA was included before FindOrFetchJUCE, or\n"
            "          (2) this JUCE version exposes ARA setup under a different name.\n"
            "  Wire the correct call here (cmake/ConfigureARA.cmake) per your JUCE\n"
            "  version's CMake API documentation.")
    endif()
endif()

if(TONEFILL_VST3_SDK_DIR)
    message(STATUS
        "External VST3 SDK requested: ${TONEFILL_VST3_SDK_DIR}\n"
        "  (JUCE uses its bundled VST3 SDK by default; only override if you must.)")
endif()

# ---------------------------------------------------------------------------
# Central per-target hook. Call once per ARA plugin target. Keeps the plugin's
# CMakeLists free of any ARA detail.
#
# The actual ARA binding is emitted by juce_add_plugin(... IS_ARA_EFFECT TRUE) combined
# with the global juce_set_ara_sdk_path() above. This hook layers on:
#   - a TONEFILL_ARA_AVAILABLE compile definition the C++ can branch on, and
#   - a single place for any future per-target ARA link/define tweaks.
# ---------------------------------------------------------------------------
function(tonefill_configure_ara target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "tonefill_configure_ara: '${target}' is not a target.")
    endif()

    if(NOT TONEFILL_ARA_AVAILABLE)
        message(STATUS
            "tonefill_configure_ara('${target}'): ARA SDK absent -> builds without ARA.")
        target_compile_definitions(${target} PRIVATE TONEFILL_ARA_AVAILABLE=0)
        return()
    endif()

    target_compile_definitions(${target} PRIVATE TONEFILL_ARA_AVAILABLE=1)
    message(STATUS "tonefill_configure_ara('${target}'): ARA enabled.")
    # TODO(ARA): add any per-target ARA link/define tweaks here if a host needs them.
endfunction()
