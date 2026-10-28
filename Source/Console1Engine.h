#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include "Console1Mapping.h"

/**
 * Console 1 controller layer: turns a Softube Console 1 (Mk I first, but any
 * CC controller works) into hands-on control of the plugins hosted in GHS FX
 * Companion's rack. Not Softube's integration - plain MIDI CCs mapped to
 * hosted plugin parameters, the way Ardour's Console 1 surface does it.
 *
 *   Console 1 --MIDI--> Engine --mapping--> slot N's plugin parameter
 *            <--ring/LED feedback--
 *
 * Where MIDI comes from (user setting):
 *  - device: GHS opens the Console 1 MIDI port itself (DeviceHub). Works in
 *    any DAW with no track routing, and is the only way to send ring/LED
 *    feedback back. Only ONE GHS instance (the "owner") reacts at a time -
 *    the one whose window you opened last or that pressed Take Control.
 *  - host: the DAW routes the controller to this track's MIDI input and the
 *    CCs arrive in processBlock. No feedback.
 *  - both.
 *
 * Mappings ("bindings") are per rack slot: CC -> parameter of the plugin in
 * that slot (or that slot's bypass). A whole per-plugin set can be saved as a
 * profile on disk and comes back automatically the next time that plugin is
 * loaded into any slot, so a knob keeps the same musical job everywhere.
 *
 * If several slots bind the same CC, the focused slot wins (Console 1's track
 * buttons 1-8 pick the focused slot); otherwise the lowest slot. Several
 * bindings on the same CC inside one slot all move together (simple macros).
 *
 * Threading: audio thread and the MIDI-device thread only push raw CC events
 * into lock-free FIFOs. Everything else - parameter writes, learn, feedback -
 * happens on the message thread in a 60 Hz timer, so it keeps working with
 * the editor window closed.
 */
namespace Console1
{
    enum class ControlKind { knob, button };

    struct ControlInfo
    {
        int cc;
        const char* id;      // stable id, e.g. "COMP_THRESH" (same names as Ardour's console1.h)
        const char* label;   // what's printed near it on the hardware
        const char* section; // "input", "shape", "eq", "comp", "drive", "output", "track", "nav"
        ControlKind kind;
    };

    /** Every known Console 1 control, hardware order. */
    const std::vector<ControlInfo>& controls();
    const ControlInfo* findControl(int cc);

    /** Track select buttons 1-20 are CC 21..40; GHS uses 1-8 to focus rack slots. */
    constexpr int trackButtonFirstCc = 21;
    constexpr int pageUpCc = 96;
    constexpr int pageDownCc = 97;

    struct Binding
    {
        int id = 0;
        int channel = 1;   // 1-16, 0 = any channel
        int cc = -1;
        int slot = -1;
        bool targetBypass = false; // true: drives the GHS slot bypass instead of a plugin parameter
        int paramIndex = -1;
        juce::String paramId, paramName;
        Console1Math::Mode mode = Console1Math::Mode::absolute;
        Console1Math::Range range;
        float sensitivity = 0.35f; // relative mode only

        // runtime only (not saved)
        bool pickedUp = false;
    };

    class Engine;

    /**
     * One per process (juce::SharedResourcePointer). Owns the actual MIDI
     * port so several GHS instances in one session never fight over it, and
     * forwards incoming CCs only to the current owner engine.
     */
    class DeviceHub : private juce::MidiInputCallback, private juce::Timer
    {
    public:
        DeviceHub();
        ~DeviceHub() override;

        void registerEngine(Engine*);
        void unregisterEngine(Engine*);
        void claim(Engine*);
        bool isOwner(const Engine*) const;

        /** Empty names = auto (first port whose name contains "Console 1" / "Console1"). Saved to disk. */
        void setPreferredDevices(const juce::String& inputName, const juce::String& outputName);
        juce::String getPreferredInput() const { return preferredIn; }
        juce::String getPreferredOutput() const { return preferredOut; }

        juce::String getOpenInputName() const;
        juce::String getOpenOutputName() const;
        bool hasOutput() const;

        /** Message thread. Returns true if a message actually went out. */
        bool send(int channel, int cc, int value);

        /** Bumped every time the device (re)opens or ownership changes - engines resend all feedback. */
        juce::uint32 getGeneration() const { return generation.load(); }

        static juce::StringArray listInputs();
        static juce::StringArray listOutputs();

    private:
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
        void timerCallback() override; // hot-plug: retry opening every 2 s
        void tryOpen();
        void closePorts();
        void loadSettings();
        void saveSettings() const;

        mutable std::mutex lock; // guards owner/engines/ports; never touched from the audio thread
        std::vector<Engine*> engines;
        Engine* owner = nullptr;

        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        juce::String preferredIn, preferredOut;
        std::atomic<juce::uint32> generation { 1 };
    };

    class Engine : private juce::Timer
    {
    public:
        struct Host
        {
            int numSlots = 8;
            std::function<juce::AudioPluginInstance*(int slot)> pluginInSlot;
            std::function<juce::AudioParameterBool*(int slot)> bypassForSlot;
        };

        explicit Engine(Host host);
        ~Engine() override;

        // ---------------- real-time side ----------------

        /** Audio thread. Queues CCs arriving from the DAW track, and (if the
            "consume" option is on) strips CCs that are bound so hosted plugins
            with their own MIDI learn don't react to them a second time. */
        void processHostMidi(juce::MidiBuffer& midi) noexcept;

        /** MIDI-device thread (DeviceHub). */
        void pushDeviceEvent(int channel, int cc, int value) noexcept;

        // ---------------- message thread ----------------

        juce::var getState();      // full snapshot for the UI
        juce::var getLiveState();  // small, 30 Hz
        juce::var listParams(int slot);

        void setOption(const juce::String& name, const juce::var& value);
        void setDevices(const juce::String& inputName, const juce::String& outputName);
        void takeControl();
        bool isOwner() const;

        void setFocusSlot(int slot);
        int getFocusSlot() const { return focusSlot; }

        /** Next CC that arrives binds to this target. paramIndex ignored if bypass. */
        void armLearn(int slot, int paramIndex, bool bypass);
        void cancelLearn();

        /** Directly bind a known control (picked on the on-screen map) to a target. Returns new id or -1. */
        int addBinding(const juce::var& json);
        bool updateBinding(int id, const juce::var& json);
        bool removeBinding(int id);
        void clearSlot(int slot);

        /** On-screen simulator: behaves exactly like the hardware sent it. */
        void inject(int channel, int cc, int value);

        // rack lifecycle (called by the processor)
        void slotLoaded(int slot);   // a new plugin landed here: drop old bindings, auto-load its profile
        void slotUnloaded(int slot);
        void slotsSwapped(int a, int b);
        void slotStateRestored(int slot); // plugin state changed under us: re-pickup, resync rings

        // per-plugin profiles on disk
        juce::var saveProfileForSlot(int slot);
        juce::var loadProfileForSlot(int slot);
        juce::var listProfiles();

        /** The UI bridge: every native function the Console 1 tab calls, except the two that
            need the editor (c1RevealProfiles, c1OpenEditor). The plugin editor and the
            headless UI test server both route through here, so they can't drift apart. */
        static const juce::StringArray& bridgeFunctionNames();
        juce::var invoke(const juce::String& name, const juce::Array<juce::var>& args);

        // persistence with the DAW session
        juce::var toVar() const;
        void fromVar(const juce::var&);

        static juce::File profilesFolder();
        static juce::String profileKeyFor(const juce::PluginDescription&);

    private:
        struct Event { juce::uint8 channel, cc, value; };

        struct EventFifo
        {
            static constexpr int capacity = 1024;
            juce::AbstractFifo fifo { capacity };
            std::array<Event, capacity> data {};
            void push(Event e) noexcept
            {
                const auto scope = fifo.write(1);
                if (scope.blockSize1 > 0) data[(size_t) scope.startIndex1] = e;
                else if (scope.blockSize2 > 0) data[(size_t) scope.startIndex2] = e;
            }
            template <typename Fn> void drain(Fn&& fn)
            {
                const auto scope = fifo.read(fifo.getNumReady());
                for (int i = 0; i < scope.blockSize1; ++i) fn(data[(size_t) (scope.startIndex1 + i)]);
                for (int i = 0; i < scope.blockSize2; ++i) fn(data[(size_t) (scope.startIndex2 + i)]);
            }
        };

        enum class Source { device, host, both };

        void timerCallback() override;
        void handleCc(int channel, int cc, int value, bool fromSimulator);
        bool handleSystemControl(int cc, int value);
        void applyBinding(Binding&, int value, int previousHw);
        void sendFeedback(bool force);
        void rebuildConsumeMask();
        void resetPickupForSlot(int slot);

        juce::AudioProcessorParameter* resolveParam(Binding&);
        float currentNorm(Binding&);
        std::vector<Binding*> winnersFor(int channel, int cc);
        static int keyOf(int channel, int cc) { return (juce::jlimit(1, 16, channel) - 1) * 128 + juce::jlimit(0, 127, cc); }

        juce::var bindingToVar(const Binding&) const;
        bool bindingFromVar(Binding&, const juce::var&);
        juce::String targetLabel(const Binding&);

        Host host;
        juce::SharedResourcePointer<DeviceHub> hub;

        EventFifo hostFifo, deviceFifo;

        std::vector<Binding> bindings;
        int nextBindingId = 1;
        int focusSlot = 0;

        std::atomic<Source> source { Source::device }; // read on the audio thread
        bool feedbackEnabled = true;
        bool autoLoadProfiles = true;
        bool trackButtonsFocusSlots = true;
        std::atomic<bool> consumeBoundCcs { true };
        juce::String defaultKnobMode = "absolute";

        // learn
        int learnSlot = -1, learnParam = -1;
        bool learnBypass = false;

        // hardware mirror: last value we know each control is at (received or sent), -1 unknown
        std::array<int, 16 * 128> hwValue;
        std::array<int, 16 * 128> sentValue;
        std::array<double, 16 * 128> lastTouchMs;
        juce::uint32 seenGeneration = 0;
        int lastFocusLed = -1;

        // audio-thread-readable copy of which CCs are bound (for consume)
        std::array<std::atomic<bool>, 16 * 128> boundMask;

        // live monitor
        struct Touch { int channel = 0, cc = -1, value = 0; juce::uint32 serial = 0; juce::String target; };
        Touch lastTouch;
        juce::uint32 touchSerial = 0;
        std::array<Touch, 12> recent {};
        int recentHead = 0;
        juce::String lastLearnedMessage;
        juce::uint32 learnedSerial = 0;
        std::atomic<juce::uint32> hostEventCount { 0 }, deviceEventCount { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Engine)
    };
}
