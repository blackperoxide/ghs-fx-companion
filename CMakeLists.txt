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
    # The UI is a real HTML/CSS/JS webview (Resources/webui/*), not hand-drawn
    # JUCE components - this links webkit2gtk on Linux (JUCE runs it in a
    # helper subprocess for plugin targets automatically). No effect on macOS/
    # Windows, which use WKWebView/WebView2 without needing this.
    NEEDS_WEB_BROWSER TRUE
)

target_sources(GHSFXCompanion PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginScanning.cpp
    Source/ChainPresets.cpp
    Source/RecipeImport.cpp
    Source/PluginDatabase.cpp
    Source/ChainAxes.cpp
    Source/DrumChainVariants.cpp
    Source/ToneAnalyzer.cpp
    Source/ToneRecommendation.cpp
)

# plugin_database.json: the same categorized real-plugin database the
# ghs-fx-chain-builder website uses (lib/data/plugin-database.json there).
# axis_config.json / drum_stage_variants.json: the same 7-axis model and
# per-stage variant content as that web app's lib/chain-axes.ts and
# lib/drum-chain-variants.ts, generated from those files (see the export
# script referenced in Source/ChainAxes.cpp / DrumChainVariants.cpp) so this
# plugin's "record and suggest" feature and the web app's prompt matcher can
# never drift onto different data.
# Resources/webui/*: the actual UI, served to the embedded WebBrowserComponent
# by PluginEditor's ResourceProvider - see PluginEditor.cpp's getResource().
# All compiled into the binary so none of it depends on an external file on
# disk or any network access.
juce_add_binary_data(GHSFXCompanionData
    SOURCES
        Resources/plugin_database.json
        Resources/axis_config.json
        Resources/drum_stage_variants.json
        Resources/webui/index.html
        Resources/webui/style.css
        Resources/webui/app.js
)

target_compile_definitions(GHSFXCompanion PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Lets our own plugin host other plugins inside itself (VST3 always available;
    # AU hosting only compiles on macOS, guarded in code with JUCE_MAC).
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

target_link_libraries(GHSFXCompanion PRIVATE
    GHSFXCompanionData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
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
