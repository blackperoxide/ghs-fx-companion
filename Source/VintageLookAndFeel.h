#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * A brushed-metal, illuminated-switch look inspired by vintage console gear
 * (UAD's A-Type-style square popup buttons, warm ivory engraved legends,
 * amber/red lit toggles) - purely procedural (gradients/bevels drawn in
 * code), no image assets, so it stays lightweight and resizes cleanly.
 * Applied once to the whole editor via setLookAndFeel(), so every stock
 * JUCE component (TextButton, ToggleButton, ComboBox, ListBox, TextEditor,
 * Label) picks it up without each usage site needing its own styling.
 */
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    /** Brushed-metal panel fill with corner screws - used for the editor's own background. */
    static void drawConsolePanel(juce::Graphics&, juce::Rectangle<float> bounds);

    static const juce::Colour bodyDark;
    static const juce::Colour panelMetalHi;
    static const juce::Colour panelMetalLo;
    static const juce::Colour bezelDark;
    static const juce::Colour cream;
    static const juce::Colour creamDim;
    static const juce::Colour amber;
    static const juce::Colour amberBright;
    static const juce::Colour redLED;
};
