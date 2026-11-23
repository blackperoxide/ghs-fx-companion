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
    const juce::Colour reject     { 0xffe8505b }; // hit turned away by Hit Match
    const juce::Colour dynamics   { 0xffc58cff }; // leveller / clipper
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

/**
 * The ALIGN tab: this track's role in the kit alignment, the Analyse / Reset
 * controls shared by the group, and a table of every instance in the group
 * with its time offset, polarity / phase and how much better it lines up.
 */
class AlignPanel : public juce::Component, private juce::Timer
{
public:
    explicit AlignPanel(GHSDrumGateProcessor&);
    ~AlignPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();

    GHSDrumGateProcessor& proc;
    juce::TextEditor nameEditor;
    juce::ToggleButton alignButton { "Align" }, refButton { "Reference" }, alignOnlyButton { "Align only (no gating)" };
    juce::ComboBox groupBox, modeBox;
    juce::TextButton analyseButton { "Analyse" }, resetButton { "Reset" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignAtt, refAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> groupAtt;

    GHSAlign::Registry::GroupStatus status;
    std::vector<GHSAlign::MemberReport> rows;
    juce::Rectangle<int> leftArea, rightArea, tableArea, statusArea, helpArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlignPanel)
};

/**
 * The REPLACE tab: drum replacement / reinforcement. Pick a built-in kit or
 * drop your own one-shots (auto-layered by loudness), and set how the samples
 * sit against the original.
 */
class ReplacePanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ReplacePanel(GHSDrumGateProcessor&);
    ~ReplacePanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragging = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void load(const juce::Array<juce::File>&);

    GHSDrumGateProcessor& proc;
    juce::ToggleButton replaceButton { "Replace / Add Samples" }, chokeButton { "Choke" };
    juce::ComboBox kitBox;
    juce::TextButton loadButton { "Load Samples..." }, clearButton { "Clear" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> replaceAtt, chokeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> kitAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    GateKnob sampleLevel, originalLevel, dynamics, pitch, length, offset;
    juce::StringArray layers;
    juce::String message;
    int lastHitCount = 0;
    float flash = 0.0f;
    int flashLayer = -1;
    float flashVelocity = 0.0f;
    bool dragging = false;
    juce::Rectangle<int> left, right, listArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplacePanel)
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
    juce::ToggleButton splitButton { "Two-Band" }, matchButton { "Match" };
    juce::ToggleButton learnHitsButton { "Learn Hits" }, learnSpillButton { "Learn Spill" };
    juce::TextButton clearButton { "Clear" };
    juce::Label scStatus, matchStatus;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, detectAtt, keySourceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyListenAtt, midiAtt, splitAtt, matchAtt;
    void setLearnMode(int mode);

    GateKnob threshold, hysteresis, sensitivity, retrigger;
    GateKnob matchTol;
    GateKnob hpf, lpf, lookahead, midiInNote;
    GateKnob attack, hold, release, adaptive, punch;
    GateKnob range, rangeHigh, crossover;
    GateKnob level, levelTarget, clip;
    GateKnob mix, output, midiNote, midiVel;

    juce::Rectangle<int> detectSection, matchSection, keySection, envSection, bandSection, dynSection, outSection;

    // GATE / ALIGN tabs
    juce::TextButton gateTab { "GATE" }, alignTab { "ALIGN" }, replaceTab { "REPLACE" };
    AlignPanel alignPanel;
    ReplacePanel replacePanel;
    int tab = 0; // 0 gate, 1 align, 2 replace
    bool showingAlign = false; // any tab other than GATE (the gate sections aren't drawn)
    void showTab(int which);

    // global switches in the header
    juce::ToggleButton bypassButton { "Bypass" }, autoGainButton { "Auto Gain" }, liveButton { "Live" }, safetyButton { "Safety" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt, autoGainAtt, liveAtt, safetyAtt;
    GateKnob ceiling;
    std::vector<juce::Component*> gateComponents();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSDrumGateEditor)
};
