#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <mutex>

/**
 * Visuals: the audio-reactive VJ/visualizer side of GHS FX Companion.
 *
 *  - Live band-energy + onset/beat analysis on a background thread (never
 *    the audio thread), fed from a short ring buffer the audio thread writes.
 *  - Host tempo/transport passthrough (first use of AudioPlayHead in this
 *    plugin) for future beat-synced scenes.
 *  - MIDI scene-switch learn: arm a scene, the next CC or note edge binds to
 *    it; bound triggers hand off a pending scene change for the message
 *    thread to apply to the real AudioParameterChoice (never written from
 *    the audio thread).
 */
namespace Visuals
{
    class Engine : private juce::Thread
    {
    public:
        Engine();
        ~Engine() override;

        void prepare(double sampleRate, int samplesPerBlock);

        /** Audio thread. Call last in processBlock, after the rack and the
            Riff House backing mix, so Visuals reacts to what's actually
            heard. */
        void processInput(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

        /** Audio thread. Cheap atomic stores only. */
        void setTransportInfo(bool isPlaying, double bpm, double ppqPosition) noexcept;

        /** Message thread, ~30 Hz. */
        juce::var getLiveState();

        // --- MIDI learn (message thread) ---
        void armMidiLearn(int sceneIndex); // -1 cancels
        void clearBinding(int sceneIndex);
        /** Message thread, ~30 Hz. Returns -1 if no MIDI-triggered scene
            change is pending, else the scene index to apply (consumes it). */
        int consumePendingSceneIndex() noexcept;

        juce::var getMidiLearnState() const;
        void setMidiLearnState(const juce::var& v);

    private:
        void run() override;
        void analyseBlock();
        void maybeLearn(int triggerNumber, bool isCc) noexcept;

        double sampleRate = 44100.0;

        // Short ring buffer: long enough for one FFT window plus a scope trace.
        juce::HeapBlock<float> ring;
        int ringSize = 0;
        std::atomic<juce::int64> totalWritten { 0 };

        static constexpr int fftOrder = 12;      // 4096 samples
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numBands = 20;
        static constexpr int scopeSize = 512;

        juce::dsp::FFT fft { fftOrder }; // background thread only - built once, reused every analyseBlock() call

        std::mutex bandsLock;
        std::array<float, numBands> bands {};
        std::array<float, numBands> bandRunningMax {};
        std::array<float, scopeSize> scope {};

        std::atomic<float> liveRms { 0.0f };
        std::atomic<float> fastEnv { 0.0f }, slowEnv { 0.0f };
        std::atomic<juce::uint32> beatSerial { 0 };
        juce::int64 lastBeatSample = 0;

        std::atomic<bool> transportPlaying { false };
        std::atomic<double> transportBpm { 120.0 }, transportPpq { 0.0 };

        // MIDI edge tracking (same idiom as RiffHouse::Engine's cc[Value|Serial]).
        std::array<std::atomic<int>, 128> ccValue {};
        std::array<std::atomic<juce::uint32>, 128> ccSerial {};
        std::array<std::atomic<juce::uint32>, 128> noteOnSerial {};

        std::atomic<int> armedLearnScene { -1 };
        std::atomic<int> pendingSceneIndex { -1 };

        // trigger number -> scene index, -1 = unbound. Fixed-size and atomic
        // rather than a mutex-guarded map, so every incoming MIDI message
        // (not just the rare learn-mode one) stays lock-free on the audio thread.
        std::array<std::atomic<int>, 128> ccToScene, noteToScene;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
    };
}
