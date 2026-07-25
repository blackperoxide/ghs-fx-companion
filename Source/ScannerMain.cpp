#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginScanning.h"

/**
 * GHS FX Companion Scanner - a standalone app, NOT a plugin. Run this on its
 * own, outside Logic, whenever you want to (re)build the plugin list. It's
 * the isolation boundary: if scanning one of your installed plugins crashes
 * this process, Logic never even knows it happened. The GHS FX Companion
 * plugin (loaded inside Logic) only ever reads the cache file this writes -
 * it never scans anything itself.
 */
int main(int, char**)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "GHS FX Companion Scanner - scanning installed VST3/AU plugins..." << std::endl;

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    GHSPluginScanning::performFullScanAndSaveCache(formatManager);

    std::cout << "Done. Results saved to: "
              << GHSPluginScanning::getCacheFile().getFullPathName() << std::endl;
    std::cout << "You can close this and reopen Logic - the plugin will pick up the new list." << std::endl;

    return 0;
}
