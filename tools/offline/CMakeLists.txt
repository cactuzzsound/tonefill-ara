# Offline harness: drives the engine on WAV files with no host and no ARA.
# Links only tonefill_engine (+ JUCE audio formats, already PUBLIC on the engine).

add_executable(tonefill_offline main.cpp)

target_link_libraries(tonefill_offline
    PRIVATE
        tonefill_engine
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Keep the binary easy to find next to the build tree.
set_target_properties(tonefill_offline PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
