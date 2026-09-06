#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

/**
 * The same 7-axis "mixing engineer" model ghs-fx-chain-builder's web app uses
 * to generalize chain recommendation beyond 5 hardcoded vibes (lib/chain-axes.ts
 * there). Axis metadata and each vibe's canonical axis point are read from
 * Resources/axis_config.json (generated from that same TS file), so this
 * plugin and the web app can never drift out of sync on what each axis means.
 */
namespace GHSChainAxes
{
    enum class Axis : int
    {
        Drive = 0,
        Brightness,
        Space,
        Movement,
        Punch,
        Width,
        Lofi,
        Count
    };

    constexpr int numAxes = (int) Axis::Count;

    /** Matches the JSON key / TS Axis string for this axis, e.g. "drive". */
    juce::String axisKey(Axis a);

    /** Each value is 0-10. Indexable by Axis. */
    struct AxisScores
    {
        std::array<float, (size_t) numAxes> values{};

        float& operator[](Axis a) noexcept { return values[(size_t) a]; }
        float operator[](Axis a) const noexcept { return values[(size_t) a]; }
    };

    struct AxisMeta
    {
        Axis id;
        juce::String label, lowLabel, highLabel, description;
    };

    float clampAxis(float value) noexcept;

    /** Euclidean distance between two axis points, restricted to axesToCompare. */
    float axisDistance(const AxisScores& a, const AxisScores& b, const std::vector<Axis>& axesToCompare);

    /** Axis metadata in canonical order, parsed once from the embedded JSON. */
    const std::vector<AxisMeta>& getAxisMeta();

    /** The 5 original hand-written vibes' canonical points in axis-space (for "closest preset" labeling). */
    const std::vector<std::pair<juce::String, AxisScores>>& getVibeAxisPoints();

    /** Which of the 5 original vibes is closest to a given axis point. */
    juce::String nearestVibe(const AxisScores& axes);
}
