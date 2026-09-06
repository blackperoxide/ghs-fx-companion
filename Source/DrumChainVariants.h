#pragma once

#include "ChainAxes.h"
#include <juce_core/juce_core.h>
#include <vector>

/**
 * Native port of ghs-fx-chain-builder's lib/drum-chain-variants.ts: assembles
 * a suggested drum chain from a 7-axis point by picking, for each of 8 fixed
 * "stage slots" (Transient Shaping, Saturation, Bus Compression, ...), the
 * hand-written variant whose axis-space center is nearest the given point -
 * same nearest-neighbor selection, same content, sourced from the same
 * Resources/drum_stage_variants.json (generated from that TS file and
 * cross-checked against its actual include() functions, not hand-guessed).
 *
 * A slot is only included in the built chain at all if its include-rule
 * passes (e.g. Saturation only shows up when drive or lofi is at least 3) -
 * see the "include" rule types below.
 */
namespace GHSDrumChainVariants
{
    struct PluginOption
    {
        juce::String brand, plugin, tip;
    };

    struct BuiltStage
    {
        juce::String id;
        juce::String name;   // already numbered, e.g. "1. Snap the Attack"
        juce::String role;
        juce::String note;   // empty if none
        std::vector<PluginOption> options; // preference order - first is the top pick
    };

    /** Best-effort chain for the given axis point. Empty if the embedded JSON failed to parse. */
    std::vector<BuiltStage> buildDrumChain(const GHSChainAxes::AxisScores& axes);
}
