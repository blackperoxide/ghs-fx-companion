#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Reads a .ghsrecipe.json file exported by the ghs-fx-chain-builder web app
 * (see lib/recipe-export.ts there) and matches each stage's candidate plugin
 * names against the locally scanned plugin list, picking the first candidate
 * that matches something actually installed - the recipe has no idea what's
 * on this machine, so matching is deliberately by name/substring, not exact
 * identifier.
 *
 * Pure parsing/string-matching - no plugin instantiation happens in here, so
 * (like PluginScanning's loadCachedList) it's always safe to call from
 * inside the loaded plugin.
 */
namespace GHSRecipeImport
{
    struct MatchedStage
    {
        juce::String stageName;
        juce::String category;
        bool matched = false;
        juce::PluginDescription description; // only valid when matched is true
    };

    /**
     * Parses the file and matches every stage against knownPlugins, in the recipe's
     * own order, truncated to maxSlots entries (one recipe stage per rack slot).
     * Returns an empty array if the file can't be read/parsed as a recipe.
     */
    juce::Array<MatchedStage> loadAndMatch(const juce::File& recipeFile,
                                            const juce::Array<juce::PluginDescription>& knownPlugins,
                                            int maxSlots);

    /**
     * The same name/manufacturer fuzzy match loadAndMatch() uses per candidate,
     * exposed directly so anything else that produces a list of candidate
     * (brand, plugin name) pairs - e.g. GHSDrumChainVariants' axis-based
     * suggestions - can check them against what's actually installed without
     * going through a recipe file. Returns nullptr if nothing matches well
     * enough (substring match either direction, case-insensitive).
     */
    const juce::PluginDescription* findBestMatch(const juce::String& candidatePlugin,
                                                  const juce::String& candidateBrand,
                                                  const juce::Array<juce::PluginDescription>& knownPlugins);
}
