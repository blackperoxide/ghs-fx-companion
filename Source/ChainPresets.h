#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Named, user-managed chain presets - separate from the host's own
 * getStateInformation/setStateInformation (which is per-DAW-session, not
 * something you browse, rename, or reuse across tracks). A preset here is
 * just the same chain ValueTree shape GHSFXCompanionProcessor already builds
 * for host state, written to a named file instead of the session - so "save
 * this chain" and "what the DAW remembers" never drift into two formats.
 *
 * Pure file I/O - no plugin instantiation happens in here, so (like
 * PluginScanning's loadCachedList) it's always safe to call from inside the
 * loaded plugin.
 */
namespace GHSChainPresets
{
    /** Where named presets are stored - one XML file per preset. */
    juce::File getPresetsDirectory();

    /** Preset names (file names without the .xml extension), alphabetically sorted. */
    juce::StringArray listPresetNames();

    /** Overwrites any existing preset with the same name. Returns false on I/O failure. */
    bool savePreset(const juce::String& name, const juce::ValueTree& chainState);

    /** Returns an invalid ValueTree if the preset doesn't exist or fails to parse. */
    juce::ValueTree loadPreset(const juce::String& name);

    bool deletePreset(const juce::String& name);
}
