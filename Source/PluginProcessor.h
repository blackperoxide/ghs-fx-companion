#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Milestone 1: this processor is itself a real AU/VST3 plugin that Logic loads
 * as one insert slot - but internally it can scan for, load, and run a single
 * other third-party plugin, passing real audio through it. This is the core
 * mechanism every "plugin chain builder" (Snap Heap, StudioRack, Meaw:Chain)
 * is built on. Chaining multiple hosted plugins and AI-driven chain matching
 * come after this works.
 */
class GHSFXCompanionProcessor : public juce::AudioProcessor
{
public:
    GHSFXCompanionProcessor();
    ~GHSFXCompanionProcessor() override;

    // --- juce::AudioProcessor ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GHS FX Companion"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Hosting ---
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** Scans the standard plugin install locations for this OS and returns what it found. */
    juce::Array<juce::PluginDescription> scanForPlugins();

    /** Async - loads the chosen plugin and swaps it in once ready, on the message thread. */
    void loadHostedPlugin(const juce::PluginDescription& description,
                           std::function<void(const juce::String& errorMessage)> onComplete);

    void unloadHostedPlugin();

    juce::AudioPluginInstance* getHostedPlugin() const { return hostedPlugin.get(); }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::unique_ptr<juce::AudioPluginInstance> hostedPlugin;
    juce::MemoryBlock pendingHostedPluginState;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionProcessor)
};
