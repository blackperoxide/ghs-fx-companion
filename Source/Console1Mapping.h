#pragma once

/**
 * Console 1 mapping math - pure C++, no JUCE, so it can be unit tested on its
 * own (tools/test_console1_mapping.cpp) without building the whole plugin.
 *
 * Everything here works on normalised values (0..1, the same range every
 * hosted plugin parameter uses) and 7-bit MIDI CC values (0..127, what the
 * Console 1 Mk I sends in MIDI mode).
 *
 * Hardware facts this file assumes (from Ardour's Console 1 surface and
 * community testing; NOT yet confirmed on Shawn's own unit):
 *  - Every control is a CC on MIDI channel 1.
 *  - Knobs send absolute 0..127, normally moving in steps of about 3
 *    (steps of 1 with Fine Adjust held).
 *  - Sending the same CC back to the device moves its LED ring and resets
 *    its internal counter to that value (this is what makes "relative /
 *    fine" mode and jump-free switching possible).
 *  - Buttons send 127 on press and 0 on release; sending 127/0 back lights
 *    or clears the LED.
 */

#include <algorithm>
#include <cmath>
#include <optional>

namespace Console1Math
{
    enum class Mode
    {
        absolute,  // knob position = parameter position (jumps if they disagree and there's no ring feedback)
        pickup,    // ignore the knob until it passes through the parameter's current value (for no-feedback setups)
        relative,  // only the change in knob value matters; sensitivity < 1 gives finer control than 0..127
        toggle,    // button: each press flips the parameter between its range ends
        momentary  // button: held = range max, released = range min
    };

    inline const char* modeName(Mode m)
    {
        switch (m)
        {
            case Mode::absolute:  return "absolute";
            case Mode::pickup:    return "pickup";
            case Mode::relative:  return "relative";
            case Mode::toggle:    return "toggle";
            case Mode::momentary: return "momentary";
        }
        return "absolute";
    }

    inline Mode modeFromName(const char* s, Mode fallback)
    {
        auto eq = [s](const char* t) { for (int i = 0;; ++i) { if (s[i] != t[i]) return false; if (s[i] == 0) return true; } };
        if (s == nullptr) return fallback;
        if (eq("absolute"))  return Mode::absolute;
        if (eq("pickup"))    return Mode::pickup;
        if (eq("relative"))  return Mode::relative;
        if (eq("toggle"))    return Mode::toggle;
        if (eq("momentary")) return Mode::momentary;
        return fallback;
    }

    inline bool isButtonMode(Mode m) { return m == Mode::toggle || m == Mode::momentary; }

    struct Range
    {
        float min = 0.0f;   // normalised parameter value at knob fully left
        float max = 1.0f;   // normalised parameter value at knob fully right
        bool invert = false;
    };

    inline float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

    /** 0..127 knob position -> normalised parameter value inside the range. */
    inline float ccToNorm(int cc, const Range& r)
    {
        float t = (float) std::min(127, std::max(0, cc)) / 127.0f;
        if (r.invert) t = 1.0f - t;
        return clamp01(r.min + t * (r.max - r.min));
    }

    /** Normalised parameter value -> where the knob ring should sit (inverse of ccToNorm). */
    inline int normToCc(float norm, const Range& r)
    {
        const float span = r.max - r.min;
        float t = std::fabs(span) < 1.0e-6f ? 0.0f : (norm - r.min) / span;
        t = clamp01(t);
        if (r.invert) t = 1.0f - t;
        return (int) std::lround(t * 127.0f);
    }

    /** Snap to a stepped parameter's grid (numSteps = number of distinct values, e.g. 2 for a switch). */
    inline float snapToSteps(float norm, int numSteps)
    {
        if (numSteps < 2 || numSteps > 1024) return norm;
        const float n = (float) (numSteps - 1);
        return clamp01(std::round(norm * n) / n);
    }

    /**
     * Soft takeover. Returns a new value once the knob has "caught" the
     * parameter: either it's within `tolerance` CC steps of the parameter, or
     * it moved across it between the previous and current message. Until
     * then returns nothing and the parameter stays put.
     * `pickedUp` is kept by the caller per binding and reset whenever the
     * parameter may have moved without the knob (slot switch, preset load).
     */
    inline std::optional<float> pickup(int cc, int previousCc, float currentNorm, const Range& r, bool& pickedUp, int tolerance = 3)
    {
        if (!pickedUp)
        {
            const int target = normToCc(currentNorm, r);
            const bool close = std::abs(cc - target) <= tolerance;
            const bool crossed = previousCc >= 0 && ((previousCc <= target && cc >= target) || (previousCc >= target && cc <= target));
            if (!(close || crossed))
                return std::nullopt;
            pickedUp = true;
        }
        return ccToNorm(cc, r);
    }

    /**
     * Relative / fine. Uses only how far the knob moved since the last value
     * we know the hardware was at (either the last value it sent, or the last
     * ring position we sent it). sensitivity 1.0 = a full 0..127 sweep covers
     * the full range; 0.25 = four sweeps' worth of precision.
     * Returns nothing when there's no previous value yet (first touch just
     * records where the knob is).
     */
    inline std::optional<float> relative(int cc, int previousCc, float currentNorm, const Range& r, float sensitivity)
    {
        if (previousCc < 0) return std::nullopt;
        const int delta = cc - previousCc;
        if (delta == 0) return std::nullopt;
        float step = (float) delta / 127.0f * sensitivity * (r.max - r.min);
        if (r.invert) step = -step;
        const float lo = std::min(r.min, r.max), hi = std::max(r.min, r.max);
        return std::min(hi, std::max(lo, currentNorm + step));
    }

    inline bool isPress(int value) { return value >= 64; }

    /** Toggle: on press, flip between range ends. Returns nothing on release. */
    inline std::optional<float> toggle(int value, float currentNorm, const Range& r)
    {
        if (!isPress(value)) return std::nullopt;
        const float mid = 0.5f * (r.min + r.max);
        const bool currentlyHigh = (r.max >= r.min) ? currentNorm >= mid : currentNorm <= mid;
        return currentlyHigh ? r.min : r.max;
    }

    inline float momentary(int value, const Range& r) { return isPress(value) ? r.max : r.min; }

    /** LED for a button binding: lit when the parameter sits in the "max" half of its range. */
    inline bool ledOn(float currentNorm, const Range& r)
    {
        const float mid = 0.5f * (r.min + r.max);
        return (r.max >= r.min) ? currentNorm >= mid : currentNorm <= mid;
    }
}
