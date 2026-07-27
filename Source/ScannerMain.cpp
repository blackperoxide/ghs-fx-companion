#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginScanning.h"

/**
 * GHS FX Companion Scanner - a standalone app, NOT a plugin. Run this on its
 * own, outside Logic, whenever you want to (re)build the plugin list.
 *
 * Two modes:
 *  - No arguments: does the full scan, re-invoking itself once per plugin
 *    file (see --scan-one below) so a crash in any one plugin only skips
 *    that plugin instead of losing the whole scan.
 *  - --scan-one <formatName> <fileOrIdentifier>: scans exactly one plugin
 *    file and prints its description(s) to stdout. This is the only mode
 *    that actually instantiates a third-party plugin - if it crashes here,
 *    only this short-lived child process dies.
 *
 * Either way, the plugin loaded inside Logic never scans anything itself -
 * it only reads the cache file this writes.
 */
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    if (argc >= 4 && juce::String(argv[1]) == "--scan-one")
    {
        GHSPluginScanning::scanOneAndPrintResult(formatManager, argv[2], argv[3]);
        return 0;
    }

    std::cout << "GHS FX Companion Scanner - scanning installed VST3/AU plugins..." << std::endl;
    std::cout << "(each plugin is probed in its own throwaway process - if one crashes, it's"
                  " skipped and the scan keeps going)" << std::endl;

    auto selfPath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    GHSPluginScanning::performFullScanAndSaveCache(formatManager, selfPath);

    std::cout << "Done. Results saved to: "
              << GHSPluginScanning::getCacheFile().getFullPathName() << std::endl;
    std::cout << "You can close this and reopen Logic - the plugin will pick up the new list." << std::endl;

    return 0;
}
