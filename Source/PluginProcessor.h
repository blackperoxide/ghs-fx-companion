#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Milestone 2: this processor hosts a bounded rack of third-party plugins in
 * series (a real "chain," not just one plugin) - the mechanism every
 * "plugin chain builder" (Snap Heap, StudioRack, Blockchain Ultra) is built
 * on. The rack size is fixed at construction (maxChainSlots) rather than
 * growing/shrinking at runtime: AU/VST3 hosts cache the parameter list at
 * load time, so a parameter count that changes later is asking for trouble.
 * Empty slots are just pass-through - that's how every real competitor in
 * this category handles an unfilled rack slot too.
 */
class GHSFXCompanionProcessor : public juce::AudioProcessor
{
public:
    /** Fixed rack size. A slot with no plugin loaded is a no-op pass-through. */
    static constexpr int maxChainSlots = 8;

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

    // --- Named chain presets (separate from the host save/restore above -
    // see ChainPresets.h) ---

    /** Alphabetically sorted list of saved preset names. */
    juce::StringArray getChainPresetNames();

    /** Writes the current chain to a named preset file. Returns false on I/O failure. */
    bool saveChainPreset(const juce::String& name);

    /** Async, like setStateInformation - onComplete fires once every matched slot has finished loading. */
    void loadChainPreset(const juce::String& name, std::function<void()> onComplete = nullptr);

    bool deleteChainPreset(const juce::String& name);

    // --- Hosting ---
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /**
     * Reads the plugin list cache written by the standalone GHSFXScanner app.
     * Never scans/instantiates anything itself - see PluginScanning.h for why.
     * Returns empty if the scanner hasn't been run yet.
     */
    juce::Array<juce::PluginDescription> loadKnownPlugins();

    /**
     * Async - loads the chosen plugin into the given rack slot (replacing
     * whatever was there) and swaps it in once ready, on the message thread.
     */
    void loadPluginIntoSlot(int slotIndex,
                             const juce::PluginDescription& description,
                             std::function<void(const juce::String& errorMessage)> onComplete);

    void unloadSlot(int slotIndex);

    /** Swaps two slots' plugins (and their bypass state) in place - the reorder operation. */
    void moveSlot(int fromIndex, int toIndex);

    juce::AudioPluginInstance* getPluginInSlot(int slotIndex) const;

    /**
     * One bypass parameter per slot, all declared at construction (see the class
     * comment on why the count can't be dynamic). Skips just that slot's
     * processBlock - separate from Logic's own insert-level bypass.
     */
    juce::AudioParameterBool* getSlotBypassParameter(int slotIndex) const;

private:
    /** Same shape used by getStateInformation and by named presets - built once, serialized two ways. */
    juce::ValueTree chainStateToValueTree();

    /** Applies a chain ValueTree (as produced by chainStateToValueTree); async since it loads plugins. */
    void applyChainStateValueTree(const juce::ValueTree& state, std::function<void()> onAllSlotsLoaded = nullptr);

    struct ChainSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioParameterBool* bypassParam = nullptr; // owned by the AudioProcessor parameter list
        juce::MemoryBlock pendingState;
    };

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::array<ChainSlot, maxChainSlots> chain;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSFXCompanionProcessor)
};
