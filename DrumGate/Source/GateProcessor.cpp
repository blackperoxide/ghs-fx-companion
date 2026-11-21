#include "GateProcessor.h"
#include "GateEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using NR  = juce::NormalisableRange<float>;

NR skewed(float lo, float hi, float centre, float interval = 0.0f)
{
    NR r(lo, hi, interval);
    r.setSkewForCentre(centre);
    return r;
}

juce::AudioParameterFloatAttributes withLabel(const juce::String& unit,
                                              std::function<juce::String(float, int)> toText = nullptr)
{
    auto a = juce::AudioParameterFloatAttributes().withLabel(unit);
    if (toText) a = a.withStringFromValueFunction(std::move(toText));
    return a;
}

juce::String msText(float v, int)
{
    if (v < 1.0f)    return juce::String(v, 2);
    if (v < 100.0f)  return juce::String(v, 1);
    return juce::String(juce::roundToInt(v));
}

juce::String hzText(float v, int)
{
    return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + "k" : juce::String(juce::roundToInt(v));
}

struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // real (denormalised) values
};

const std::vector<Preset>& factoryPresets()
{
    using namespace GateParamIDs;
    static const std::vector<Preset> presets {
        { "Init", {} },
        { "Kick - Tight", { { threshold, -20.f }, { hysteresis, 6.f }, { attack, 0.1f }, { hold, 60.f },
                            { release, 120.f }, { range, -80.f }, { lookahead, 1.5f }, { keyHpf, 35.f },
                            { keyLpf, 1500.f }, { detect, 0.f }, { retrigger, 30.f }, { punch, 2.f }, { level, 40.f }, { levelTarget, -8.f } } },
        { "Snare - Bleed Killer", { { threshold, -18.f }, { hysteresis, 8.f }, { attack, 0.2f }, { hold, 40.f },
                            { release, 180.f }, { range, -40.f }, { lookahead, 1.0f }, { keyHpf, 180.f },
                            { keyLpf, 7000.f }, { detect, 2.f }, { sens, 8.f }, { retrigger, 25.f }, { punch, 1.5f } } },
        { "Toms - Natural Ring", { { threshold, -26.f }, { hysteresis, 10.f }, { attack, 0.5f }, { hold, 150.f },
                            { release, 450.f }, { range, -30.f }, { lookahead, 2.0f }, { keyHpf, 60.f },
                            { keyLpf, 3000.f }, { detect, 2.f }, { sens, 6.f }, { retrigger, 40.f },
                            { bandSplit, 1.f }, { range, -24.f }, { rangeHigh, -60.f }, { crossover, 900.f },
                            { adaptive, 70.f } } },
        { "Hi-Hat - Clean", { { threshold, -30.f }, { hysteresis, 4.f }, { attack, 0.05f }, { hold, 10.f },
                            { release, 60.f }, { range, -18.f }, { lookahead, 0.5f }, { keyHpf, 4000.f },
                            { keyLpf, 18000.f }, { detect, 0.f } } },
        { "Snare - Two-Band Natural", { { threshold, -22.f }, { hysteresis, 8.f }, { attack, 0.2f }, { hold, 30.f },
                            { release, 220.f }, { lookahead, 1.0f }, { keyHpf, 150.f }, { keyLpf, 8000.f },
                            { detect, 2.f }, { sens, 6.f }, { retrigger, 25.f }, { bandSplit, 1.f }, { range, -12.f },
                            { rangeHigh, -50.f }, { crossover, 2500.f }, { adaptive, 60.f } } },
        { "Snare - Punch & Clip", { { threshold, -20.f }, { hysteresis, 8.f }, { attack, 0.1f }, { hold, 35.f },
                            { release, 160.f }, { range, -40.f }, { keyHpf, 180.f }, { keyLpf, 7000.f }, { detect, 2.f },
                            { sens, 8.f }, { retrigger, 25.f }, { level, 60.f }, { levelTarget, -10.f }, { clip, 4.f } } },
        { "Room - Duck on Kick", { { mode, 1.f }, { keySource, 1.f }, { threshold, -24.f }, { hysteresis, 6.f },
                            { attack, 1.0f }, { hold, 30.f }, { release, 250.f }, { range, -12.f },
                            { keyHpf, 30.f }, { keyLpf, 800.f } } },
        { "Kick - MIDI Trigger", { { threshold, -22.f }, { hysteresis, 8.f }, { hold, 30.f }, { release, 80.f },
                            { keyHpf, 35.f }, { keyLpf, 1200.f }, { detect, 2.f }, { sens, 8.f },
                            { retrigger, 50.f }, { midiOn, 1.f }, { midiNote, 36.f }, { midiVelSens, 80.f } } },
    };
    return presets;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GHSDrumGateProcessor::createLayout()
{
    using namespace GateParamIDs;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [](const char* id) { return juce::ParameterID { id, 1 }; };

    l.add(std::make_unique<APF>(pid(threshold), "Threshold", NR(-80.f, 0.f, 0.1f), -24.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(hysteresis), "Hysteresis", NR(0.f, 24.f, 0.1f), 6.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(attack), "Attack", skewed(0.01f, 50.f, 2.f, 0.01f), 0.5f, withLabel("ms", msText)));
    l.add(std::make_unique<APF>(pid(hold), "Hold", skewed(0.f, 1000.f, 80.f, 0.1f), 50.f, withLabel("ms", msText)));
    l.add(std::make_unique<APF>(pid(release), "Release", skewed(1.f, 3000.f, 200.f, 0.1f), 150.f, withLabel("ms", msText)));
    l.add(std::make_unique<APF>(pid(range), "Range", NR(-80.f, 0.f, 0.1f), -60.f,
                                withLabel("dB", [](float v, int) { return v <= -79.9f ? juce::String("-inf") : juce::String(v, 1); })));
    l.add(std::make_unique<APF>(pid(lookahead), "Lookahead", NR(0.f, GHSGate::Engine::maxLookaheadMs, 0.01f), 1.f, withLabel("ms", msText)));
    l.add(std::make_unique<APF>(pid(keyHpf), "Key HPF", skewed(20.f, 8000.f, 300.f, 1.f), 60.f, withLabel("Hz", hzText)));
    l.add(std::make_unique<APF>(pid(keyLpf), "Key LPF", skewed(200.f, 20000.f, 3000.f, 1.f), 12000.f, withLabel("Hz", hzText)));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(detect), "Detect", juce::StringArray { "Peak", "RMS", "Transient" }, 0));
    l.add(std::make_unique<APF>(pid(sens), "Transient Sens", NR(0.f, 24.f, 0.1f), 6.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(retrigger), "Retrigger Guard", skewed(0.f, 250.f, 40.f, 0.1f), 0.f, withLabel("ms", msText)));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(mode), "Mode", juce::StringArray { "Gate", "Duck" }, 0));
    l.add(std::make_unique<APF>(pid(punch), "Punch", NR(0.f, 12.f, 0.1f), 0.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(mix), "Mix", NR(0.f, 100.f, 0.1f), 100.f, withLabel("%")));
    l.add(std::make_unique<APF>(pid(output), "Output", NR(-24.f, 24.f, 0.1f), 0.f, withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(keyListen), "Key Listen", false));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(keySource), "Key Source", juce::StringArray { "Internal", "Sidechain", "MIDI" }, 0));
    l.add(std::make_unique<juce::AudioParameterInt>(pid(midiInNote), "Trigger Note In", -1, 127, -1, juce::AudioParameterIntAttributes()
              .withStringFromValueFunction([](int v, int) { return v < 0 ? juce::String("Any") : juce::MidiMessage::getMidiNoteName(v, true, true, 3); })));
    l.add(std::make_unique<APF>(pid(adaptive), "Adaptive Decay", NR(0.f, 100.f, 1.f), 0.f, withLabel("%")));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(bandSplit), "Two-Band", false));
    l.add(std::make_unique<APF>(pid(rangeHigh), "High Range", NR(-80.f, 0.f, 0.1f), -80.f,
                                withLabel("dB", [](float v, int) { return v <= -79.9f ? juce::String("-inf") : juce::String(v, 1); })));
    l.add(std::make_unique<APF>(pid(crossover), "Crossover", skewed(150.f, 8000.f, 1200.f, 1.f), 1500.f, withLabel("Hz", hzText)));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(hitMatch), "Hit Match", false));
    l.add(std::make_unique<APF>(pid(matchTol), "Match Tolerance", NR(1.f, 12.f, 0.1f), 4.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(level), "Leveller", NR(0.f, 100.f, 1.f), 0.f, withLabel("%")));
    l.add(std::make_unique<APF>(pid(levelTarget), "Level Target", NR(-36.f, 0.f, 0.1f), -12.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(clip), "Hit Clip", NR(0.f, 12.f, 0.1f), 0.f, withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(midiOn), "MIDI Trigger", false));
    l.add(std::make_unique<juce::AudioParameterInt>(pid(midiNote), "MIDI Note", 0, 127, 36));
    l.add(std::make_unique<APF>(pid(midiVelSens), "Velocity Sens", NR(0.f, 100.f, 1.f), 70.f, withLabel("%")));
    return l;
}

GHSDrumGateProcessor::GHSDrumGateProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "GHSDrumGate", createLayout())
{
    using namespace GateParamIDs;
    auto p = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pThreshold = p(threshold);  pHysteresis = p(hysteresis); pAttack = p(attack);
    pHold = p(hold);            pRelease = p(release);       pRange = p(range);
    pLookahead = p(lookahead);  pHpf = p(keyHpf);            pLpf = p(keyLpf);
    pDetect = p(detect);        pSens = p(sens);             pRetrigger = p(retrigger);
    pMode = p(mode);            pPunch = p(punch);           pMix = p(mix);
    pOutput = p(output);        pKeyListen = p(keyListen);   pKeySource = p(keySource);
    pMidiOn = p(midiOn);        pMidiNote = p(midiNote);     pMidiVelSens = p(midiVelSens);
    pAdaptive = p(adaptive);    pBandSplit = p(bandSplit);   pRangeHigh = p(rangeHigh);
    pCrossover = p(crossover);  pHitMatch = p(hitMatch);     pMatchTol = p(matchTol);
    pLevel = p(level);          pLevelTarget = p(levelTarget); pClip = p(clip);
    pMidiInNote = p(midiInNote);
}

bool GHSDrumGateProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void GHSDrumGateProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare(sampleRate, getMainBusNumInputChannels());
    keyBuffer.setSize(1, std::max(1, samplesPerBlock), false, true, true);

    const int la = engine.latencySamples(readSettings());
    setLatencySamples(la);
    reportedLatency = la;
    lastNoteSent = -1;
}

GHSGate::Settings GHSDrumGateProcessor::readSettings() const
{
    GHSGate::Settings s;
    s.thresholdDb  = pThreshold->load();
    s.hysteresisDb = pHysteresis->load();
    s.attackMs     = pAttack->load();
    s.holdMs       = pHold->load();
    s.releaseMs    = pRelease->load();
    s.rangeDb      = pRange->load();
    s.lookaheadMs  = pLookahead->load();
    s.keyHpfHz     = pHpf->load();
    s.keyLpfHz     = pLpf->load();
    s.detect       = static_cast<GHSGate::DetectMode>(juce::roundToInt(pDetect->load()));
    s.transientSensDb = pSens->load();
    s.retriggerMs  = pRetrigger->load();
    s.mode         = static_cast<GHSGate::GateMode>(juce::roundToInt(pMode->load()));
    s.punchDb      = pPunch->load();
    s.mix          = pMix->load() / 100.0f;
    s.outputDb     = pOutput->load();
    s.keyListen    = pKeyListen->load() > 0.5f;
    s.adaptiveDecay = pAdaptive->load() / 100.0f;
    s.bandSplit    = pBandSplit->load() > 0.5f;
    s.rangeHighDb  = pRangeHigh->load();
    s.crossoverHz  = pCrossover->load();
    s.hitMatch     = pHitMatch->load() > 0.5f;
    s.matchToleranceDb = pMatchTol->load();
    s.levelAmount  = pLevel->load() / 100.0f;
    s.levelTargetDb = pLevelTarget->load();
    s.clipDb       = pClip->load();
    s.learn        = static_cast<GHSGate::LearnMode>(juce::jlimit(0, 2, learnMode.load()));
    s.triggerFromMidi = juce::roundToInt(pKeySource->load()) == 2;
    return s;
}

void GHSDrumGateProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto settings = readSettings();

    // ---- learned profiles: pick up restored/cleared ones, publish learned ones ----
    if (const int clear = clearRequest.exchange(0); clear != 0)
    {
        if (clear & 1) engine.hitProfile.clear();
        if (clear & 2) engine.spillProfile.clear();
    }
    {
        const juce::SpinLock::ScopedTryLockType lock(profileLock);
        if (lock.isLocked() && profilesPendingForEngine)
        {
            engine.hitProfile = sharedHitProfile;
            engine.spillProfile = sharedSpillProfile;
            profilesPendingForEngine = false;
        }
    }

    // Latency = lookahead + the hit analysis window. Hosts pick a change up on
    // their next latency query; most re-sync on transport restart.
    const int la = engine.latencySamples(settings);
    if (la != reportedLatency)
    {
        setLatencySamples(la);
        reportedLatency = la;
    }

    auto mainBus = getBusBuffer(buffer, true, 0);
    const int mainChans = mainBus.getNumChannels();

    // ---- build the mono key ----
    if (keyBuffer.getNumSamples() < n)
        keyBuffer.setSize(1, n, false, false, true); // only if the host exceeds its promised block size
    auto* key = keyBuffer.getWritePointer(0);
    juce::FloatVectorOperations::clear(key, n);

    const bool wantSidechain = pKeySource->load() > 0.5f;
    bool usingSidechain = false;
    if (wantSidechain && juce::roundToInt(pKeySource->load()) == 1 && getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
    {
        auto sc = getBusBuffer(buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            usingSidechain = true;
            const float scale = 1.0f / float(sc.getNumChannels());
            for (int ch = 0; ch < sc.getNumChannels(); ++ch)
                juce::FloatVectorOperations::addWithMultiply(key, sc.getReadPointer(ch), scale, n);
        }
    }
    sidechainConnected = usingSidechain;

    if (! usingSidechain && mainChans > 0)
    {
        // Key from the input: use the loudest channel per sample rather than
        // the sum, so a hard-panned hit isn't 6 dB down at the detector.
        for (int ch = 0; ch < mainChans; ++ch)
        {
            const auto* src = mainBus.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                if (std::abs(src[i]) > std::abs(key[i])) key[i] = src[i];
        }
    }

    // ---- MIDI trigger input (Key Source = MIDI): note-ons open the gate ----
    int numMidiTriggers = 0;
    if (settings.triggerFromMidi)
    {
        const int wanted = juce::roundToInt(pMidiInNote->load());
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() && (wanted < 0 || m.getNoteNumber() == wanted) && numMidiTriggers < int(midiTriggers.size()))
                midiTriggers[size_t(numMidiTriggers++)] = { juce::jlimit(0, n - 1, meta.samplePosition), m.getFloatVelocity() };
        }
    }

    // ---- MIDI: pass input through, add our triggers ----
    const bool midiOut = pMidiOn->load() > 0.5f;
    const int note = juce::jlimit(0, 127, juce::roundToInt(pMidiNote->load()));
    const float velSens = pMidiVelSens->load() / 100.0f;

    if ((! midiOut || note != lastNoteSent) && lastNoteSent >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(1, lastNoteSent), 0);
        lastNoteSent = -1;
    }

    auto onTrigger = [&](const GHSGate::TriggerEvent& e)
    {
        if (e.isOpen) pending.hit = true;
        if (e.rejected) { pending.rejected = true; return; }
        if (! midiOut) return;

        if (e.isOpen)
        {
            if (lastNoteSent >= 0)
                midi.addEvent(juce::MidiMessage::noteOff(1, lastNoteSent), e.sampleOffset);
            const float v = (1.0f - velSens) * 1.0f + velSens * (0.05f + 0.95f * e.strength);
            const auto vel = (juce::uint8) juce::jlimit(1, 127, juce::roundToInt(v * 127.0f));
            midi.addEvent(juce::MidiMessage::noteOn(1, note, vel), e.sampleOffset);
            lastNoteSent = note;
        }
        else if (lastNoteSent >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, lastNoteSent), e.sampleOffset);
            lastNoteSent = -1;
        }
    };

    auto perSample = [&](int, float inAbs, float keyDb, float gain)
    {
        pending.inputPeak = std::max(pending.inputPeak, inAbs);
        pending.keyDb = std::max(pending.keyDb, keyDb);
        pending.minGain = std::min(pending.minGain, gain);

        if (++frameCounter >= samplesPerFrame)
        {
            frameCounter = 0;
            const auto scope = displayFifo.write(1);
            if (scope.blockSize1 > 0) displayBuffer[size_t(scope.startIndex1)] = pending;
            else if (scope.blockSize2 > 0) displayBuffer[size_t(scope.startIndex2)] = pending;
            pending = { 0.0f, -120.0f, 1.0f, false, false };
        }
    };

    engine.process(mainBus.getArrayOfWritePointers(), mainChans, key, n,
                   midiTriggers.data(), numMidiTriggers, settings, perSample, onTrigger);

    learnedHitCount = engine.hitProfile.count;
    learnedSpillCount = engine.spillProfile.count;
    lastMatchDistance = engine.lastMatchDistance;
    lastHitRejected = engine.lastHitRejected;
    {
        const juce::SpinLock::ScopedTryLockType lock(profileLock);
        if (lock.isLocked() && ! profilesPendingForEngine)
        {
            sharedHitProfile = engine.hitProfile;
            sharedSpillProfile = engine.spillProfile;
        }
    }

    meterGainDb = engine.currentGainDb();
    meterKeyDb = engine.currentKeyDb();
    meterOpen = engine.gateIsOpen();

    // Anything beyond the main output (shouldn't happen, but never leave garbage)
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, n);
}

int GHSDrumGateProcessor::popDisplayFrames(DisplayFrame* dest, int maxFrames)
{
    const auto scope = displayFifo.read(std::min(maxFrames, displayFifo.getNumReady()));
    int copied = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[copied++] = displayBuffer[size_t(scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[copied++] = displayBuffer[size_t(scope.startIndex2 + i)];
    return copied;
}

// ---- Programs ----
int GHSDrumGateProcessor::getNumPrograms() { return int(factoryPresets().size()); }

const juce::String GHSDrumGateProcessor::getProgramName(int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow(index, int(p.size())) ? juce::String(p[size_t(index)].name) : juce::String();
}

void GHSDrumGateProcessor::setCurrentProgram(int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow(index, int(presets.size()))) return;
    currentProgram = index;

    // Every preset starts from the defaults, then applies its own values.
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());

    for (const auto& [id, value] : presets[size_t(index)].values)
        if (auto* ranged = apvts.getParameter(id))
            ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
}

// ---- State ----
void GHSDrumGateProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram, nullptr);
    {
        const juce::SpinLock::ScopedLockType lock(profileLock);
        state.setProperty("hitProfile", profileToString(sharedHitProfile), nullptr);
        state.setProperty("spillProfile", profileToString(sharedSpillProfile), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void GHSDrumGateProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            currentProgram = tree.getProperty("program", 0);
            {
                const juce::SpinLock::ScopedLockType lock(profileLock);
                sharedHitProfile = profileFromString(tree.getProperty("hitProfile").toString());
                sharedSpillProfile = profileFromString(tree.getProperty("spillProfile").toString());
                profilesPendingForEngine = true;
            }
            tree.removeProperty("hitProfile", nullptr);
            tree.removeProperty("spillProfile", nullptr);
            apvts.replaceState(tree);
        }
}

juce::String GHSDrumGateProcessor::profileToString(const GHSGate::Profile& p)
{
    if (! p.valid()) return {};
    juce::StringArray parts;
    parts.add(juce::String(p.count));
    for (auto v : p.mean) parts.add(juce::String(v, 3));
    return parts.joinIntoString(" ");
}

GHSGate::Profile GHSDrumGateProcessor::profileFromString(const juce::String& text)
{
    GHSGate::Profile p;
    auto parts = juce::StringArray::fromTokens(text, " ", "");
    parts.removeEmptyStrings();
    if (parts.size() != 1 + GHSGate::numFingerprintBands) return p;
    p.count = parts[0].getIntValue();
    for (int i = 0; i < GHSGate::numFingerprintBands; ++i)
        p.mean[size_t(i)] = parts[i + 1].getFloatValue();
    return p;
}

juce::AudioProcessorEditor* GHSDrumGateProcessor::createEditor() { return new GHSDrumGateEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GHSDrumGateProcessor(); }
