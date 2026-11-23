#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "GateDSP.h"
#include "AlignDSP.h"
#include "AlignRegistry.h"
#include "SamplerDSP.h"

/**
 * GHS Drum Gate - a drum-focused gate/ducker with a filtered key, three
 * detection modes, lookahead, hold, hysteresis, a retrigger (double-hit)
 * guard, timbre-based Hit Match (learn hits / learn spill), two-band gating,
 * adaptive decay, a per-hit leveller and clipper, a punch accent, parallel mix,
 * external sidechain or MIDI triggering, key listen, and MIDI trigger output
 * for drum replacement - plus cross-instance time / polarity / phase
 * alignment of every mic on the kit (see AlignRegistry.h).
 */
class GHSDrumGateProcessor : public juce::AudioProcessor,
                             public GHSAlign::AlignMember
{
public:
    GHSDrumGateProcessor();
    ~GHSDrumGateProcessor() override;

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
        bool  rejected;    // Hit Match turned a candidate hit away in this frame
    };

    /** Editor-side: pulls any pending frames. Returns how many were copied. */
    int popDisplayFrames(DisplayFrame* dest, int maxFrames);

    std::atomic<float> meterGainDb { 0.0f };
    std::atomic<float> meterKeyDb { -120.0f };
    std::atomic<bool>  meterOpen { false };
    std::atomic<bool>  sidechainConnected { false };

    // ---- Hit Match learning (message thread <-> audio thread) ----
    /** 0 = off, 1 = learning hits, 2 = learning spill. Set by the editor's Learn buttons. */
    std::atomic<int> learnMode { 0 };
    /** Editor asks to forget a profile: bit 1 = hits, bit 2 = spill. Applied on the audio thread. */
    std::atomic<int> clearRequest { 0 };
    std::atomic<int> learnedHitCount { 0 };
    std::atomic<int> learnedSpillCount { 0 };
    std::atomic<float> lastMatchDistance { -1.0f };
    std::atomic<bool> lastHitRejected { false };

    // ---- Alignment (GHSAlign::AlignMember; message thread) ----
    int getAlignGroup() const override;
    bool isAlignReference() const override;
    juce::String getAlignName() const override;
    GHSAlign::CaptureBuffer& getAlignCapture() override { return alignCapture; }
    double getAlignSampleRate() const override { return currentSampleRate.load(); }
    void applyAlignment(const GHSAlign::MemberAlignment&) override;

    GHSAlign::MemberAlignment getAlignment() const;
    /** The name shown in every instance's Align table. Empty = use the host's track name. */
    void setUserAlignName(const juce::String&);
    juce::String getUserAlignName() const;
    void updateTrackProperties(const TrackProperties&) override;
    GHSAlign::Registry& getAlignRegistry() { return *alignRegistry; }

    // ---- Drum replacement (message thread) ----
    /** Loads one-shots (WAV/AIFF/FLAC/...) as the "User Samples" kit. Returns an error message, or empty on success. */
    juce::String loadUserSamples(const juce::Array<juce::File>& files);
    void clearUserSamples();
    juce::StringArray getUserSamplePaths() const;
    /** Layer summary of the selected kit, for the UI: one line per layer. */
    juce::StringArray describeCurrentKit() const;
    std::atomic<int> lastHitLayer { -1 };
    std::atomic<float> lastHitVelocity { 0.0f };
    std::atomic<int> hitCounter { 0 };

    // ---- Output metering ----
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<float> outputPeakDb { -120.0f };
    std::atomic<bool> safetyActive { false }; // the ceiling touched a peak recently

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    /** Samples per display frame - the editor uses it to label the time axis. */
    static constexpr int samplesPerFrame = 128;
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

private:
    GHSGate::Settings readSettings() const;
    int alignBaseSamples() const;
    void setAlignmentInternal(const GHSAlign::MemberAlignment&); // fixed delay every aligned instance adds (and reports as latency)

    // drum replacement
    GHSSampler::Sampler sampler;
    std::vector<std::unique_ptr<GHSSampler::Bank>> synthKits;      // permanent
    std::vector<std::unique_ptr<GHSSampler::Bank>> userBanks;      // message thread owns; old ones freed once unused
    std::atomic<const GHSSampler::Bank*> userBankWanted { nullptr }; // published by the message thread
    std::atomic<const GHSSampler::Bank*> userBankInUse { nullptr };  // acknowledged by the audio thread
    std::atomic<bool> audioPickingBank { false };
    juce::StringArray userSamplePaths;
    void publishUserBank(std::unique_ptr<GHSSampler::Bank>);
    struct PendingTrigger { int offset; float velocity; int onsetBack; };
    std::array<PendingTrigger, 64> pendingTriggers {};
    int numPendingTriggers = 0;

    // bypass + auto gain + safety
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> dryLine;   // input delayed by the plugin's latency, for bypass and auto gain
    int dryWritePos = 0;
    juce::AudioBuffer<float> dryBlock;
    float bypassMix = 0.0f;              // 0 = processed, 1 = bypassed (ramped)
    double agInEnergy = 1e-9, agOutEnergy = 1e-9, agLoudDb = -120.0;
    float agGain = 1.0f;
    int safetyHold = 0;

    juce::SharedResourcePointer<GHSAlign::Registry> alignRegistry;
    GHSAlign::CaptureBuffer alignCapture;
    GHSAlign::Aligner aligner;
    mutable juce::CriticalSection alignLock;  // guards the strings + alignment below (message thread only)
    juce::String hostTrackName, userAlignName;
    GHSAlign::MemberAlignment alignment;
    // audio thread copy of the alignment
    std::atomic<double> rtAlignOffsetSec { 0.0 }, rtAlignPhase { 0.0 };
    std::atomic<bool> rtAlignUsePhase { false }, rtAlignValid { false };
    std::atomic<int> rtAlignVersion { 0 };
    int appliedAlignVersion = -1;
    bool appliedAlignOn = false;

    GHSGate::Engine engine;

    // Learned profiles cross threads under this lock: the audio thread only
    // ever *tries* it, so it can never block on the message thread.
    juce::SpinLock profileLock;
    GHSGate::Profile sharedHitProfile, sharedSpillProfile;
    bool profilesPendingForEngine = false;
    static juce::String profileToString(const GHSGate::Profile&);
    static GHSGate::Profile profileFromString(const juce::String&);
    std::array<GHSGate::MidiTrigger, 256> midiTriggers {};
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
    DisplayFrame pending { 0.0f, -120.0f, 1.0f, false, false };

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
    std::atomic<float>* pMidiInNote = nullptr;
    std::atomic<float>* pAlignOn = nullptr;
    std::atomic<float>* pAlignRef = nullptr;
    std::atomic<float>* pAlignGroup = nullptr;
    std::atomic<float>* pAlignPhase = nullptr;
    std::atomic<float>* pReplaceOn = nullptr;
    std::atomic<float>* pKit = nullptr;
    std::atomic<float>* pSampleLevel = nullptr;
    std::atomic<float>* pOriginalLevel = nullptr;
    std::atomic<float>* pSampleDyn = nullptr;
    std::atomic<float>* pSamplePitch = nullptr;
    std::atomic<float>* pSampleLength = nullptr;
    std::atomic<float>* pSampleOffset = nullptr;
    std::atomic<float>* pSampleChoke = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pSafety = nullptr;
    std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pLive = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pLevelTarget = nullptr;
    std::atomic<float>* pLevel = nullptr;
    std::atomic<float>* pMatchTol = nullptr;
    std::atomic<float>* pHitMatch = nullptr;
    std::atomic<float>* pCrossover = nullptr;
    std::atomic<float>* pRangeHigh = nullptr;
    std::atomic<float>* pBandSplit = nullptr;
    std::atomic<float>* pAdaptive = nullptr;

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
    inline constexpr const char* adaptive   = "adaptiveDecay";
    inline constexpr const char* bandSplit  = "bandSplit";
    inline constexpr const char* rangeHigh  = "rangeHigh";
    inline constexpr const char* crossover  = "crossover";
    inline constexpr const char* hitMatch   = "hitMatch";
    inline constexpr const char* matchTol   = "matchTolerance";
    inline constexpr const char* level      = "level";
    inline constexpr const char* levelTarget = "levelTarget";
    inline constexpr const char* clip       = "clip";
    inline constexpr const char* midiInNote = "midiInNote";
    inline constexpr const char* alignOn    = "alignOn";
    inline constexpr const char* alignRef   = "alignReference";
    inline constexpr const char* alignGroup = "alignGroup";
    inline constexpr const char* alignPhase = "alignPhase";
    inline constexpr const char* replaceOn  = "replaceOn";
    inline constexpr const char* kit        = "replaceKit";
    inline constexpr const char* sampleLevel = "sampleLevel";
    inline constexpr const char* originalLevel = "originalLevel";
    inline constexpr const char* sampleDyn  = "sampleDynamics";
    inline constexpr const char* samplePitch = "samplePitch";
    inline constexpr const char* sampleLength = "sampleLength";
    inline constexpr const char* sampleOffset = "sampleOffset";
    inline constexpr const char* sampleChoke = "sampleChoke";
    inline constexpr const char* autoGain   = "autoGain";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* safety     = "safety";
    inline constexpr const char* ceiling    = "ceiling";
    inline constexpr const char* live       = "liveMode";
}
