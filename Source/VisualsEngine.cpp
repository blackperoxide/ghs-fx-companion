#include "VisualsEngine.h"
#include <cmath>

namespace Visuals
{

Engine::Engine() : juce::Thread("Visuals analysis")
{
    for (int i = 0; i < 128; ++i)
    {
        ccValue[i] = 0;
        ccSerial[i] = 0;
        noteOnSerial[i] = 0;
        ccToScene[i] = -1;
        noteToScene[i] = -1;
    }
    startThread(juce::Thread::Priority::low);
}

Engine::~Engine()
{
    stopThread(2000);
}

void Engine::prepare(double sr, int)
{
    if (sr == sampleRate && ring != nullptr) return;
    sampleRate = sr;
    ringSize = (int) (sr * 2.0);
    ring.calloc((size_t) juce::jmax(ringSize, fftSize + 1));
    ringSize = juce::jmax(ringSize, fftSize + 1);
    totalWritten = 0;
    lastBeatSample = 0;
}

void Engine::processInput(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const auto base = totalWritten.load(std::memory_order_relaxed);
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if (ring != nullptr && chans > 0 && n > 0)
    {
        const float g = 1.0f / (float) chans;
        float sumSq = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < chans; ++c) s += buffer.getSample(c, i);
            s *= g;
            ring[(size_t) ((base + i) % ringSize)] = s;
            sumSq += s * s;
        }
        totalWritten.store(base + n, std::memory_order_release);

        const float blockRms = std::sqrt(sumSq / (float) n);
        liveRms.store(blockRms, std::memory_order_relaxed);

        // Single-pole envelope followers; coefficients derived from the block
        // duration so the ~10ms/~300ms time constants hold at any block size.
        const float blockSeconds = (float) n / (float) sampleRate;
        const float fastCoeff = 1.0f - std::exp(-blockSeconds / 0.010f);
        const float slowCoeff = 1.0f - std::exp(-blockSeconds / 0.300f);
        const float prevFast = fastEnv.load(std::memory_order_relaxed);
        const float prevSlow = slowEnv.load(std::memory_order_relaxed);
        const float fe = prevFast + (blockRms - prevFast) * fastCoeff;
        const float se = prevSlow + (blockRms - prevSlow) * slowCoeff;
        fastEnv.store(fe, std::memory_order_relaxed);
        slowEnv.store(se, std::memory_order_relaxed);

        const juce::int64 refractory = (juce::int64) (sampleRate * 0.1);
        if (fe > se * 1.5f && se > 0.001f && (base - lastBeatSample) > refractory)
        {
            lastBeatSample = base;
            beatSerial.fetch_add(1, std::memory_order_relaxed);
        }
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            const int num = m.getControllerNumber();
            if (num >= 0 && num < 128)
            {
                ccValue[num].store(m.getControllerValue(), std::memory_order_relaxed);
                ccSerial[num].fetch_add(1, std::memory_order_relaxed);
                maybeLearn(num, true);
            }
            continue;
        }
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (note >= 0 && note < 128)
            {
                noteOnSerial[note].fetch_add(1, std::memory_order_relaxed);
                maybeLearn(note, false);
            }
        }
    }
}

void Engine::maybeLearn(int triggerNumber, bool isCc) noexcept
{
    auto& bindings = isCc ? ccToScene : noteToScene;

    const int learnTarget = armedLearnScene.exchange(-1, std::memory_order_relaxed);
    if (learnTarget >= 0)
    {
        bindings[triggerNumber].store(learnTarget, std::memory_order_relaxed);
        pendingSceneIndex.store(learnTarget, std::memory_order_relaxed); // preview the switch immediately
        return;
    }

    const int bound = bindings[triggerNumber].load(std::memory_order_relaxed);
    if (bound >= 0)
        pendingSceneIndex.store(bound, std::memory_order_relaxed);
}

void Engine::setTransportInfo(bool isPlaying, double bpm, double ppqPosition) noexcept
{
    transportPlaying.store(isPlaying, std::memory_order_relaxed);
    transportBpm.store(bpm, std::memory_order_relaxed);
    transportPpq.store(ppqPosition, std::memory_order_relaxed);
}

void Engine::armMidiLearn(int sceneIndex) { armedLearnScene.store(sceneIndex, std::memory_order_relaxed); }

void Engine::clearBinding(int sceneIndex)
{
    for (int i = 0; i < 128; ++i)
    {
        if (ccToScene[i].load(std::memory_order_relaxed) == sceneIndex) ccToScene[i].store(-1, std::memory_order_relaxed);
        if (noteToScene[i].load(std::memory_order_relaxed) == sceneIndex) noteToScene[i].store(-1, std::memory_order_relaxed);
    }
}

int Engine::consumePendingSceneIndex() noexcept { return pendingSceneIndex.exchange(-1, std::memory_order_relaxed); }

void Engine::analyseBlock()
{
    const auto total = totalWritten.load(std::memory_order_acquire);
    if (ring == nullptr || total < fftSize) return;

    std::vector<float> win((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        win[(size_t) i] = ring[(size_t) ((total - fftSize + i) % ringSize)];

    // Hann-windowed magnitude spectrum (same manual-window idiom as RiffHouseEngine's chroma pass).
    std::vector<float> fftBuf((size_t) fftSize * 2, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftBuf[(size_t) i] = win[(size_t) i] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / fftSize));

    fft.performFrequencyOnlyForwardTransform(fftBuf.data());

    const double nyquist = sampleRate * 0.5;
    const double loHz = 40.0;
    std::array<float, numBands> newBands {};
    for (int b = 1; b < fftSize / 2; ++b)
    {
        const double f = b * sampleRate / fftSize;
        if (f < loHz || f > nyquist) continue;
        const double frac = std::log(f / loHz) / std::log(nyquist / loHz);
        int band = (int) (frac * numBands);
        band = juce::jlimit(0, numBands - 1, band);
        newBands[(size_t) band] += fftBuf[(size_t) b] * fftBuf[(size_t) b];
    }
    for (auto& v : newBands) v = std::sqrt(v);

    std::array<float, scopeSize> newScope {};
    for (int i = 0; i < scopeSize; ++i)
        newScope[(size_t) i] = ring[(size_t) ((total - scopeSize + i) % ringSize)];

    std::lock_guard<std::mutex> l(bandsLock);
    for (int b = 0; b < numBands; ++b)
    {
        // Slow-decaying per-band running max as a simple auto-gain, so quiet
        // passages still read as motion instead of a flat line.
        bandRunningMax[(size_t) b] = juce::jmax(newBands[(size_t) b], bandRunningMax[(size_t) b] * 0.999f);
        bands[(size_t) b] = bandRunningMax[(size_t) b] > 1.0e-6f ? juce::jlimit(0.0f, 1.0f, newBands[(size_t) b] / bandRunningMax[(size_t) b]) : 0.0f;
    }
    scope = newScope;
}

void Engine::run()
{
    while (!threadShouldExit())
    {
        wait(16); // ~60 Hz cap; the editor's own timer decides how often getLiveState() is actually read
        analyseBlock();
    }
}

juce::var Engine::getLiveState()
{
    auto* o = new juce::DynamicObject();

    {
        std::lock_guard<std::mutex> l(bandsLock);
        juce::Array<juce::var> b;
        for (auto v : bands) b.add(v);
        o->setProperty("bands", b);
        juce::Array<juce::var> s;
        for (auto v : scope) s.add(v);
        o->setProperty("scope", s);
    }

    o->setProperty("rms", liveRms.load());
    o->setProperty("beatSerial", (int) beatSerial.load());
    o->setProperty("bpm", transportBpm.load());
    o->setProperty("playing", transportPlaying.load());

    juce::Array<juce::var> cc;
    for (int i = 0; i < 128; ++i)
        if (const auto ser = ccSerial[i].load()) cc.add(juce::Array<juce::var> { i, ccValue[i].load(), (int) ser });
    o->setProperty("cc", cc);

    juce::Array<juce::var> ccBindings, noteBindings;
    for (int i = 0; i < 128; ++i)
    {
        const int sc = ccToScene[i].load();
        if (sc >= 0) ccBindings.add(juce::Array<juce::var> { i, sc });
        const int sn = noteToScene[i].load();
        if (sn >= 0) noteBindings.add(juce::Array<juce::var> { i, sn });
    }
    auto* bindings = new juce::DynamicObject();
    bindings->setProperty("cc", ccBindings);
    bindings->setProperty("note", noteBindings);
    o->setProperty("bindings", juce::var(bindings));
    o->setProperty("learnArmed", armedLearnScene.load());

    return juce::var(o);
}

juce::var Engine::getMidiLearnState() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> ccBindings, noteBindings;
    for (int i = 0; i < 128; ++i)
    {
        const int sc = ccToScene[i].load();
        if (sc >= 0) ccBindings.add(juce::Array<juce::var> { i, sc });
        const int sn = noteToScene[i].load();
        if (sn >= 0) noteBindings.add(juce::Array<juce::var> { i, sn });
    }
    o->setProperty("cc", ccBindings);
    o->setProperty("note", noteBindings);
    return juce::var(o);
}

void Engine::setMidiLearnState(const juce::var& v)
{
    for (int i = 0; i < 128; ++i) { ccToScene[i] = -1; noteToScene[i] = -1; }
    if (!v.isObject()) return;

    if (auto* ccArr = v.getProperty("cc", {}).getArray())
        for (auto& pair : *ccArr)
            if (auto* p = pair.getArray(); p != nullptr && p->size() == 2)
            {
                const int num = (int) p->getReference(0), scene = (int) p->getReference(1);
                if (num >= 0 && num < 128) ccToScene[num] = scene;
            }

    if (auto* noteArr = v.getProperty("note", {}).getArray())
        for (auto& pair : *noteArr)
            if (auto* p = pair.getArray(); p != nullptr && p->size() == 2)
            {
                const int num = (int) p->getReference(0), scene = (int) p->getReference(1);
                if (num >= 0 && num < 128) noteToScene[num] = scene;
            }
}

}
