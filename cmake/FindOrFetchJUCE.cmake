# Locate JUCE. Prefer an in-tree submodule; allow an external checkout via -DJUCE_DIR=...
# We deliberately do NOT FetchContent JUCE by default: a pinned submodule keeps the ARA /
# VST3 binding reproducible for a commercial build.

# Run once per configure: guards against a second include() re-running
# add_subdirectory(JUCE), which CMake rejects ("binary directory already used").
include_guard(GLOBAL)

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using JUCE from JUCE_DIR=${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" juce_build)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE submodule at external/JUCE")
    add_subdirectory(external/JUCE)
else()
    message(FATAL_ERROR
        "JUCE not found.\n"
        "  Run: git submodule update --init --recursive\n"
        "  Or pass: -DJUCE_DIR=/absolute/path/to/JUCE")
endif()
