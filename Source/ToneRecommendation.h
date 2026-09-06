#pragma once

#include "DrumChainVariants.h"
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Ties GHSDrumChainVariants::buildDrumChain() to what's actually installed:
 * for every candidate (brand, plugin) pair the axis-based recommendation
 * suggests, checks it against knownPlugins with the same fuzzy matcher the
 * recipe importer uses, so the UI can show which suggestions the user can
 * load right now versus which are "here's what to look for."
 */
namespace GHSToneRecommendation
{
    struct SuggestedOption
    {
        juce::String brand, plugin, tip;
        bool owned = false;
        juce::PluginDescription description; // only valid when owned is true
    };

    struct SuggestedStage
    {
        juce::String id, name, role, note;
        std::vector<SuggestedOption> options; // same preference order as the chain data
    };

    std::vector<SuggestedStage> recommend(const GHSChainAxes::AxisScores& axes,
                                           const juce::Array<juce::PluginDescription>& knownPlugins);
}
