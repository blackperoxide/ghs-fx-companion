#include "VoiceEngine.h"
#include "RiffHouseEngine.h"
#include <cmath>

namespace Voice
{

namespace
{
    constexpr float minClarity = 0.72f;          // looser than transcribeBuffer's offline 0.8 - real-time windows are noisier
    constexpr int confirmTicks = 3;               // ~36 ms at a 12 ms tick, agreeing ticks required for a NEW note
    constexpr int minTicksSinceOnset = 5;         // ~60 ms, mirrors transcribeBuffer's 0.07 s minimum note duration
    constexpr int reAttackMinTicks = 8;           // ~96 ms before the same pitch can re-trigger
    constexpr int quietTicksBeforeOff = 4;        // ~48 ms below gate before releasing a held note
}

Engine::Engine() : juce::Thread("Voice analysis")
{
    startThread(juce::Thread::Priority::low);
}

Engine::~Engine()
{
    stopThread(2000);
}

void Engine::prepare(double sr, int, int numRackSlots)
{
    numSlots = juce::jmax(1, numRackSlots);
    if (sr == sampleRate && ring != nullptr) return;
    sampleRate = sr;
    ringSize = juce::jmax((int) (sr * 1.0), 8192);
    ring.calloc((size_t) ringSize);
    totalWritten = 0;
}

void Engine::processInput(const juce::AudioBuffer<float>& buffer, bool enabledNow, float sensitivityNow01) noexcept
{
    sensitivity.store(juce::jlimit(0.0f, 1.0f, sensitivityNow01), std::memory_order_relaxed);

    const bool wasEnabled = enabled.exchange(enabledNow, std::memory_order_relaxed);
    if (wasEnabled && !enabledNow)
        forceNoteOffPending.store(true, std::memory_order_relaxed);

    if (!enabledNow || ring == nullptr) return;

    const auto base = totalWritten.load(std::memory_order_relaxed);
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    if (chans <= 0 || n <= 0) return;

    const float g = 1.0f / (float) chans;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < chans; ++c) s += buffer.getSample(c, i);
        ring[(size_t) ((base + i) % ringSize)] = s * g;
    }
    totalWritten.store(base + n, std::memory_order_release);
}

void Engine::pushEvent(juce::uint8 status, juce::uint8 note, juce::uint8 velocity) noexcept
{
    const auto scope = midiOutFifo.write(1);
    if (scope.blockSize1 > 0) midiOutBuf[(size_t) scope.startIndex1] = { status, note, velocity };
    else if (scope.blockSize2 > 0) midiOutBuf[(size_t) scope.startIndex2] = { status, note, velocity };
}

void Engine::drainPendingMidiInto(juce::MidiBuffer& target) noexcept
{
    const int channel = juce::jlimit(1, 16, midiChannel.load(std::memory_order_relaxed));

    if (forceNoteOffPending.exchange(false, std::memory_order_relaxed) && audioThreadActiveNote >= 0)
    {
        target.addEvent(juce::MidiMessage::noteOff((int) audioThreadActiveChannel, audioThreadActiveNote), 0);
        audioThreadActiveNote = -1;
    }

    int s1, n1, s2, n2;
    const int ready = midiOutFifo.getNumReady();
    midiOutFifo.prepareToRead(ready, s1, n1, s2, n2);
    auto apply = [&](const VoiceEvent& e)
    {
        if (e.status == 0x90)
        {
            target.addEvent(juce::MidiMessage::noteOn(channel, (int) e.note, (juce::uint8) e.velocity), 0);
            audioThreadActiveNote = e.note;
            audioThreadActiveChannel = channel;
        }
        else
        {
            target.addEvent(juce::MidiMessage::noteOff(channel, (int) e.note), 0);
            if (audioThreadActiveNote == e.note) audioThreadActiveNote = -1;
        }
    };
    for (int i = 0; i < n1; ++i) apply(midiOutBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) apply(midiOutBuf[(size_t) (s2 + i)]);
    midiOutFifo.finishedRead(n1 + n2);
}

void Engine::setTargetSlot(int slot) noexcept
{
    const int clamped = juce::isPositiveAndBelow(slot, numSlots) ? slot : -1;
    if (clamped != targetSlot.exchange(clamped, std::memory_order_relaxed))
        forceNoteOffPending.store(true, std::memory_order_relaxed);
}

void Engine::setMidiChannel(int channel1to16) noexcept
{
    midiChannel.store(juce::jlimit(1, 16, channel1to16), std::memory_order_relaxed);
}

void Engine::run()
{
    std::vector<float> win;

    while (!threadShouldExit())
    {
        wait(12);
        ++framesSinceOnset;

        if (!enabled.load(std::memory_order_relaxed))
        {
            liveHz = 0.0f; liveClarity = 0.0f; liveRms = 0.0f; liveNote = -1;
            // The audio thread already force-released any held note on disable
            // (forceNoteOffPending); drop this thread's note memory too, or a
            // re-enable could mistake a fresh sung note for one still sustained.
            sustainedNote = -1; candidateNote = -1; candidateCount = 0;
            fastEnv = 0.0f; slowEnv = 0.0f;
            continue;
        }

        const auto total = totalWritten.load(std::memory_order_acquire);
        const int n = 4096;
        if (ring == nullptr || total < n) continue;
        win.resize((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = ring[(size_t) ((total - n + i) % ringSize)];

        const float r = RiffHouse::rmsOf(win.data() + n - 2048, 2048);
        liveRms = r;

        const float sens = sensitivity.load(std::memory_order_relaxed);
        const float gate = juce::jmap(sens, 0.0f, 1.0f, 0.012f, 0.0015f);

        // Single-pole envelope followers, tick-rate coefficients (this loop's
        // own ~12 ms cadence is the "block" here, not an audio callback).
        constexpr float dt = 0.012f;
        const float fastCoeff = 1.0f - std::exp(-dt / 0.010f);
        const float slowCoeff = 1.0f - std::exp(-dt / 0.100f);
        fastEnv += (r - fastEnv) * fastCoeff;
        slowEnv += (r - slowEnv) * slowCoeff;

        if (r < gate)
        {
            liveHz = 0.0f; liveClarity = 0.0f;
            if (sustainedNote >= 0)
            {
                if (++quietTicks >= quietTicksBeforeOff)
                {
                    pushEvent(0x80, (juce::uint8) sustainedNote, 0);
                    sustainedNote = -1;
                    liveNote = -1;
                }
            }
            candidateNote = -1; candidateCount = 0;
            continue;
        }
        quietTicks = 0;

        double dsr;
        auto dec = RiffHouse::decimate(win.data(), n, sampleRate, dsr);
        const int dn = juce::jmin((int) dec.size(), 1024);
        float clarity = 0.0f;
        const float hz = dn > 0 ? RiffHouse::detectPitchYin(dec.data() + dec.size() - (size_t) dn, dn, dsr, clarity) : 0.0f;
        liveHz = hz;
        liveClarity = clarity;

        if (hz <= 0.0f || clarity < minClarity)
        {
            candidateNote = -1; candidateCount = 0;
            continue;
        }

        const int note = juce::jlimit(0, 127, (int) std::lround(69.0 + 12.0 * std::log2((double) hz / 440.0)));
        const juce::uint8 velocity = (juce::uint8) juce::jlimit(1, 127,
            (int) juce::jmap(r, 0.01f, 0.35f, 35.0f, 127.0f));

        if (sustainedNote < 0)
        {
            if (candidateNote == note) ++candidateCount; else { candidateNote = note; candidateCount = 1; }
            if (candidateCount >= confirmTicks && framesSinceOnset > minTicksSinceOnset)
            {
                pushEvent(0x90, (juce::uint8) note, velocity);
                sustainedNote = note;
                liveNote = note;
                framesSinceOnset = 0;
                candidateCount = 0;
            }
        }
        else if (note == sustainedNote)
        {
            candidateNote = -1; candidateCount = 0;
            if (fastEnv > slowEnv * 1.6f && slowEnv > gate && framesSinceOnset > reAttackMinTicks)
            {
                pushEvent(0x80, (juce::uint8) sustainedNote, 0);
                pushEvent(0x90, (juce::uint8) note, velocity);
                framesSinceOnset = 0;
            }
        }
        else
        {
            if (candidateNote == note) ++candidateCount; else { candidateNote = note; candidateCount = 1; }
            if (candidateCount >= confirmTicks && framesSinceOnset > minTicksSinceOnset)
            {
                pushEvent(0x80, (juce::uint8) sustainedNote, 0);
                pushEvent(0x90, (juce::uint8) note, velocity);
                sustainedNote = note;
                liveNote = note;
                framesSinceOnset = 0;
                candidateCount = 0;
            }
        }
    }
}

juce::var Engine::getLiveState()
{
    auto* o = new juce::DynamicObject();
    o->setProperty("enabled", enabled.load());
    o->setProperty("hz", (double) liveHz.load());
    o->setProperty("clarity", (double) liveClarity.load());
    o->setProperty("rms", (double) liveRms.load());
    o->setProperty("note", liveNote.load());
    o->setProperty("targetSlot", targetSlot.load());
    o->setProperty("midiChannel", midiChannel.load());
    return juce::var(o);
}

juce::var Engine::toVar() const
{
    juce::var v(new juce::DynamicObject());
    auto* o = v.getDynamicObject();
    o->setProperty("version", 1);
    o->setProperty("targetSlot", targetSlot.load());
    o->setProperty("midiChannel", midiChannel.load());
    return v;
}

void Engine::fromVar(const juce::var& v)
{
    if (!v.isObject()) return;
    targetSlot = juce::isPositiveAndBelow((int) v.getProperty("targetSlot", -1), numSlots)
                     ? (int) v.getProperty("targetSlot", -1) : -1;
    midiChannel = juce::jlimit(1, 16, (int) v.getProperty("midiChannel", 1));
}

}
