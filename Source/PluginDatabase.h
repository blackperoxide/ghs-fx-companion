#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * The same categorized-plugin database ghs-fx-chain-builder's web app uses
 * (lib/data/plugin-database.json there - name/manufacturer/category/
 * subcategory/tags for ~1,700 real scanned plugins), compiled into this
 * binary via juce_add_binary_data so it works without any external file on
 * disk. Lookup mirrors the web app's lib/plugin-database.ts: exact name
 * match first, then length-guarded substring match in either direction.
 *
 * Pure lookup over a parsed-once static table - no plugin instantiation, so
 * safe to call from anywhere in the plugin, same as PluginScanning's
 * loadCachedList.
 */
namespace GHSPluginDatabase
{
    struct Entry
    {
        juce::String name;
        juce::String manufacturer;
        juce::String category;
        juce::String subcategory;
        juce::String tags;
    };

    /** Returns nullptr if pluginName doesn't match anything in the database. */
    const Entry* lookupPlugin(const juce::String& pluginName);
}
