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
 * On top of that, a manifest (ScanManifest.xml) remembers each plugin file's
 * modification time and whether it scanned successfully last time, so a
 * re-run only rescans files that are new, changed, or previously failed -
 * everything else is carried forward from the cache untouched. And since
 * each scan is its own OS process, several can run at once instead of one
 * at a time.
 *
 * GHSFXScanner (a separate standalone app, not a plugin) does the actual
 * scanning and writes the cache. GHSFXCompanion (the AU/VST3 plugin loaded
 * inside Logic) only ever calls loadCachedList() - it never scans.
 */
namespace GHSPluginScanning
{
    /** Where the scan results live - shared between the standalone scanner and the plugin. */
    juce::File getCacheFile();

    /** Where the per-file scan history (mod times + success) lives, next to the cache. */
    juce::File getManifestFile();

    /** When the plugin list cache was last written, or a default/invalid Time if it doesn't exist yet. */
    juce::Time getLastScanTime();

    /** completed/total plugins scanned so far this run, and the identifier just finished. */
    using ProgressCallback = std::function<void(int completed, int total, const juce::String& currentIdentifier)>;

    /**
     * Only call this from the standalone GHSFXScanner app, never from inside a loaded
     * plugin. Skips any plugin file that hasn't changed since it last scanned
     * successfully; everything else is (re)scanned by re-invoking scannerExecutablePath
     * with --scan-one, running up to a handful at once, so a crash in any one plugin
     * only skips that plugin.
     */
    void performFullScanAndSaveCache(juce::AudioPluginFormatManager& formatManager,
                                      const juce::String& scannerExecutablePath,
                                      const ProgressCallback& onProgress = {});

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
