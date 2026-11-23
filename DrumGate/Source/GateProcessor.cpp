#include <thread>
#include "GateProcessor.h"
#include "GateEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
        { "Kick - Replace (Synth Kick)", { { threshold, -22.f }, { hysteresis, 8.f }, { hold, 30.f }, { release, 80.f },
                            { keyHpf, 35.f }, { keyLpf, 1200.f }, { detect, 2.f }, { sens, 8.f }, { retrigger, 50.f },
                            { lookahead, 2.f }, { replaceOn, 1.f }, { kit, 1.f }, { originalLevel, -6.f }, { sampleDyn, 60.f } } },
        { "Kick - Reinforce in a Mix", { { threshold, -17.f }, { hysteresis, 8.f }, { hold, 30.f }, { release, 80.f },
                            { range, 0.f }, { keyHpf, 30.f }, { keyLpf, 120.f }, { detect, 2.f }, { sens, 8.f },
                            { retrigger, 120.f }, { lookahead, 10.f }, { replaceOn, 1.f }, { kit, 1.f }, { sampleLevel, -4.f },
                            { sampleDyn, 60.f }, { sampleOffset, -3.2f } } },
        { "Snare - Reinforce (Synth Snare)", { { threshold, -20.f }, { hysteresis, 8.f }, { hold, 35.f }, { release, 160.f },
                            { range, -40.f }, { keyHpf, 180.f }, { keyLpf, 7000.f }, { detect, 2.f }, { sens, 8.f },
                            { retrigger, 25.f }, { lookahead, 2.f }, { replaceOn, 1.f }, { kit, 2.f }, { sampleLevel, -6.f },
                            { sampleDyn, 70.f } } },
        { "Overheads - Align Only", { { keySource, 3.f }, { alignOn, 1.f }, { alignRef, 1.f } } },
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
    l.add(std::make_unique<APF>(pid(keyLpf), "Key LPF", skewed(50.f, 20000.f, 2000.f, 1.f), 12000.f, withLabel("Hz", hzText)));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(detect), "Detect", juce::StringArray { "Peak", "RMS", "Transient" }, 0));
    l.add(std::make_unique<APF>(pid(sens), "Transient Sens", NR(0.f, 24.f, 0.1f), 6.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(retrigger), "Retrigger Guard", skewed(0.f, 250.f, 40.f, 0.1f), 0.f, withLabel("ms", msText)));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(mode), "Mode", juce::StringArray { "Gate", "Duck" }, 0));
    l.add(std::make_unique<APF>(pid(punch), "Punch", NR(0.f, 12.f, 0.1f), 0.f, withLabel("dB")));
    l.add(std::make_unique<APF>(pid(mix), "Mix", NR(0.f, 100.f, 0.1f), 100.f, withLabel("%")));
    l.add(std::make_unique<APF>(pid(output), "Output", NR(-24.f, 24.f, 0.1f), 0.f, withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(keyListen), "Key Listen", false));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(keySource), "Key Source", juce::StringArray { "Internal", "Sidechain", "MIDI", "Align Only" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(alignOn), "Align", false));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(alignRef), "Align Reference", false));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(alignGroup), "Align Group", juce::StringArray { "A", "B", "C", "D" }, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(alignPhase), "Align Phase", false));

    auto dbOrOff = [](float v, int) { return v <= -59.9f ? juce::String("-inf") : juce::String(v, 1); };
    l.add(std::make_unique<juce::AudioParameterBool>(pid(replaceOn), "Replace", false));
    l.add(std::make_unique<juce::AudioParameterChoice>(pid(kit), "Kit",
              juce::StringArray { "User Samples", "Synth Kick", "Synth Snare", "Synth Tom", "Synth Clap" }, 1));
    l.add(std::make_unique<APF>(pid(sampleLevel), "Sample Level", NR(-60.f, 12.f, 0.1f), 0.f, withLabel("dB", dbOrOff)));
    l.add(std::make_unique<APF>(pid(originalLevel), "Original Level", NR(-60.f, 6.f, 0.1f), 0.f, withLabel("dB", dbOrOff)));
    l.add(std::make_unique<APF>(pid(sampleDyn), "Sample Dynamics", NR(0.f, 100.f, 1.f), 70.f, withLabel("%")));
    l.add(std::make_unique<APF>(pid(samplePitch), "Sample Pitch", NR(-12.f, 12.f, 0.1f), 0.f, withLabel("st")));
    l.add(std::make_unique<APF>(pid(sampleLength), "Sample Length", NR(5.f, 100.f, 1.f), 100.f, withLabel("%")));
    l.add(std::make_unique<APF>(pid(sampleOffset), "Sample Offset", NR(-10.f, 10.f, 0.01f), 0.f, withLabel("ms", msText)));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(sampleChoke), "Choke", true));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(autoGain), "Auto Gain", false));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(bypass), "Bypass", false));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(safety), "Safety Ceiling", true));
    l.add(std::make_unique<APF>(pid(ceiling), "Ceiling", NR(-12.f, 0.f, 0.1f), -0.3f, withLabel("dB")));
    l.add(std::make_unique<juce::AudioParameterBool>(pid(live), "Live Mode", false));
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
    pAlignOn = p(alignOn);      pAlignRef = p(alignRef);     pAlignGroup = p(alignGroup);
    pAlignPhase = p(alignPhase);
    pReplaceOn = p(replaceOn);  pKit = p(kit);               pSampleLevel = p(sampleLevel);
    pOriginalLevel = p(originalLevel); pSampleDyn = p(sampleDyn); pSamplePitch = p(samplePitch);
    pSampleLength = p(sampleLength); pSampleOffset = p(sampleOffset); pSampleChoke = p(sampleChoke);
    pAutoGain = p(autoGain);    pSafety = p(safety);         pCeiling = p(ceiling);
    pLive = p(live);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(GateParamIDs::bypass));

    for (auto make : { GHSSampler::Synth::kickKit, GHSSampler::Synth::snareKit, GHSSampler::Synth::tomKit, GHSSampler::Synth::clapKit })
        synthKits.push_back(std::make_unique<GHSSampler::Bank>(make()));

    alignRegistry->addMember(this);
}

GHSDrumGateProcessor::~GHSDrumGateProcessor()
{
    alignRegistry->removeMember(this);
    alignCapture.freeze();
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

    aligner.prepare(getMainBusNumInputChannels(), 2 * alignBaseSamples() + GHSAlign::sincTaps);
    sampler.prepare(sampleRate);
    const int chans = std::max(1, getMainBusNumInputChannels());
    dryLine.setSize(chans, int(sampleRate * 0.06) + samplesPerBlock + 16, false, true, true);
    dryLine.clear();
    dryWritePos = 0;
    dryBlock.setSize(chans, std::max(1, samplesPerBlock), false, true, true);
    bypassMix = bypassParam != nullptr && bypassParam->get() ? 1.0f : 0.0f;
    agInEnergy = agOutEnergy = 1e-9;
    agLoudDb = -120.0;
    agGain = 1.0f;
    appliedAlignVersion = -1;

    const int la = engine.latencySamples(readSettings()) + (pAlignOn->load() > 0.5f ? alignBaseSamples() : 0);
    setLatencySamples(la);
    reportedLatency = la;
    lastNoteSent = -1;
}

int GHSDrumGateProcessor::alignBaseSamples() const
{
    return int(std::lround(GHSAlign::maxAlignMs * currentSampleRate.load() / 1000.0)) + GHSAlign::Aligner::minDelay;
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
    s.gateBypass   = juce::roundToInt(pKeySource->load()) == 3;
    s.liveMode     = pLive->load() > 0.5f;
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

    // Latency = lookahead + the hit analysis window (+ the fixed alignment
    // base when Align is on). Hosts pick a change up on their next latency
    // query; most re-sync on transport restart.
    const bool alignOnNow = pAlignOn->load() > 0.5f;
    const int la = engine.latencySamples(settings) + (alignOnNow ? alignBaseSamples() : 0);
    if (la != reportedLatency)
    {
        setLatencySamples(la);
        reportedLatency = la;
    }

    auto mainBus = getBusBuffer(buffer, true, 0);
    const int mainChans = mainBus.getNumChannels();

    // ---- the input, delayed by exactly the plugin's latency: what bypass plays (no timing jump) ----
    {
        const int cap = dryLine.getNumSamples();
        if (dryBlock.getNumSamples() < n) dryBlock.setSize(dryBlock.getNumChannels(), n, false, false, true);
        const int chans = std::min(mainChans, dryLine.getNumChannels());
        const int delay = juce::jlimit(0, cap - n - 1, la);
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* in = mainBus.getReadPointer(ch);
            float* line = dryLine.getWritePointer(ch);
            float* out = dryBlock.getWritePointer(ch);
            int w = dryWritePos;
            for (int i = 0; i < n; ++i)
            {
                line[w] = in[i];
                int r = w - delay;
                if (r < 0) r += cap;
                out[i] = line[r];
                if (++w == cap) w = 0;
            }
        }
        dryWritePos = (dryWritePos + n) % cap;
    }

    // ---- alignment capture: this track's raw input, stamped with the host timeline ----
    if (alignCapture.state.load(std::memory_order_relaxed) != GHSAlign::CaptureBuffer::Idle)
    {
        bool playing = false;
        std::optional<juce::int64> time;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (const auto t = pos->getTimeInSamples()) time = *t;
            }
        alignCapture.write(mainBus.getArrayOfReadPointers(), mainChans, n, playing, time);
    }

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
    const bool midiOut = pMidiOn->load() > 0.5f && ! (bypassParam != nullptr && bypassParam->get());
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
        if (e.isOpen && numPendingTriggers < int(pendingTriggers.size()))
            pendingTriggers[size_t(numPendingTriggers++)] = { e.sampleOffset, 0.05f + 0.95f * e.strength, e.onsetBack };
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

    numPendingTriggers = 0;
    engine.process(mainBus.getArrayOfWritePointers(), mainChans, key, n,
                   midiTriggers.data(), numMidiTriggers, settings, perSample, onTrigger);

    // ---- drum replacement: original level, then the samples on every accepted hit ----
    {
        const float orig = pOriginalLevel->load();
        const bool replace = pReplaceOn->load() > 0.5f && ! settings.gateBypass;
        if (replace && orig < 0.0f)
            mainBus.applyGain(orig <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain(orig));

        // pick the kit (the message thread may be swapping the user kit: see publishUserBank)
        audioPickingBank.store(true);
        const int kitIndex = juce::roundToInt(pKit->load());
        const GHSSampler::Bank* want = kitIndex == 0 ? userBankWanted.load()
                                                     : synthKits[size_t(juce::jlimit(1, int(synthKits.size()), kitIndex) - 1)].get();
        if (want != sampler.getBank()) sampler.setBank(want);
        userBankInUse.store(kitIndex == 0 ? want : nullptr);
        audioPickingBank.store(false);

        if (replace)
        {
            GHSSampler::Params sp;
            const float lvl = pSampleLevel->load();
            sp.levelDb = lvl <= -59.9f ? -200.0f : lvl;
            sp.dynamics = pSampleDyn->load() / 100.0f;
            sp.pitchSemis = pSamplePitch->load();
            sp.length = pSampleLength->load() / 100.0f;
            sp.choke = pSampleChoke->load() > 0.5f;

            // The audio is delayed by lookahead + window, so a transient that began
            // `onsetBack` samples before the hit was decided reaches the output
            // (lookahead + window - onsetBack) samples after the decision. Start the
            // sample exactly there, plus the user's offset. If the transient began
            // earlier than the lookahead can cover, the sample starts as early as
            // it can (raise Lookahead to fix that).
            const int ahead = engine.lookaheadSamples(settings) + engine.windowFor(settings);
            const int offset = juce::roundToInt(pSampleOffset->load() * currentSampleRate.load() / 1000.0);
            for (int t = 0; t < numPendingTriggers; ++t)
            {
                const auto& pt = pendingTriggers[size_t(t)];
                sampler.trigger(std::max(0, pt.offset + ahead - pt.onsetBack + offset), pt.velocity, sp);
                lastHitLayer = sampler.lastHit.layer;
                lastHitVelocity = sampler.lastHit.velocity;
                hitCounter.fetch_add(1);
            }
            sampler.render(mainBus.getArrayOfWritePointers(), mainChans, n);
        }
        else if (sampler.activeVoices() > 0)
            sampler.reset();
    }

    // ---- alignment: fixed base delay minus this mic's lag, then polarity / phase ----
    if (alignOnNow)
    {
        const int version = rtAlignVersion.load();
        if (version != appliedAlignVersion || ! appliedAlignOn)
        {
            const double sr = currentSampleRate.load();
            const bool valid = rtAlignValid.load();
            const double lag = valid ? rtAlignOffsetSec.load() * sr : 0.0;
            aligner.set(double(alignBaseSamples()) - lag, valid ? rtAlignPhase.load() : 0.0, valid && rtAlignUsePhase.load());
            if (! appliedAlignOn) aligner.reset();
            appliedAlignVersion = version;
        }
        aligner.process(mainBus.getArrayOfWritePointers(), mainChans, n);
    }
    appliedAlignOn = alignOnNow;

    const bool bypassed = bypassParam != nullptr && bypassParam->get();

    // ---- auto gain: match the processed level to the input's, measured on the loud parts ----
    {
        double eIn = 0.0, eOut = 0.0;
        const int chans = std::min(mainChans, dryBlock.getNumChannels());
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = dryBlock.getReadPointer(ch);
            const float* w = mainBus.getReadPointer(ch);
            for (int i = 0; i < n; ++i) { eIn += double(d[i]) * d[i]; eOut += double(w[i]) * w[i]; }
        }
        const double blockDb = 10.0 * std::log10(eIn / double(std::max(1, n * chans)) + 1e-20);
        const double sr = currentSampleRate.load();
        agLoudDb = std::max(blockDb, agLoudDb - 1.0 * double(n) / sr); // tracks the loud level, falls 1 dB/s
        float target = 1.0f;
        if (pAutoGain->load() > 0.5f && ! bypassed)
        {
            if (blockDb > agLoudDb - 20.0 && blockDb > -80.0) // only the loud parts (the hits), not the gaps
            {
                const double a = 1.0 - std::exp(-double(n) / (sr * 3.0));
                agInEnergy += a * (eIn - agInEnergy);
                agOutEnergy += a * (eOut - agOutEnergy);
            }
            target = float(juce::jlimit(0.125, 8.0, std::sqrt(agInEnergy / std::max(agOutEnergy, 1e-20))));
        }
        const float next = agGain + (target - agGain) * float(1.0 - std::exp(-double(n) / (sr * 0.25)));
        if (std::abs(next - 1.0f) > 1e-6f || std::abs(agGain - 1.0f) > 1e-6f)
            for (int ch = 0; ch < mainChans; ++ch)
                mainBus.applyGainRamp(ch, 0, n, agGain, next);
        agGain = next;
        autoGainDb = juce::Decibels::gainToDecibels(agGain);
    }

    // ---- safety ceiling: in a live rig the converter clips at 0 dBFS no matter what ----
    if (pSafety->load() > 0.5f)
    {
        const float ceil = juce::Decibels::decibelsToGain(pCeiling->load());
        bool touched = false;
        for (int ch = 0; ch < mainChans; ++ch)
        {
            float* d = mainBus.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs(d[i]);
                if (a > 0.7f * ceil)
                {
                    touched |= a > ceil;
                    d[i] = juce::jlimit(-ceil, ceil, GHSGate::Engine::softClip(d[i], ceil));
                }
            }
        }
        if (touched) safetyHold = int(currentSampleRate.load() * 0.5);
        else safetyHold = std::max(0, safetyHold - n);
        safetyActive = safetyHold > 0;
    }
    else
        safetyActive = false;

    // ---- bypass: crossfade (10 ms) to the latency-matched dry input ----
    {
        const float target = bypassed ? 1.0f : 0.0f;
        if (std::abs(bypassMix - target) > 1e-6f || bypassed)
        {
            const float step = float(n) / float(currentSampleRate.load() * 0.010);
            const float next = target > bypassMix ? std::min(target, bypassMix + step) : std::max(target, bypassMix - step);
            const int chans = std::min(mainChans, dryBlock.getNumChannels());
            for (int ch = 0; ch < chans; ++ch)
            {
                float* w = mainBus.getWritePointer(ch);
                const float* d = dryBlock.getReadPointer(ch);
                for (int i = 0; i < n; ++i)
                {
                    const float m = bypassMix + (next - bypassMix) * float(i) / float(n);
                    w[i] = w[i] * (1.0f - m) + d[i] * m;
                }
            }
            bypassMix = next;
        }
    }
    if (bypassed)
    {
        // MIDI: pass the input through untouched, add no triggers of our own
        if (lastNoteSent >= 0) { midi.addEvent(juce::MidiMessage::noteOff(1, lastNoteSent), n - 1); lastNoteSent = -1; }
    }

    {
        float pk = 0.0f;
        for (int ch = 0; ch < mainChans; ++ch) pk = std::max(pk, mainBus.getMagnitude(ch, 0, n));
        outputPeakDb = juce::Decibels::gainToDecibels(pk, -120.0f);
    }

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
    {
        const juce::ScopedLock sl(alignLock);
        state.setProperty("alignName", userAlignName, nullptr);
        state.setProperty("userSamples", userSamplePaths.joinIntoString("\n"), nullptr);
        if (alignment.valid)
            state.setProperty("alignment", juce::String(alignment.offsetSeconds * 1.0e6, 3) + " " + juce::String(alignment.phaseRad, 6)
                                               + " " + (alignment.usePhase ? "1" : "0"), nullptr);
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
            {
                GHSAlign::MemberAlignment al;
                auto parts = juce::StringArray::fromTokens(tree.getProperty("alignment").toString(), " ", "");
                parts.removeEmptyStrings();
                if (parts.size() == 3)
                {
                    al.valid = true;
                    al.offsetSeconds = parts[0].getDoubleValue() / 1.0e6;
                    al.phaseRad = parts[1].getDoubleValue();
                    al.usePhase = parts[2] == "1";
                }
                {
                    const juce::ScopedLock sl(alignLock);
                    userAlignName = tree.getProperty("alignName").toString();
                }
                setAlignmentInternal(al);
            }
            {
                juce::Array<juce::File> files;
                for (auto& path : juce::StringArray::fromLines(tree.getProperty("userSamples").toString()))
                    if (path.isNotEmpty()) files.add(juce::File(path));
                if (! files.isEmpty()) loadUserSamples(files);
            }
            tree.removeProperty("userSamples", nullptr);
            tree.removeProperty("alignment", nullptr);
            tree.removeProperty("alignName", nullptr);
            tree.removeProperty("hitProfile", nullptr);
            tree.removeProperty("spillProfile", nullptr);
            apvts.replaceState(tree);
        }
}

// ---- Drum replacement ----

juce::String GHSDrumGateProcessor::loadUserSamples(const juce::Array<juce::File>& files)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    auto bank = std::make_unique<GHSSampler::Bank>();
    bank->name = "User Samples";
    juce::StringArray paths, problems;

    // a folder counts as all the audio files in it
    juce::Array<juce::File> all;
    for (auto& f : files)
    {
        if (f.isDirectory())
            for (auto& c : f.findChildFiles(juce::File::findFiles, false, fm.getWildcardForAllFormats()))
                all.add(c);
        else
            all.add(f);
    }

    for (auto& f : all)
    {
        if (bank->samples.size() >= 64) { problems.add("only the first 64 files were loaded"); break; }
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
        if (reader == nullptr) { problems.add(f.getFileName() + " (not a readable audio file)"); continue; }
        const auto len = (int) std::min<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * 10.0));
        const int chans = (int) std::min<unsigned int>(2, reader->numChannels);
        if (len <= 0 || chans <= 0) { problems.add(f.getFileName() + " (empty)"); continue; }
        juce::AudioBuffer<float> buf(chans, len);
        reader->read(&buf, 0, len, 0, true, chans > 1);
        GHSSampler::Sample smp;
        smp.name = f.getFileNameWithoutExtension().toStdString();
        smp.sampleRate = reader->sampleRate;
        smp.data.resize(size_t(chans));
        for (int ch = 0; ch < chans; ++ch)
            smp.data[size_t(ch)].assign(buf.getReadPointer(ch), buf.getReadPointer(ch) + len);
        smp.analyse();
        bank->samples.push_back(std::move(smp));
        paths.add(f.getFullPathName());
    }

    if (bank->samples.empty())
        return problems.isEmpty() ? juce::String("No audio files found") : "Couldn't load: " + problems.joinIntoString(", ");

    bank->build();
    userSamplePaths = paths;
    publishUserBank(std::move(bank));

    // switch the kit to User Samples
    if (auto* k = apvts.getParameter(GateParamIDs::kit))
        k->setValueNotifyingHost(k->convertTo0to1(0.0f));
    return problems.isEmpty() ? juce::String() : "Skipped: " + problems.joinIntoString(", ");
}

void GHSDrumGateProcessor::clearUserSamples()
{
    userSamplePaths.clear();
    publishUserBank(nullptr);
}

juce::StringArray GHSDrumGateProcessor::getUserSamplePaths() const { return userSamplePaths; }

void GHSDrumGateProcessor::publishUserBank(std::unique_ptr<GHSSampler::Bank> bank)
{
    const GHSSampler::Bank* fresh = bank.get();
    if (bank) userBanks.push_back(std::move(bank));
    userBankWanted.store(fresh);

    // Free kits the audio thread can no longer be using: wait out a block
    // that's in the middle of picking, then keep only the wanted one and
    // whichever it last acknowledged.
    while (audioPickingBank.load()) std::this_thread::yield();
    const auto* inUse = userBankInUse.load();
    userBanks.erase(std::remove_if(userBanks.begin(), userBanks.end(),
                                   [&](const std::unique_ptr<GHSSampler::Bank>& b) { return b.get() != fresh && b.get() != inUse; }),
                    userBanks.end());
}

juce::StringArray GHSDrumGateProcessor::describeCurrentKit() const
{
    juce::StringArray out;
    const int kitIndex = juce::roundToInt(pKit->load());
    const GHSSampler::Bank* b = kitIndex == 0 ? userBankWanted.load()
                                              : synthKits[size_t(juce::jlimit(1, int(synthKits.size()), kitIndex) - 1)].get();
    if (b == nullptr) return out;
    const int L = int(b->layers.size());
    for (int i = 0; i < L; ++i)
    {
        const auto& layer = b->layers[size_t(i)];
        juce::String line;
        line << "Layer " << (i + 1) << "  vel " << juce::roundToInt(127.0 * i / L) << "-" << juce::roundToInt(127.0 * (i + 1) / L) << "  ";
        juce::StringArray names;
        for (int idx : layer.samples) names.add(juce::String(b->samples[size_t(idx)].name));
        line << names.joinIntoString(", ");
        out.add(line);
    }
    return out;
}

// ---- Alignment ----

int GHSDrumGateProcessor::getAlignGroup() const { return juce::jlimit(0, GHSAlign::numGroups - 1, juce::roundToInt(pAlignGroup->load())); }
bool GHSDrumGateProcessor::isAlignReference() const { return pAlignRef->load() > 0.5f; }

juce::String GHSDrumGateProcessor::getAlignName() const
{
    const juce::ScopedLock sl(alignLock);
    if (userAlignName.isNotEmpty()) return userAlignName;
    if (hostTrackName.isNotEmpty()) return hostTrackName;
    return "Drum Gate " + juce::String(alignMemberId);
}

void GHSDrumGateProcessor::setUserAlignName(const juce::String& name)
{
    const juce::ScopedLock sl(alignLock);
    userAlignName = name.trim();
}

juce::String GHSDrumGateProcessor::getUserAlignName() const
{
    const juce::ScopedLock sl(alignLock);
    return userAlignName;
}

void GHSDrumGateProcessor::updateTrackProperties(const TrackProperties& props)
{
    const juce::ScopedLock sl(alignLock);
    hostTrackName = props.name;
}

GHSAlign::MemberAlignment GHSDrumGateProcessor::getAlignment() const
{
    const juce::ScopedLock sl(alignLock);
    return alignment;
}

void GHSDrumGateProcessor::setAlignmentInternal(const GHSAlign::MemberAlignment& al)
{
    {
        const juce::ScopedLock sl(alignLock);
        alignment = al;
    }
    rtAlignOffsetSec = al.offsetSeconds;
    rtAlignPhase = al.phaseRad;
    rtAlignUsePhase = al.usePhase;
    rtAlignValid = al.valid;
    rtAlignVersion.fetch_add(1);
}

void GHSDrumGateProcessor::applyAlignment(const GHSAlign::MemberAlignment& al)
{
    setAlignmentInternal(al);
    // A fresh analysis switches alignment on for every track it covers
    if (al.valid)
        if (auto* p = apvts.getParameter(GateParamIDs::alignOn); p != nullptr && p->getValue() < 0.5f)
            p->setValueNotifyingHost(1.0f);
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
