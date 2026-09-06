#include "DrumChainVariants.h"
#include "BinaryData.h"
#include <limits>

using GHSChainAxes::Axis;
using GHSChainAxes::AxisScores;
using GHSChainAxes::axisDistance;

namespace
{
    struct IncludeRule
    {
        enum class Type { Always, Gte, AnyGte } type = Type::Always;
        Axis axis = Axis::Drive;               // for Gte
        float value = 0.0f;                    // for Gte
        std::vector<std::pair<Axis, float>> checks; // for AnyGte

        bool evaluate(const AxisScores& axes) const
        {
            switch (type)
            {
                case Type::Always: return true;
                case Type::Gte:    return axes[axis] >= value;
                case Type::AnyGte:
                    for (auto& [ax, v] : checks)
                        if (axes[ax] >= v)
                            return true;
                    return false;
            }
            return true;
        }
    };

    Axis axisFromKey(const juce::String& key)
    {
        for (int i = 0; i < GHSChainAxes::numAxes; ++i)
            if (GHSChainAxes::axisKey((Axis) i) == key)
                return (Axis) i;
        jassertfalse;
        return Axis::Drive;
    }

    AxisScores parseCenter(const juce::DynamicObject* obj)
    {
        AxisScores scores;
        if (obj == nullptr)
            return scores;
        for (auto& prop : obj->getProperties())
            scores[axisFromKey(prop.name.toString())] = (float) (double) prop.value;
        return scores;
    }

    IncludeRule parseIncludeRule(const juce::DynamicObject* obj)
    {
        IncludeRule rule;
        if (obj == nullptr)
            return rule;

        auto type = obj->getProperty("type").toString();
        if (type == "always")
        {
            rule.type = IncludeRule::Type::Always;
        }
        else if (type == "gte")
        {
            rule.type = IncludeRule::Type::Gte;
            rule.axis = axisFromKey(obj->getProperty("axis").toString());
            rule.value = (float) (double) obj->getProperty("value");
        }
        else if (type == "any_gte")
        {
            rule.type = IncludeRule::Type::AnyGte;
            if (auto* checksArr = obj->getProperty("checks").getArray())
            {
                for (auto& checkVar : *checksArr)
                {
                    if (auto* pair = checkVar.getArray())
                    {
                        if (pair->size() == 2)
                        {
                            rule.checks.emplace_back(axisFromKey((*pair)[0].toString()),
                                                      (float) (double) (*pair)[1]);
                        }
                    }
                }
            }
        }
        else
        {
            jassertfalse; // unknown include-rule type - Resources/drum_stage_variants.json shape changed
        }

        return rule;
    }

    struct StageSlotVariant
    {
        juce::String id, name, role, note;
        AxisScores center;
        std::vector<GHSDrumChainVariants::PluginOption> options;
    };

    struct StageSlot
    {
        juce::String label;
        std::vector<Axis> axes; // which axes matter for distance in this slot
        IncludeRule include;
        std::vector<StageSlotVariant> variants;
    };

    const std::vector<StageSlot>& getSlots()
    {
        static const std::vector<StageSlot> slots = []
        {
            std::vector<StageSlot> result;

            auto json = juce::String::fromUTF8(BinaryData::drum_stage_variants_json,
                                                 BinaryData::drum_stage_variants_jsonSize);
            auto parsed = juce::JSON::parse(json);
            auto* arr = parsed.getArray();
            if (arr == nullptr)
                return result;

            for (auto& slotVar : *arr)
            {
                auto* slotObj = slotVar.getDynamicObject();
                if (slotObj == nullptr)
                    continue;

                StageSlot slot;
                slot.label = slotObj->getProperty("label").toString();

                if (auto* axesArr = slotObj->getProperty("axes").getArray())
                    for (auto& a : *axesArr)
                        slot.axes.push_back(axisFromKey(a.toString()));

                slot.include = parseIncludeRule(slotObj->getProperty("include").getDynamicObject());

                if (auto* variantsArr = slotObj->getProperty("variants").getArray())
                {
                    for (auto& variantVar : *variantsArr)
                    {
                        auto* vObj = variantVar.getDynamicObject();
                        if (vObj == nullptr)
                            continue;

                        StageSlotVariant variant;
                        variant.id = vObj->getProperty("id").toString();
                        variant.name = vObj->getProperty("name").toString();
                        variant.role = vObj->getProperty("role").toString();
                        auto noteVar = vObj->getProperty("note");
                        variant.note = noteVar.isVoid() || noteVar.isUndefined() ? juce::String() : noteVar.toString();
                        variant.center = parseCenter(vObj->getProperty("center").getDynamicObject());

                        if (auto* optionsArr = vObj->getProperty("options").getArray())
                        {
                            for (auto& optVar : *optionsArr)
                            {
                                auto* optObj = optVar.getDynamicObject();
                                if (optObj == nullptr)
                                    continue;

                                GHSDrumChainVariants::PluginOption opt;
                                opt.brand = optObj->getProperty("brand").toString();
                                opt.plugin = optObj->getProperty("plugin").toString();
                                opt.tip = optObj->getProperty("tip").toString();
                                variant.options.push_back(opt);
                            }
                        }

                        slot.variants.push_back(variant);
                    }
                }

                result.push_back(slot);
            }

            return result;
        }();

        return slots;
    }

    const StageSlotVariant& pickVariant(const StageSlot& slot, const AxisScores& axes)
    {
        const StageSlotVariant* best = &slot.variants.front();
        float bestDist = std::numeric_limits<float>::infinity();

        for (auto& variant : slot.variants)
        {
            const float dist = axisDistance(axes, variant.center, slot.axes);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &variant;
            }
        }

        return *best;
    }
}

std::vector<GHSDrumChainVariants::BuiltStage> GHSDrumChainVariants::buildDrumChain(const GHSChainAxes::AxisScores& axes)
{
    std::vector<BuiltStage> result;

    int stageNumber = 1;
    for (auto& slot : getSlots())
    {
        if (!slot.include.evaluate(axes))
            continue;

        auto& variant = pickVariant(slot, axes);

        BuiltStage stage;
        stage.id = variant.id;
        stage.name = juce::String(stageNumber++) + ". " + variant.name;
        stage.role = variant.role;
        stage.note = variant.note;
        stage.options = variant.options;

        result.push_back(std::move(stage));
    }

    return result;
}
