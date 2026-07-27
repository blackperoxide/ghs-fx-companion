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
 * One process boundary isn't quite enough on its own, though (also confirmed
 * in testing: a single misbehaving plugin crashed the whole standalone
 * scanner mid-run, losing everything scanned before it). So scanning itself
 * is a second layer: the scanner re-invokes itself once per plugin file via
 * --scan-one, so a crash there only loses that one plugin, not the whole
 * scan, and progress is saved to the cache after every file.
 *
 * GHSFXScanner (a separate standalone app, not a plugin) does the actual
 * scanning and writes the cache. GHSFXCompanion (the AU/VST3 plugin loaded
 * inside Logic) only ever calls loadCachedList() - it never scans.
 */
namespace GHSPluginScanning
{
    /** Where the scan results live - shared between the standalone scanner and the plugin. */
    juce::File getCacheFile();

    /**
     * Only call this from the standalone GHSFXScanner app, never from inside a loaded
     * plugin. Re-invokes scannerExecutablePath once per plugin file with --scan-one,
     * so a crash in any one plugin only skips that plugin.
     */
    void performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager,
                                      const juce::String& scannerExecutablePath);

    /**
     * The --scan-one child mode: scans exactly one file/identifier for one format and
     * prints its PluginDescription(s) to stdout. This is the only place that actually
     * instantiates a plugin - if it crashes, only this small child process dies.
     */
    void scanOneAndPrintResult(juce::AudioPluginFormatManager& formatManager,
                                const juce::String& formatName,
                                const juce::String& fileOrIdentifier);

    /** Safe to call from inside the plugin - just reads a file, no plugin instantiation. */
    bool loadCachedList(juce::KnownPluginList& listToPopulate);
}
