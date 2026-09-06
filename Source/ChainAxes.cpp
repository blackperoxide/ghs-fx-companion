#include "ChainAxes.h"
#include "BinaryData.h"
#include <cmath>

namespace GHSChainAxes
{
    juce::String axisKey(Axis a)
    {
        switch (a)
        {
            case Axis::Drive:      return "drive";
            case Axis::Brightness: return "brightness";
            case Axis::Space:      return "space";
            case Axis::Movement:   return "movement";
            case Axis::Punch:      return "punch";
            case Axis::Width:      return "width";
            case Axis::Lofi:       return "lofi";
            case Axis::Count:      break;
        }
        return {};
    }

    namespace
    {
        Axis axisFromKey(const juce::String& key)
        {
            for (int i = 0; i < numAxes; ++i)
                if (axisKey((Axis) i) == key)
                    return (Axis) i;
            jassertfalse; // JSON has a key our enum doesn't know about - source drifted
            return Axis::Drive;
        }

        AxisScores parseAxisObject(const juce::DynamicObject* obj)
        {
            AxisScores scores;
            if (obj == nullptr)
                return scores;

            for (auto& prop : obj->getProperties())
            {
                auto key = prop.name.toString();
                for (int i = 0; i < numAxes; ++i)
                {
                    if (axisKey((Axis) i) == key)
                    {
                        scores[(Axis) i] = (float) (double) prop.value;
                        break;
                    }
                }
            }
            return scores;
        }

        struct AxisConfig
        {
            std::vector<AxisMeta> meta;
            std::vector<std::pair<juce::String, AxisScores>> vibePoints;
        };

        const AxisConfig& getConfig()
        {
            static const AxisConfig config = []
            {
                AxisConfig result;

                auto json = juce::String::fromUTF8(BinaryData::axis_config_json,
                                                     BinaryData::axis_config_jsonSize);
                auto parsed = juce::JSON::parse(json);
                auto* root = parsed.getDynamicObject();
                if (root == nullptr)
                    return result;

                if (auto* axesArr = root->getProperty("axes").getArray())
                {
                    for (auto& item : *axesArr)
                    {
                        auto* obj = item.getDynamicObject();
                        if (obj == nullptr)
                            continue;

                        AxisMeta m;
                        m.id = axisFromKey(obj->getProperty("id").toString());
                        m.label = obj->getProperty("label").toString();
                        m.lowLabel = obj->getProperty("lowLabel").toString();
                        m.highLabel = obj->getProperty("highLabel").toString();
                        m.description = obj->getProperty("description").toString();
                        result.meta.push_back(m);
                    }
                }

                if (auto* vibeObj = root->getProperty("vibeAxisPoints").getDynamicObject())
                {
                    for (auto& prop : vibeObj->getProperties())
                    {
                        auto scores = parseAxisObject(prop.value.getDynamicObject());
                        result.vibePoints.emplace_back(prop.name.toString(), scores);
                    }
                }

                return result;
            }();

            return config;
        }
    }

    float clampAxis(float value) noexcept
    {
        return juce::jlimit(0.0f, 10.0f, value);
    }

    float axisDistance(const AxisScores& a, const AxisScores& b, const std::vector<Axis>& axesToCompare)
    {
        float sumSq = 0.0f;
        for (auto axis : axesToCompare)
        {
            const float diff = a[axis] - b[axis];
            sumSq += diff * diff;
        }
        return std::sqrt(sumSq);
    }

    const std::vector<AxisMeta>& getAxisMeta()
    {
        return getConfig().meta;
    }

    const std::vector<std::pair<juce::String, AxisScores>>& getVibeAxisPoints()
    {
        return getConfig().vibePoints;
    }

    juce::String nearestVibe(const AxisScores& axes)
    {
        static const std::vector<Axis> allAxes = []
        {
            std::vector<Axis> v;
            for (int i = 0; i < numAxes; ++i)
                v.push_back((Axis) i);
            return v;
        }();

        juce::String best;
        float bestDist = std::numeric_limits<float>::infinity();

        for (auto& [name, point] : getVibeAxisPoints())
        {
            const float dist = axisDistance(axes, point, allAxes);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = name;
            }
        }

        return best;
    }
}
