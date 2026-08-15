cmake_minimum_required(VERSION 3.22)
# M_PI is POSIX, not standard C++, and MSVC's <cmath> withholds it without
# this.  The DSP defines its own constant and does not need it; the tests
# still use M_PI, and this is for them.
add_compile_definitions(_USE_MATH_DEFINES)

project(EPMK2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

# CLAP, which JUCE does not build itself: 9.0.1 knows AAX, AU, AUv3, LV2,
# Standalone, Unity, VST and VST3, and nothing else.  clap-juce-extensions
# wraps the existing AudioProcessor, so there is no second implementation to
# keep in step -- the CLAP is the same plugin as the VST3.
add_subdirectory(clap-juce-extensions EXCLUDE_FROM_ALL)

# The panel's typeface, embedded rather than looked up on the host.  JUCE's
# default is whatever the machine calls "sans-serif", which is a different font
# on every OS -- so without this the plugin looks different everywhere and the
# label widths the layout depends on are not knowable at build time.
#
# Liberation Sans is under the SIL Open Font License 1.1, which permits
# embedding and redistribution, including commercially.  It is bundled
# unmodified; see resources/fonts/LICENSE-Liberation.txt, which ships with the
# plugin.  Modifying it would trigger the OFL's Reserved Font Name clause and
# require renaming.
juce_add_binary_data(EPMK2_fonts
    HEADER_NAME EpMk2Fonts.h
    NAMESPACE   EpMk2Fonts
    SOURCES
        resources/fonts/LiberationSans-Regular.ttf
        resources/fonts/LiberationSans-Bold.ttf)

# The plugin targets are shared libraries, so anything linked into them has to
# be position independent.
set_target_properties(EPMK2_fonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

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

# CLAP_ID is the plugin's identity to a host and must never change once
# anything has saved a project referring to it.
clap_juce_extensions_plugin(TARGET EPMK2
    CLAP_ID "audio.defensem3ch.epmk2"
    CLAP_FEATURES instrument "electric piano" keyboard synthesizer stereo)

target_link_libraries(EPMK2
    PRIVATE
        EPMK2_fonts
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
    PRIVATE EPMK2_fonts juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Loads the built .clap the way a host would.  Separate from the main test
# binary because it has to open the *artefact*, not link the code.
add_executable(EPMK2_claptest tests/clap_load.cpp)
set_target_properties(EPMK2_claptest PROPERTIES OUTPUT_NAME "epmk2-claptest")
target_include_directories(EPMK2_claptest
    PRIVATE clap-juce-extensions/clap-libs/clap/include)
target_compile_definitions(EPMK2_claptest PUBLIC
    JUCE_WEB_BROWSER=0          # or juce_gui_extra wants gtk and webkit
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(EPMK2_claptest PRIVATE juce::juce_audio_processors ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------
# The .scl parser, tested on its own.  No JUCE and no plugin: it is a pure
# function over a string, and keeping it that way is what makes the malformed
# cases cheap to cover.
add_executable(EPMK2_scaletest tests/test_scale.cpp)
set_target_properties(EPMK2_scaletest PROPERTIES OUTPUT_NAME "epmk2-scaletest")

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
    PRIVATE EPMK2_fonts juce::juce_audio_utils juce::juce_audio_formats
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
    PRIVATE EPMK2_fonts juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC  juce::juce_recommended_config_flags)
