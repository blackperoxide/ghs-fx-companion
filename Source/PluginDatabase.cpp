#include "PluginDatabase.h"
#include "BinaryData.h"
#include <vector>

namespace
{
    constexpr int kMinSubstringLength = 4;

    struct Indexed
    {
        GHSPluginDatabase::Entry entry;
        juce::String normalizedName;
    };

    const std::vector<Indexed>& getIndexed()
    {
        static const std::vector<Indexed> indexed = []
        {
            std::vector<Indexed> result;

            auto json = juce::String::fromUTF8(BinaryData::plugin_database_json,
                                                BinaryData::plugin_database_jsonSize);
            auto parsed = juce::JSON::parse(json);

            if (auto* arr = parsed.getArray())
            {
                result.reserve((size_t) arr->size());

                for (auto& item : *arr)
                {
                    auto* obj = item.getDynamicObject();
                    if (obj == nullptr)
                        continue;

                    GHSPluginDatabase::Entry entry;
                    entry.name = obj->getProperty("name").toString();
                    entry.manufacturer = obj->getProperty("manufacturer").toString();
                    entry.category = obj->getProperty("category").toString();
                    entry.subcategory = obj->getProperty("subcategory").toString();
                    entry.tags = obj->getProperty("tags").toString();

                    result.push_back({ entry, entry.name.toLowerCase() });
                }
            }

            return result;
        }();

        return indexed;
    }
}

const GHSPluginDatabase::Entry* GHSPluginDatabase::lookupPlugin(const juce::String& pluginName)
{
    auto query = pluginName.trim().toLowerCase();
    if (query.isEmpty())
        return nullptr;

    auto& indexed = getIndexed();

    for (auto& item : indexed)
        if (item.normalizedName == query)
            return &item.entry;

    if (query.length() < kMinSubstringLength)
        return nullptr;

    const Entry* best = nullptr;
    int bestLength = 0;

    for (auto& item : indexed)
    {
        if (item.normalizedName.length() < kMinSubstringLength)
            continue;

        if (item.normalizedName.contains(query) || query.contains(item.normalizedName))
        {
            if (item.normalizedName.length() > bestLength)
            {
                bestLength = item.normalizedName.length();
                best = &item.entry;
            }
        }
    }

    return best;
}
