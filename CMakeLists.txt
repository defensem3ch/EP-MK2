cmake_minimum_required(VERSION 3.22)
project(EPMK2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_plugin(EPMK2
    PRODUCT_NAME            "EP-MK2"
    COMPANY_NAME            "defensem3ch"
    BUNDLE_ID               "audio.defensem3ch.epmk2"
    PLUGIN_MANUFACTURER_CODE Dfm3
    PLUGIN_CODE             Epm2
    FORMATS                 VST3 LV2 Standalone
    LV2URI                  "urn:defensem3ch:epmk2"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(EPMK2 PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/Parameters.cpp)

target_compile_definitions(EPMK2 PUBLIC
    JUCE_WEB_BROWSER=0          # webkit2gtk is not required for this plugin
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(EPMK2
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless test that drives the AudioProcessor the way a host would.
juce_add_console_app(EPMK2_tests PRODUCT_NAME "epmk2-tests")

target_sources(EPMK2_tests PRIVATE
    tests/test_processor.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/Parameters.cpp)

target_compile_definitions(EPMK2_tests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(EPMK2_tests
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Offline MIDI player: renders a performance and reports cost and glitches.
juce_add_console_app(EPMK2_playmidi PRODUCT_NAME "epmk2-playmidi")

target_sources(EPMK2_playmidi PRIVATE
    tests/play_midi.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/Parameters.cpp)

target_compile_definitions(EPMK2_playmidi PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(EPMK2_playmidi
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC  juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Renders the model on the reference library's own grid of pitches and
# velocities, so both can go through the same analysis.
juce_add_console_app(EPMK2_bank PRODUCT_NAME "epmk2-bank")

target_sources(EPMK2_bank PRIVATE
    tests/render_bank.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/Parameters.cpp)

target_compile_definitions(EPMK2_bank PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(EPMK2_bank
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC  juce::juce_recommended_config_flags)
