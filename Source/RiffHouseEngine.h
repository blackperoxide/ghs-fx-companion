#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>

/**
 * Riff House: the play-along / practice side of GHS FX Companion.
 *
 * Everything that needs real audio lives here, in C++, so the game works
 * with whatever is plugged into the track (Tone cable, interface, mic, a DAW
 * bus) and with any MIDI routed to the plugin. The webview only draws the
 * note highway and runs scoring; it reads the live state this engine emits.
 *
 *  - Live input analysis on a background thread (never the audio thread):
 *      YIN monophonic pitch (guitar/bass/voice) + 12-bin chroma (strummed
 *      chords) + held MIDI notes (keys).
 *  - Always-on 60 s retro capture ("Save that") -> WAV + MIDI in the Inbox.
 *  - Loop / slice export of the last capture for samplers and DAWs.
 *  - Song import: audio file or stem (in-plugin monophonic transcription),
 *    MIDI file (e.g. from Basic Pitch), or a song folder made by
 *    tools/riffhouse_import.py (Demucs stems + Basic Pitch MIDI).
 *  - Backing playback (minus-one stems) with pitch-preserving slow-down.
 */
namespace RiffHouse
{
    juce::File rootFolder();   // ~/Music/GHS/RiffHouse
    juce::File chartsFolder(); // ~/Music/GHS/RiffHouse/Charts
    juce::File inboxFolder();  // ~/Music/GHS/Inbox

    /** YIN pitch estimate in Hz (0 if unvoiced). clarity is 1 - aperiodicity (0..1). */
    float detectPitchYin(const float* x, int n, double sampleRate, float& clarity, float minHz = 38.0f, float maxHz = 1400.0f);

    /** WSOLA time-stretch. rate < 1 = slower, pitch unchanged. */
    juce::AudioBuffer<float> timeStretch(const juce::AudioBuffer<float>& in, double rate);

    class Engine : private juce::Thread
    {
    public:
        Engine();
        ~Engine() override;

        void prepare(double sampleRate, int maxBlockSize);

        /** Audio thread, called with the raw input before the rack processes it. Lock-free. */
        void processInput(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

        /** Audio thread, after the rack: mixes the backing track in. Never blocks. */
        void renderBacking(juce::AudioBuffer<float>& buffer) noexcept;

        // ---- message thread API (all return JSON-friendly vars) ----
        juce::var getLiveState();

        juce::var saveThat(double seconds, double bpm);
        juce::var getCaptureWave(int numPoints);
        juce::var exportLoop(double startSec, double endSec, double bpm);
        juce::var exportSlices(double startSec, double endSec, double sensitivity, double bpm);

        /** Background. Transcribes a (mostly monophonic) audio file into a chart. */
        void importAudioAsync(const juce::File& file, const juce::String& instrument, std::function<void(juce::var)> onDone);
        juce::var importMidi(const juce::File& file, const juce::String& instrument);
        /** Background. Song folder from tools/riffhouse_import.py (song.json + stems + midi). */
        void importSongFolderAsync(const juce::File& folder, std::function<void(juce::var)> onDone);

        juce::var listCharts();
        /** Returns the chart JSON and loads its backing (minus the part, unless hearPart). */
        void loadChartAsync(const juce::String& id, bool hearPart, double rate, std::function<void(juce::var)> onDone);

        void setTransport(bool playing, double positionSec, float gain);
        void setRateAsync(double rate, std::function<void()> onDone);

        /** Apollo/Console-style zero-latency monitoring: plugin outputs only the backing, never your dry/wet input. */
        void setMonitorExternally(bool b) { monitorExternally = b; }
        bool isMonitoringExternally() const noexcept { return monitorExternally.load(); }

    private:
        void run() override; // analysis thread

        static juce::var makeChartVar(const juce::String& title, const juce::String& instrument, double bpm,
                                      const std::vector<std::array<double, 3>>& notes,
                                      const juce::StringArray& backing, const juce::String& part,
                                      const juce::String& source);
        static juce::String saveChart(const juce::var& chart);
        static juce::var transcribeBuffer(const juce::AudioBuffer<float>& mono, double sr,
                                          const juce::String& title, const juce::String& instrument,
                                          const juce::StringArray& backing, const juce::String& part,
                                          const juce::String& source);
        juce::var midiFileToChart(const juce::File& f, const juce::String& title, const juce::String& instrument,
                                  const juce::StringArray& backing, const juce::String& part);
        bool readMono(const juce::File& f, juce::AudioBuffer<float>& out, double& sr);
        bool writeWav(const juce::File& f, const float* data, int n, double sr);
        void installBacking(juce::AudioBuffer<float>&& raw, double sr);

        juce::AudioFormatManager formats;

        double sampleRate = 48000.0;

        // retro capture ring (mono) - written by the audio thread only
        juce::HeapBlock<float> ring;
        int ringSize = 0;
        std::atomic<juce::int64> totalWritten { 0 };

        // MIDI
        struct MidiEv { juce::int64 pos; juce::uint8 status, note, vel; };
        juce::AbstractFifo midiFifo { 4096 };
        std::array<MidiEv, 4096> midiEvBuf {};
        std::atomic<juce::uint64> held[2] {};
        std::mutex midiLogLock;
        std::vector<MidiEv> midiLog; // analysis thread fills, trimmed to 60 s

        // live analysis results
        std::atomic<float> liveHz { 0 }, liveClarity { 0 }, liveRms { 0 };
        std::mutex chromaLock;
        std::array<float, 12> chroma {};

        // last saved capture (for loop/slice)
        juce::AudioBuffer<float> lastCapture;
        juce::String lastCaptureName;

        // backing
        juce::SpinLock backingLock;
        juce::AudioBuffer<float> backingRaw, backingPlay; // raw at file rate (resampled to host rate), play = stretched
        double backingRate = 1.0;
        std::atomic<bool> playing { false };
        std::atomic<juce::int64> playPos { 0 };
        std::atomic<float> backingGain { 0.8f };
        std::atomic<bool> busy { false };
        std::atomic<bool> monitorExternally { false };

        // MIDI CC from any controller (pedal, knobs, pads) - the UI does "MIDI learn" for game actions & rack control
        std::atomic<int> ccValue[128];
        std::atomic<juce::uint32> ccSerial[128];

        juce::ThreadPool pool { juce::jmax(2, juce::SystemStats::getNumCpus() - 2) }; // M1 Pro/Max: 8-8 worker threads

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
    };
}
