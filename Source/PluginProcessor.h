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

    /**
     * Reads the plugin list cache written by the standalone GHSFXScanner app.
     * Never scans/instantiates anything itself - see PluginScanning.h for why.
     * Returns empty if the scanner hasn't been run yet.
     */
    juce::Array<juce::PluginDescription> loadKnownPlugins();

    /** Async - loads the chosen plugin and swaps it in once ready, on the message thread. */
    void loadHostedPlugin(const juce::PluginDescription& description,
                           std::function<void(const juce::String& errorMessage)> onComplete);

    void unloadHostedPlugin();

    juce::AudioPluginInstance* getHostedPlugin() const { return hostedPlugin.get(); }

    /**
     * Quick A/B toggle for the hosted plugin specifically - separate from Logic's
     * own insert-level bypass (the power button on the channel strip), which
     * bypasses this whole AU. This just skips the hosted plugin's processBlock,
     * so you can compare wet/dry without unloading it.
     *
     * Deliberately NOT named getBypassParameter() - that's a real virtual on
     * juce::AudioProcessor that hosts use to wire their own native bypass UI
     * to a parameter. Colliding with it would merge Logic's own bypass button
     * with this one, which is the opposite of what this is for.
     */
    juce::AudioParameterBool* getHostedBypassParameter() const { return bypassParam; }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::unique_ptr<juce::AudioPluginInstance> hostedPlugin;
    juce::MemoryBlock pendingHostedPluginState;

    juce::AudioParameterBool* bypassParam = nullptr;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionProcessor)
};
