#pragma once
// Fake hosted plugins for the Console 1 tests (no real plugin formats needed).
#include "../Source/Console1Engine.h"

struct FakeParam : juce::HostedAudioProcessorParameter
{
    FakeParam(juce::String i, juce::String n, int s = 0x7fffffff, float v = 0.5f) : id(i), nm(n), steps(s), value(v) {}
    juce::String id, nm; int steps; float value;
    float getValue() const override { return value; }
    void setValue(float v) override { value = v; }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int) const override { return nm; }
    juce::String getLabel() const override { return {}; }
    float getValueForText(const juce::String& t) const override { return t.getFloatValue(); }
    int getNumSteps() const override { return steps; }
    bool isDiscrete() const override { return steps < 0x7fffffff; }
    juce::String getParameterID() const override { return id; }
};

struct FakePlugin : juce::AudioPluginInstance
{
    FakePlugin(juce::String n, std::vector<std::pair<juce::String, juce::String>> ps, juce::String maker = "TestCo") : name(n), manufacturer(maker)
    {
        for (auto& p : ps) addHostedParameter(std::make_unique<FakeParam>(p.first, p.second, p.first == "type" ? 4 : 0x7fffffff));
    }
    juce::String name, manufacturer;
    void fillInPluginDescription(juce::PluginDescription& d) const override
    { d.name = name; d.manufacturerName = manufacturer; d.pluginFormatName = "VST3"; }
    const juce::String getName() const override { return name; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

