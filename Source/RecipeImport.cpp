#include "RecipeImport.h"

namespace
{
    // Recipe candidate names sometimes carry a qualifier in parens, e.g.
    // "API 2500 (fast attack)" - strip that before comparing against a scanned
    // plugin's plain name.
    juce::String normalize(const juce::String& raw)
    {
        auto s = raw;
        auto parenIndex = s.indexOfChar('(');
        if (parenIndex > 0)
            s = s.substring(0, parenIndex);
        return s.trim().toLowerCase();
    }

    const juce::PluginDescription* findBestMatch(const juce::String& candidatePlugin,
                                                  const juce::String& candidateBrand,
                                                  const juce::Array<juce::PluginDescription>& knownPlugins)
    {
        auto normCandidate = normalize(candidatePlugin);
        if (normCandidate.isEmpty())
            return nullptr;

        const juce::PluginDescription* best = nullptr;
        int bestScore = 0;

        for (auto& desc : knownPlugins)
        {
            auto normKnown = desc.name.trim().toLowerCase();
            if (normKnown.isEmpty())
                continue;

            if (!normKnown.contains(normCandidate) && !normCandidate.contains(normKnown))
                continue;

            int score = 1;
            if (normKnown == normCandidate)
                score += 2;
            if (desc.manufacturerName.containsIgnoreCase(candidateBrand)
                || candidateBrand.containsIgnoreCase(desc.manufacturerName))
                score += 1;

            if (score > bestScore)
            {
                bestScore = score;
                best = &desc;
            }
        }

        return best;
    }
}

juce::Array<GHSRecipeImport::MatchedStage> GHSRecipeImport::loadAndMatch(const juce::File& recipeFile,
                                                                          const juce::Array<juce::PluginDescription>& knownPlugins,
                                                                          int maxSlots)
{
    juce::Array<MatchedStage> results;

    auto json = juce::JSON::parse(recipeFile);
    auto* root = json.getDynamicObject();
    if (root == nullptr)
        return results;

    auto* stagesArray = root->getProperty("stages").getArray();
    if (stagesArray == nullptr)
        return results;

    for (auto& stageVar : *stagesArray)
    {
        if (results.size() >= maxSlots)
            break;

        auto* stageObj = stageVar.getDynamicObject();
        if (stageObj == nullptr)
            continue;

        MatchedStage stage;
        stage.stageName = stageObj->getProperty("name").toString();
        stage.category = stageObj->getProperty("category").toString();

        if (auto* candidates = stageObj->getProperty("candidates").getArray())
        {
            for (auto& candidateVar : *candidates)
            {
                auto* candidateObj = candidateVar.getDynamicObject();
                if (candidateObj == nullptr)
                    continue;

                auto plugin = candidateObj->getProperty("plugin").toString();
                auto brand = candidateObj->getProperty("brand").toString();

                if (auto* found = findBestMatch(plugin, brand, knownPlugins))
                {
                    stage.matched = true;
                    stage.description = *found;
                    break;
                }
            }
        }

        results.add(stage);
    }

    return results;
}
