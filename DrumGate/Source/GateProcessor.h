#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "GateDSP.h"

/**
 * GHS Drum Gate - a drum-focused gate/ducker with a filtered key, three
 * detection modes, lookahead, hold, hysteresis, a retrigger (double-hit)
 * guard, a punch accent, parallel mix, external sidechain, key listen,
 * and MIDI trigger output for drum replacement.
 */
class GHSDrumGateProcessor : public juce::AudioProcessor
{
public:
    GHSDrumGateProcessor();
    ~GHSDrumGateProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GHS Drum Gate"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets (Init / Kick / Snare / Toms / Hi-Hat / Room Duck)
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- Display feed (audio thread -> editor), lock-free ----
    struct DisplayFrame
    {
        float inputPeak;   // linear, delayed (what the gate sees on its audio path)
        float keyDb;       // detector level
        float minGain;     // lowest gain applied in this frame (linear)
        bool  hit;         // a gate opening happened in this frame
    };

    /** Editor-side: pulls any pending frames. Returns how many were copied. */
    int popDisplayFrames(DisplayFrame* dest, int maxFrames);

    std::atomic<float> meterGainDb { 0.0f };
    std::atomic<float> meterKeyDb { -120.0f };
    std::atomic<bool>  meterOpen { false };
    std::atomic<bool>  sidechainConnected { false };

    /** Samples per display frame - the editor uses it to label the time axis. */
    static constexpr int samplesPerFrame = 128;
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

private:
    GHSGate::Settings readSettings() const;

    GHSGate::Engine engine;
    juce::AudioBuffer<float> keyBuffer;
    int reportedLatency = -1;
    int currentProgram = 0;
    std::atomic<double> currentSampleRate { 44100.0 };

    // MIDI trigger output state
    int lastNoteSent = -1;

    // display accumulation
    juce::AbstractFifo displayFifo { 8192 };
    std::vector<DisplayFrame> displayBuffer = std::vector<DisplayFrame>(8192);
    int frameCounter = 0;
    DisplayFrame pending { 0.0f, -120.0f, 1.0f, false };

    // cached parameter pointers (real-time safe reads)
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pHysteresis = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pLookahead = nullptr;
    std::atomic<float>* pHpf = nullptr;
    std::atomic<float>* pLpf = nullptr;
    std::atomic<float>* pDetect = nullptr;
    std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pRetrigger = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pPunch = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pKeyListen = nullptr;
    std::atomic<float>* pKeySource = nullptr;
    std::atomic<float>* pMidiOn = nullptr;
    std::atomic<float>* pMidiNote = nullptr;
    std::atomic<float>* pMidiVelSens = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHSDrumGateProcessor)
};

namespace GateParamIDs
{
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* hysteresis = "hysteresis";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* hold       = "hold";
    inline constexpr const char* release    = "release";
    inline constexpr const char* range      = "range";
    inline constexpr const char* lookahead  = "lookahead";
    inline constexpr const char* keyHpf     = "keyHpf";
    inline constexpr const char* keyLpf     = "keyLpf";
    inline constexpr const char* detect     = "detect";
    inline constexpr const char* sens       = "sensitivity";
    inline constexpr const char* retrigger  = "retrigger";
    inline constexpr const char* mode       = "mode";
    inline constexpr const char* punch      = "punch";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
    inline constexpr const char* keyListen  = "keyListen";
    inline constexpr const char* keySource  = "keySource";
    inline constexpr const char* midiOn     = "midiOn";
    inline constexpr const char* midiNote   = "midiNote";
    inline constexpr const char* midiVelSens = "midiVelSens";
}
