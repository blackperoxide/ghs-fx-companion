# GHS Drum Gate - a standalone AU/VST3 drum gate (a separate plugin from GHS
# FX Companion, not something hosted inside it).
#
# Builds two ways:
#   - as part of the top-level project (root CMakeLists.txt does
#     add_subdirectory(DrumGate)), sharing that project's JUCE checkout, or
#   - on its own: `cmake -S DrumGate -B build-gate` (fetches JUCE itself).
#     Handy on machines without the webview dependencies the Companion needs.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(GHSDrumGate VERSION 0.1.0)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(FetchContent)
    FetchContent_Declare(
      JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.4
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Optional extra formats for local testing (e.g. -DGHS_GATE_EXTRA_FORMATS=Standalone).
set(GHS_GATE_EXTRA_FORMATS "" CACHE STRING "Extra JUCE plugin formats to build for GHS Drum Gate")

juce_add_plugin(GHSDrumGate
    VERSION 0.1.0
    COMPANY_NAME "Grand Healing Studio"
    BUNDLE_ID "com.grandhealingstudio.ghsdrumgate"
    PLUGIN_MANUFACTURER_CODE Ghst
    PLUGIN_CODE Ghdg
    FORMATS VST3 AU ${GHS_GATE_EXTRA_FORMATS}
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME "GHS Drum Gate"
    IS_SYNTH FALSE
    # MIDI out = the drum-replacement trigger (a note per gate opening, with
    # velocity from hit strength). Like GHS FX Companion, a plugin with MIDI
    # I/O is declared as an AU MusicEffect (aumf) - that's what auval expects.
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
)

target_sources(GHSDrumGate PRIVATE
    Source/GateProcessor.cpp
    Source/GateEditor.cpp
)

target_compile_definitions(GHSDrumGate PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(GHSDrumGate PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# DSP unit test (no JUCE needed): cmake --build <dir> --target GHSDrumGateDSPTest && ctest
add_executable(GHSDrumGateDSPTest Tests/GateDSPTest.cpp)
target_include_directories(GHSDrumGateDSPTest PRIVATE Source)
enable_testing()
add_test(NAME GHSDrumGateDSP COMMAND GHSDrumGateDSPTest)
