# Catch2 v3 via FetchContent (pin in production). Kept self-contained so `ctest` works
# without a vendored submodule. Swap to add_subdirectory(external/catch2) if you prefer
# a pinned submodule for offline builds.
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(tonefill_tests
    test_main.cpp
    dsp/test_seeded_rng.cpp
    dsp/test_render_settings_hash.cpp
    dsp/test_windows.cpp
    dsp/test_fft_roundtrip.cpp
    dsp/test_features.cpp
    dsp/test_noise_shaper.cpp
    dsp/test_tonal_detect.cpp
    dsp/test_crossfade.cpp
    dsp/test_granular.cpp
    dsp/test_ambience_concat.cpp
    engine/test_render_determinism.cpp
    engine/test_edge_conditioning.cpp
    engine/test_macro_envelope.cpp
    engine/test_clean_ambience.cpp
    golden/test_golden_regression.cpp)

target_link_libraries(tonefill_tests
    PRIVATE
        tonefill_engine
        Catch2::Catch2WithMain)

# src/ for engine headers; CMAKE_CURRENT_SOURCE_DIR so golden/fixtures.h resolves as
# "golden/fixtures.h". TONEFILL_GOLDEN_DIR points the regression test at its goldens.txt.
target_include_directories(tonefill_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(tonefill_tests PRIVATE
    TONEFILL_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# Register the whole Catch2 binary as ONE CTest test. We deliberately avoid
# catch_discover_tests: it injects each TEST_CASE name as a CTest filter, which breaks on
# Catch2-special characters in names ([ ] , ; ~ * etc.). One binary == bulletproof, and Catch2
# still prints per-case results and returns non-zero if any case fails.
add_test(NAME tonefill_tests COMMAND tonefill_tests)
