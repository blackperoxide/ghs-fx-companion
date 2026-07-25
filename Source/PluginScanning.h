#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Scanning (instantiating each installed plugin briefly to query it) is the
 * one operation in this whole project that must NEVER run inside Logic's own
 * process - if any one of dozens of installed plugins misbehaves during that
 * probe, it takes the whole DAW down with it (confirmed: this crashed Logic
 * in testing). Every real competitor in this category solves it the same
 * way: scan out-of-process, cache the results, have the in-DAW plugin only
 * ever read the cache.
 *
 * GHSFXScanner (a separate standalone app, not a plugin) does the actual
 * scanning and writes the cache. GHSFXCompanion (the AU/VST3 plugin loaded
 * inside Logic) only ever calls loadCachedList() - it never scans.
 */
namespace GHSPluginScanning
{
    /** Where the scan results live - shared between the standalone scanner and the plugin. */
    juce::File getCacheFile();

    /** Only call this from the standalone GHSFXScanner app, never from inside a loaded plugin. */
    void performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager);

    /** Safe to call from inside the plugin - just reads a file, no plugin instantiation. */
    bool loadCachedList(juce::KnownPluginList& listToPopulate);
}
