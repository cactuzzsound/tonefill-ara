set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TONEFILL_BUILD_TESTS "Build unit/regression tests" ON)
option(TONEFILL_BUILD_TOOLS "Build the offline harness (tools/offline)" ON)
option(TONEFILL_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# ARA is opt-in: enabling it requires an ARA SDK whose version matches your JUCE build.
# Left OFF so a present-but-mismatched ARA SDK never breaks the default (plain-insert) build.
# Turn ON together with TF-005 once the JUCE-matched ARA SDK commit is pinned.
option(TONEFILL_ENABLE_ARA "Build the ARA effect path (needs a JUCE-matched ARA SDK)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Default build type" FORCE)
endif()

# Generate compile_commands.json for tooling / clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
