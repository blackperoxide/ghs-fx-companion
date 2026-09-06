#include "ToneRecommendation.h"
#include "RecipeImport.h"

std::vector<GHSToneRecommendation::SuggestedStage> GHSToneRecommendation::recommend(
    const GHSChainAxes::AxisScores& axes,
    const juce::Array<juce::PluginDescription>& knownPlugins)
{
    std::vector<SuggestedStage> result;

    for (auto& built : GHSDrumChainVariants::buildDrumChain(axes))
    {
        SuggestedStage stage;
        stage.id = built.id;
        stage.name = built.name;
        stage.role = built.role;
        stage.note = built.note;

        for (auto& opt : built.options)
        {
            SuggestedOption suggested;
            suggested.brand = opt.brand;
            suggested.plugin = opt.plugin;
            suggested.tip = opt.tip;

            if (auto* match = GHSRecipeImport::findBestMatch(opt.plugin, opt.brand, knownPlugins))
            {
                suggested.owned = true;
                suggested.description = *match;
            }

            stage.options.push_back(suggested);
        }

        result.push_back(std::move(stage));
    }

    return result;
}
