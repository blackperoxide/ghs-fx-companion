#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GateProcessor.h"

namespace GateColours
{
    const juce::Colour background { 0xff121418 };
    const juce::Colour panel      { 0xff1c1f25 };
    const juce::Colour panelEdge  { 0xff2a2e36 };
    const juce::Colour text       { 0xffd9dde3 };
    const juce::Colour dimText    { 0xff8a919c };
    const juce::Colour accent     { 0xffff9f2e }; // gate / threshold
    const juce::Colour key        { 0xff38c8e8 }; // detector / key
    const juce::Colour closed     { 0xff3a3f48 }; // audio the gate removed
    const juce::Colour open       { 0xff8ee06a }; // gate-open indicator
}

class GateLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GateLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(juce::FontOptions(13.0f)); }
    juce::Label* createSliderTextBox(juce::Slider&) override;
};

/** A labelled rotary knob bound to one parameter. */
class GateKnob : public juce::Component
{
public:
    GateKnob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title);
    void resized() override;
    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
 * Scrolling display: input level (grey = what got removed, colour = what
 * passed), the detector/key level, the open and close thresholds, and a tick
 * per detected hit. Drag up/down anywhere on it to set the threshold.
 */
class GateDisplay : public juce::Component, private juce::Timer
{
public:
    explicit GateDisplay(GHSDrumGateProcessor&);
    ~GateDisplay() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr float minDb = -72.0f;

private:
    void timerCallback() override;
    float dbToY(float db, juce::Rectangle<float> area) const;
    float yToDb(float y, juce::Rectangle<float> area) const;
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> meterArea() const;

    GHSDrumGateProcessor& proc;
    std::vector<GHSDrumGateProcessor::DisplayFrame> history; // ring, one per pixel column (ish)
    std::vector<GHSDrumGateProcessor::DisplayFrame> scratch = std::vector<GHSDrumGateProcessor::DisplayFrame>(8192);
    int head = 0;
    float meterDb = 0.0f;
    bool dragging = false;
};

class GHSDrumGateEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GHSDrumGateEditor(GHSDrumGateProcessor&);
    ~GHSDrumGateEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addChoice(juce::ComboBox& box, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att,
                   const char* paramId);
    void drawSection(juce::Graphics&, juce::Rectangle<int> r, const juce::String& title);

    GHSDrumGateProcessor& proc;
    GateLookAndFeel lnf;

    GateDisplay display;

    juce::ComboBox presetBox, modeBox, detectBox, keySourceBox;
    juce::ToggleButton keyListenButton { "Key Listen" }, midiButton { "MIDI Out" };
    juce::Label scStatus;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, detectAtt, keySourceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyListenAtt, midiAtt;

    GateKnob threshold, hysteresis, sensitivity, retrigger;
    GateKnob hpf, lpf, lookahead;
    GateKnob attack, hold, release, range, punch;
    GateKnob mix, output, midiNote, midiVel;

    juce::Rectangle<int> detectSection, keySection, envSection, outSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSDrumGateEditor)
};
