#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>

/**
 * Voice: real-time voice-to-MIDI. Sing or hum and the pitch is tracked and
 * turned into MIDI notes, which drive one chosen Rack slot's hosted
 * instrument directly - no external virtual MIDI port, no DAW routing.
 *
 * Reuses RiffHouse::Engine's proven live-pitch pipeline (RiffHouse::decimate,
 * RiffHouse::rmsOf, RiffHouse::detectPitchYin, same ring-buffer + background
 * thread shape) and Visuals::Engine's fast/slow envelope-follower idiom for
 * deciding when a note starts/stops. Melody mode only in v1; vocal-percussion
 * rhythm mode (trainable hit classification) is a separate v2, not started.
 */
namespace Voice
{
    class Engine : private juce::Thread
    {
    public:
        Engine();
        ~Engine() override;

        void prepare(double sampleRate, int samplesPerBlock, int numRackSlots);

        /** Audio thread. Call where RiffHouse::Engine::processInput is called,
            before the rack loop. Cheap: a ring-buffer write plus a couple of
            atomic stores. Never allocates, never blocks. */
        void processInput(const juce::AudioBuffer<float>& buffer, bool enabledNow, float sensitivityNow01) noexcept;

        /** Audio thread. Call once per block, right before the rack loop.
            Appends any note on/off decided since the last block - plus a
            forced note-off if Voice was just disabled or re-targeted to a
            different slot - to `target` at sample 0. Never blocks. */
        void drainPendingMidiInto(juce::MidiBuffer& target) noexcept;

        // --- engine-local config (message thread only; not host parameters -
        // same bucket as Console1::Engine's focusSlot/defaultKnobMode: a
        // setup-once routing choice, not something a host automates) ---
        void setTargetSlot(int slot) noexcept;   // -1 = none; clamped to [0, numSlots)
        int getTargetSlot() const noexcept { return targetSlot.load(); }
        void setMidiChannel(int channel1to16) noexcept;
        int getMidiChannel() const noexcept { return midiChannel.load(); }

        /** Message thread, ~30 Hz. */
        juce::var getLiveState();

        /** Session persistence: target slot + MIDI channel only. */
        juce::var toVar() const;
        void fromVar(const juce::var& v);

    private:
        void run() override;

        struct VoiceEvent { juce::uint8 status, note, velocity; };
        void pushEvent(juce::uint8 status, juce::uint8 note, juce::uint8 velocity) noexcept;

        double sampleRate = 48000.0;
        int numSlots = 8;

        // Ring buffer the audio thread writes and the background thread reads,
        // same shape as RiffHouse::Engine/Visuals::Engine.
        juce::HeapBlock<float> ring;
        int ringSize = 0;
        std::atomic<juce::int64> totalWritten { 0 };

        std::atomic<bool> enabled { false };
        std::atomic<float> sensitivity { 0.5f };
        std::atomic<int> targetSlot { -1 };
        std::atomic<int> midiChannel { 1 };
        std::atomic<bool> forceNoteOffPending { false };

        // Background-thread-only note state machine.
        int sustainedNote = -1;
        int candidateNote = -1;
        int candidateCount = 0;
        int framesSinceOnset = 1000;
        float fastEnv = 0.0f, slowEnv = 0.0f;
        int quietTicks = 0;

        // Background thread -> audio thread MIDI handoff (same shape as
        // Console1::Engine's EventFifo).
        static constexpr int fifoCapacity = 64;
        juce::AbstractFifo midiOutFifo { fifoCapacity };
        std::array<VoiceEvent, fifoCapacity> midiOutBuf {};

        // Audio-thread-exclusive: which note (if any) this engine currently
        // believes is sounding, so a forced note-off targets the right pitch.
        int audioThreadActiveNote = -1;
        int audioThreadActiveChannel = 1;

        std::atomic<float> liveHz { 0.0f }, liveClarity { 0.0f }, liveRms { 0.0f };
        std::atomic<int> liveNote { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
    };
}
