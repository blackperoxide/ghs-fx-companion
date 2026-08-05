cmake_minimum_required(VERSION 3.22)

project(GHSFXCompanion VERSION 0.1.0)

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

# Milestone 1: host a single third-party VST3/AU plugin and pass real audio
# through it. No chain, no AI matching yet - just prove the hosting mechanism
# works before building anything on top of it.
juce_add_plugin(GHSFXCompanion
    COMPANY_NAME "Grand Healing Studio"
    BUNDLE_ID "com.grandhealingstudio.ghsfxcompanion"
    PLUGIN_MANUFACTURER_CODE Ghst
    PLUGIN_CODE Ghs1
    FORMATS VST3 AU
    # Without this, every rebuild only updates the copy inside build/ - Logic
    # never sees it and keeps running whatever was last copied to the system
    # plugin folders by hand.
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME "GHS FX Companion"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx
    # MusicEffect (not plain Effect) - this AU wants MIDI in/out, and aumf is
    # the AU type for that; declaring MIDI I/O under aufx is what auval warned
    # about (MusicDeviceMIDIEvent implemented on a type that shouldn't have it).
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
)

target_sources(GHSFXCompanion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginScanning.cpp
)

target_compile_definitions(GHSFXCompanion PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Lets our own plugin host other plugins inside itself (VST3 always available;
    # AU hosting only compiles on macOS, guarded in code with JUCE_MAC).
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

target_link_libraries(GHSFXCompanion PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Standalone scanner - NOT a plugin, run manually outside Logic. This is the
# isolation boundary: it's the only thing allowed to actually instantiate
# installed plugins to query them, so a crash there never touches Logic.
# See Source/PluginScanning.h for why this split exists.
juce_add_console_app(GHSFXScanner
    PRODUCT_NAME "GHS FX Companion Scanner"
)

target_sources(GHSFXScanner PRIVATE
    Source/ScannerMain.cpp
    Source/PluginScanning.cpp
)

target_compile_definitions(GHSFXScanner PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

target_link_libraries(GHSFXScanner PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(APPLE)
    # AU hosting (JUCE_PLUGINHOST_AU) needs these - juce_add_plugin() links
    # them automatically for plugin targets, but a plain console app doesn't
    # get them for free.
    target_link_libraries(GHSFXScanner PRIVATE
        "-framework CoreAudioKit"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreMIDI"
    )
endif()
