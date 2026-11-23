#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/**
 * The gate engine, kept free of JUCE so it can be unit-tested on its own
 * (see DrumGate/Tests/GateDSPTest.cpp) and so the audio-thread code is easy
 * to read in one place.
 *
 * Signal flow per sample:
 *
 *   key (input, external sidechain, or MIDI notes)
 *     -> key HPF -> key LPF                 (focus the detector on the drum)
 *     -> detector (Peak / RMS / Transient)
 *     -> hit analysis window (3 ms)         measures each candidate hit's
 *                                            peak and timbre fingerprint
 *     -> Hit Match                          accept/reject by timbre against
 *                                            learned "hit" and "spill" profiles
 *     -> open/close state machine           threshold + hysteresis, hold,
 *                                            retrigger guard
 *     -> envelope                           linear attack, exponential release
 *                                            (adaptive: scales with hit strength),
 *                                            punch accent
 *
 *   main audio -> delay (lookahead + analysis window)
 *     -> optional 2-band split (LR4 crossover), each band gated to its own range
 *     -> per-hit leveller -> per-hit clipper -> dry/wet mix -> output trim
 *
 * The audio is delayed by (lookahead + analysis window) while detection runs
 * on the undelayed key, so the gate can make its decision on the first few
 * milliseconds of a hit and still open *before* that hit reaches the output.
 */
namespace GHSGate
{

inline float dbToGain(float db) { return db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g)  { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10(g); }

/** Plain RBJ biquad (direct form I). Coefficients computed here so nothing allocates on the audio thread. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = float(nb0 / na0); b1 = float(nb1 / na0); b2 = float(nb2 / na0);
        a1 = float(na1 / na0); a2 = float(na2 / na0);
    }

    static double omega(double sr, double freq) { return 2.0 * M_PI * std::clamp(freq, 10.0, sr * 0.45) / sr; }

    void setHighPass(double sr, double freq, double q = 0.7071)
    {
        const double w0 = omega(sr, freq), c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setLowPass(double sr, double freq, double q = 0.7071)
    {
        const double w0 = omega(sr, freq), c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setBandPass(double sr, double freq, double q)
    {
        const double w0 = omega(sr, freq), c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    float process(float x)
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

/** 4th-order Linkwitz-Riley crossover: low + high sum back to an allpass (flat magnitude). */
struct Crossover
{
    Biquad lp1, lp2, hp1, hp2;
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void setFrequency(double sr, double f)
    {
        lp1.setLowPass(sr, f); lp2.setLowPass(sr, f);
        hp1.setHighPass(sr, f); hp2.setHighPass(sr, f);
    }
    void process(float x, float& low, float& high)
    {
        low = lp2.process(lp1.process(x));
        high = hp2.process(hp1.process(x));
    }
};

/**
 * A hit's timbre: energy in a few bands across the drum spectrum over the
 * first milliseconds, normalised so loudness doesn't matter - only shape.
 * A snare and the kick spill on the snare mic can be the same level but have
 * very different fingerprints.
 */
static constexpr int numFingerprintBands = 7;
inline constexpr std::array<float, numFingerprintBands> fingerprintBandHz { 60.f, 130.f, 280.f, 600.f, 1300.f, 3000.f, 7500.f };
using Fingerprint = std::array<float, numFingerprintBands>; // dB, mean removed

inline float fingerprintDistance(const Fingerprint& a, const Fingerprint& b)
{
    float sum = 0.0f;
    for (int i = 0; i < numFingerprintBands; ++i) { const float d = a[size_t(i)] - b[size_t(i)]; sum += d * d; }
    return std::sqrt(sum / float(numFingerprintBands));
}

struct Profile
{
    Fingerprint mean {};
    int count = 0;
    bool valid() const { return count > 0; }
    void add(const Fingerprint& f)
    {
        ++count;
        const float w = 1.0f / float(std::min(count, 32)); // running mean, then slowly adapting
        for (size_t i = 0; i < mean.size(); ++i) mean[i] += w * (f[i] - mean[i]);
    }
    void clear() { mean = {}; count = 0; }
};

enum class DetectMode { Peak = 0, RMS = 1, Transient = 2 };
enum class GateMode   { Gate = 0, Duck = 1 };
enum class LearnMode  { Off = 0, Hits = 1, Spill = 2 };

struct Settings
{
    float thresholdDb   = -24.0f;
    float hysteresisDb  = 6.0f;   // close threshold = threshold - hysteresis
    float attackMs      = 0.5f;
    float holdMs        = 50.0f;
    float releaseMs     = 150.0f;
    float adaptiveDecay = 0.0f;   // 0..1: harder hits get a longer release, softer hits a shorter one
    float rangeDb       = -60.0f; // how far the gate closes (-80 = fully shut); the low band when split
    bool  bandSplit     = false;  // two-band gating
    float rangeHighDb   = -80.0f; // high band range when split
    float crossoverHz   = 1500.0f;
    float lookaheadMs   = 1.0f;
    float keyHpfHz      = 60.0f;
    float keyLpfHz      = 12000.0f;
    DetectMode detect   = DetectMode::Peak;
    float transientSensDb = 6.0f; // Transient mode: how far the fast envelope must jump above the slow one
    float retriggerMs   = 0.0f;   // minimum time between gate openings (double-hit / flam guard)
    bool  hitMatch      = false;  // accept/reject hits by timbre against the learned profiles
    float matchToleranceDb = 4.0f;
    LearnMode learn     = LearnMode::Off;
    bool  triggerFromMidi = false; // gate opens on MIDI notes instead of audio detection
    bool  gateBypass    = false;  // "Align Only": no gating or hit dynamics, audio just passes (still delayed for alignment)
    GateMode mode       = GateMode::Gate;
    float punchDb       = 0.0f;   // extra gain right at the opening, decays quickly
    float levelAmount   = 0.0f;   // 0..1: pull every hit's peak toward levelTargetDb
    float levelTargetDb = -12.0f;
    float clipDb        = 0.0f;   // per-hit clipping depth below that hit's (levelled) peak
    float mix           = 1.0f;   // 0..1 dry/wet (parallel gating)
    float outputDb      = 0.0f;
    bool keyListen      = false;
};

/** Called for every gate opening/closing - used for MIDI output and the display's hit markers. */
struct TriggerEvent
{
    int sampleOffset;  // within the current block
    bool isOpen;       // true = gate opened (a hit), false = gate closed
    float strength;    // 0..1, how hard the hit was (for MIDI velocity)
    bool rejected = false; // a candidate hit that Hit Match turned away (isOpen is false; the gate never opened)
};

/** A MIDI note-on used as the trigger source. */
struct MidiTrigger
{
    int sampleOffset;
    float velocity; // 0..1
};

class Engine
{
public:
    static constexpr float maxLookaheadMs = 10.0f;
    static constexpr float analysisMs = 3.0f;

    void prepare(double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::max(1, numChannels);
        maxDelay = int(std::ceil(sr * (maxLookaheadMs + analysisMs) / 1000.0)) + 2;
        delay.assign(size_t(channels + 1), std::vector<float>(size_t(maxDelay), 0.0f)); // +1 = key channel (for key listen)
        crossovers.assign(size_t(channels), Crossover {});
        analysisSamples = std::max(1, int(std::lround(sr * analysisMs / 1000.0)));
        for (size_t b = 0; b < bands.size(); ++b)
            bands[b].setBandPass(sr, fingerprintBandHz[b], 1.4);
        writePos = 0;
        lastSettingsValid = false;
        reset();
    }

    void reset()
    {
        hpf.reset(); lpf.reset();
        for (auto& b : bands) b.reset();
        for (auto& x : crossovers) x.reset();
        envPeak = envRms = envFast = envSlow = 0.0f;
        state = State::Closed;
        holdCounter = 0;
        evalCounter = 0;
        midiCountdown = -1;
        samplesSinceOpen = 1 << 30;
        openness = 0.0f;
        punchEnv = 0.0f;
        levelGain = levelGainTarget = 1.0f;
        clipCeiling = 0.0f;
        currentHitPeak = 0.0f;
        for (auto& d : delay) std::fill(d.begin(), d.end(), 0.0f);
    }

    /** Total delay the host must compensate for at the given settings. */
    int latencySamples(const Settings& s) const
    {
        const int la = std::clamp(int(std::lround(sr * s.lookaheadMs / 1000.0)), 0, maxDelay - analysisSamples - 1);
        return la + analysisSamples;
    }

    bool gateIsOpen() const { return state == State::Open; }
    float currentGainDb() const { return lastGainDb; }
    float currentKeyDb() const { return lastKeyDb; }

    Profile hitProfile, spillProfile; // learned by the audio thread, read/restored by the owner between blocks
    Fingerprint lastFingerprint {};
    float lastMatchDistance = -1.0f;  // distance of the last analysed hit from the hit profile (-1 = n/a)
    bool lastHitRejected = false;

    /**
     * Processes in place.
     *  audio:     channel pointers for the main bus (numChannels as passed to prepare()).
     *  key:       mono key signal (already summed), numSamples long. Not modified.
     *  midi:      note-on triggers (used when settings.triggerFromMidi), sorted by offset.
     *  perSample: callback(sampleIndex, inputAbs, keyDb, gain) for the display.
     *  onTrigger: callback(TriggerEvent).
     */
    template <typename PerSample, typename OnTrigger>
    void process(float* const* audio, int numChannels, const float* key, int numSamples,
                 const MidiTrigger* midi, int numMidi,
                 const Settings& s, PerSample&& perSample, OnTrigger&& onTrigger)
    {
        updateCoefficients(s);

        const int totalDelay = latencySamples(s);
        const float openThr  = s.thresholdDb;
        const float closeThr = s.thresholdDb - std::max(0.0f, s.hysteresisDb);
        const float rangeLow  = s.rangeDb <= -79.9f ? 0.0f : dbToGain(s.rangeDb);
        const float rangeHigh = s.bandSplit ? (s.rangeHighDb <= -79.9f ? 0.0f : dbToGain(s.rangeHighDb)) : rangeLow;
        const float punchGain = dbToGain(std::max(0.0f, s.punchDb));
        const float outGain = dbToGain(s.outputDb);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f);
        const int holdSamples = int(sr * s.holdMs / 1000.0);
        const int guardSamples = int(sr * s.retriggerMs / 1000.0);
        const int chans = std::min(numChannels, channels);
        const bool gateMode = s.mode == GateMode::Gate || s.gateBypass;
        int midiIndex = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- key filter + detection (undelayed) ----
            const float raw = key[i];
            const float k = lpf.process(hpf.process(raw));
            const float ak = std::fabs(k);

            envPeak = ak > envPeak ? ak : envPeak * peakRelCoef;
            envRms  = envRms + rmsCoef * (k * k - envRms);
            envFast = ak > envFast ? envFast + fastAttCoef * (ak - envFast) : envFast * fastRelCoef;
            envSlow = ak > envSlow ? envSlow + slowAttCoef * (ak - envSlow) : envSlow * slowRelCoef;

            float levelDb;
            bool onsetCondition;
            switch (s.detect)
            {
                case DetectMode::RMS:
                    levelDb = gainToDb(std::sqrt(std::max(0.0f, envRms)) * 1.41421356f); // sine-calibrated so RMS reads like peak
                    onsetCondition = levelDb > openThr;
                    break;
                case DetectMode::Transient:
                {
                    levelDb = gainToDb(envFast);
                    // Only a *sudden* rise counts as a hit: rejects bleed and
                    // ringing that creeps up slowly past the threshold.
                    const float rise = levelDb - gainToDb(envSlow);
                    onsetCondition = levelDb > openThr && rise > s.transientSensDb;
                    break;
                }
                case DetectMode::Peak:
                default:
                    levelDb = gainToDb(envPeak);
                    onsetCondition = levelDb > openThr;
                    break;
            }
            lastKeyDb = levelDb;

            // fingerprint bands always run so their state is warm when a hit starts
            float bandOut[numFingerprintBands];
            for (size_t b = 0; b < bands.size(); ++b) bandOut[b] = bands[b].process(raw);

            // ---- state machine ----
            if (samplesSinceOpen < (1 << 30)) ++samplesSinceOpen;

            if (s.gateBypass)
            {
                if (state == State::Open) closeGate(i, onTrigger);
                state = State::Closed;
            }
            else if (s.triggerFromMidi)
            {
                while (midiIndex < numMidi && midi[midiIndex].sampleOffset <= i)
                {
                    // Delay by the analysis window so MIDI-triggered openings line
                    // up with the delayed audio exactly like detected ones do.
                    midiCountdown = analysisSamples;
                    midiVelocity = midi[midiIndex].velocity;
                    ++midiIndex;
                }
                if (midiCountdown >= 0 && midiCountdown-- == 0)
                    // no measured peak for a MIDI hit: leveller stays neutral
                    openGate(i, s, holdSamples, std::clamp(midiVelocity, 0.0f, 1.0f), dbToGain(s.levelTargetDb), onTrigger);
                else if (state == State::Open)
                {
                    if (holdCounter > 0) --holdCounter;
                    else closeGate(i, onTrigger);
                }
                if (state == State::Evaluating || state == State::Rejected) state = State::Closed;
            }
            else switch (state)
            {
                case State::Closed:
                    if (onsetCondition && samplesSinceOpen >= guardSamples)
                    {
                        state = State::Evaluating;
                        evalCounter = 0;
                        evalPeak = 0.0f;
                        evalLevelDb = levelDb;
                        bandEnergy.fill(0.0f);
                    }
                    break;

                case State::Evaluating:
                {
                    evalPeak = std::max(evalPeak, std::fabs(raw));
                    evalLevelDb = std::max(evalLevelDb, levelDb);
                    for (size_t b = 0; b < bands.size(); ++b) bandEnergy[b] += bandOut[b] * bandOut[b];

                    if (++evalCounter >= analysisSamples)
                    {
                        const auto fp = makeFingerprint();
                        lastFingerprint = fp;
                        if (decideHit(fp, s))
                        {
                            const float strength = std::clamp((evalLevelDb - openThr) / 24.0f, 0.0f, 1.0f);
                            openGate(i, s, holdSamples, strength, evalPeak, onTrigger);
                        }
                        else
                        {
                            state = State::Rejected; // wait for this sound to fall away before looking again
                            onTrigger(TriggerEvent { i, false, 0.0f, true });
                        }
                    }
                    break;
                }

                case State::Rejected:
                    if (levelDb <= closeThr) state = State::Closed;
                    break;

                case State::Open:
                    if (levelDb > closeThr)
                        holdCounter = holdSamples;
                    else if (holdCounter > 0)
                        --holdCounter;
                    else
                        closeGate(i, onTrigger);
                    break;
            }

            // ---- envelope ----
            if (s.gateBypass)
                openness = 1.0f;
            else if (state == State::Open)
                openness = std::min(1.0f, openness + attackStep);
            else
                openness *= hitReleaseCoef;

            punchEnv *= punchCoef;
            levelGain += levelSmoothCoef * (levelGainTarget - levelGain);

            float gLow, gHigh;
            if (gateMode)
            {
                gLow  = rangeLow  + (1.0f - rangeLow)  * openness;
                gHigh = rangeHigh + (1.0f - rangeHigh) * openness;
                if (punchGain > 1.0f && ! s.gateBypass)
                {
                    const float p = 1.0f + (punchGain - 1.0f) * punchEnv * openness;
                    gLow *= p; gHigh *= p;
                }
            }
            else
            {
                gLow  = 1.0f - (1.0f - rangeLow)  * openness;
                gHigh = 1.0f - (1.0f - rangeHigh) * openness;
            }
            const float displayGain = std::max(gLow, gHigh);
            lastGainDb = gainToDb(displayGain);

            const bool applyDynamics = gateMode && ! s.keyListen && ! s.gateBypass;
            const float lvl = applyDynamics ? levelGain : 1.0f;
            const bool clip = applyDynamics && s.clipDb > 0.01f && clipCeiling > 0.0f;

            // ---- delay + apply ----
            const int readPos = (writePos - totalDelay + maxDelay) % maxDelay;
            float inAbs = 0.0f;

            auto& keyLine = delay[size_t(channels)];
            keyLine[size_t(writePos)] = k;
            const float delayedKey = keyLine[size_t(readPos)];

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = delay[size_t(ch)];
                line[size_t(writePos)] = audio[ch][i];
                float dry = line[size_t(readPos)];
                inAbs = std::max(inAbs, std::fabs(dry));

                float gated;
                if (s.bandSplit && ! s.gateBypass)
                {
                    float lo, hi;
                    crossovers[size_t(ch)].process(dry, lo, hi);
                    dry = lo + hi; // allpass sum: keeps dry and wet phase-aligned for the mix
                    gated = lo * gLow + hi * gHigh;
                }
                else
                    gated = dry * gLow;

                gated *= lvl;
                if (clip) gated = softClip(gated, clipCeiling);

                const float out = s.keyListen ? delayedKey : dry * (1.0f - wet) + gated * wet;
                audio[ch][i] = out * outGain;
            }
            for (int ch = chans; ch < numChannels; ++ch)
                audio[ch][i] = 0.0f;

            writePos = (writePos + 1) % maxDelay;

            perSample(i, inAbs, levelDb, displayGain);
        }
    }

    template <typename OnTrigger>
    void process(float* const* audio, int numChannels, const float* key, int numSamples,
                 const Settings& s, OnTrigger&& onTrigger)
    {
        process(audio, numChannels, key, numSamples, nullptr, 0, s, [](int, float, float, float) {}, onTrigger);
    }

    /** Smooth clipper: untouched below 70% of the ceiling, then bends into it. */
    static float softClip(float x, float ceiling)
    {
        const float a = std::fabs(x), knee = 0.7f * ceiling;
        if (a <= knee) return x;
        const float span = ceiling - knee;
        const float y = knee + span * std::tanh((a - knee) / span);
        return x < 0.0f ? -y : y;
    }

private:
    enum class State { Closed, Evaluating, Open, Rejected };

    Fingerprint makeFingerprint() const
    {
        Fingerprint fp {};
        float mean = 0.0f;
        for (size_t b = 0; b < fp.size(); ++b)
        {
            fp[b] = 10.0f * std::log10(bandEnergy[b] + 1.0e-12f);
            mean += fp[b];
        }
        mean /= float(fp.size());
        for (auto& v : fp) v -= mean;
        return fp;
    }

    bool decideHit(const Fingerprint& fp, const Settings& s)
    {
        lastMatchDistance = hitProfile.valid() ? fingerprintDistance(fp, hitProfile.mean) : -1.0f;

        if (s.learn == LearnMode::Hits)  { hitProfile.add(fp);   lastHitRejected = false; return true; }
        if (s.learn == LearnMode::Spill) { spillProfile.add(fp); lastHitRejected = true;  return false; }

        bool accept = true;
        if (s.hitMatch)
        {
            const float dHit = hitProfile.valid() ? fingerprintDistance(fp, hitProfile.mean) : -1.0f;
            const float dSpill = spillProfile.valid() ? fingerprintDistance(fp, spillProfile.mean) : -1.0f;
            if (dHit >= 0.0f && dHit > s.matchToleranceDb) accept = false;
            if (dSpill >= 0.0f && dSpill <= s.matchToleranceDb && (dHit < 0.0f || dSpill < dHit)) accept = false;
        }
        lastHitRejected = ! accept;
        return accept;
    }

    template <typename OnTrigger>
    void openGate(int i, const Settings& s, int holdSamples, float strength, float hitPeak, OnTrigger&& onTrigger)
    {
        state = State::Open;
        holdCounter = holdSamples;
        samplesSinceOpen = 0;
        punchEnv = 1.0f;

        // A re-opening while the previous hit's tail is still sounding (its
        // decay bumping back over the threshold) is part of that hit, not a
        // new one - it mustn't swap in a shorter release, or have the
        // leveller boost a quiet tail. Only a clearly new, comparable hit
        // re-measures.
        const bool newHit = openness < 0.05f || hitPeak > currentHitPeak * 0.5f;
        if (newHit)
        {
            currentHitPeak = hitPeak;

            // Adaptive decay: release scales from x0.5 (softest) to x2 (hardest) at 100%
            const float factor = std::pow(2.0f, s.adaptiveDecay * (2.0f * strength - 1.0f));
            hitReleaseCoef = releaseCoefFor(s.releaseMs * factor);

            // Leveller: move this hit's peak toward the target by `amount`
            const float peakDb = gainToDb(std::max(hitPeak, 1.0e-6f));
            const float levelDb = std::clamp(s.levelAmount * (s.levelTargetDb - peakDb), -18.0f, 18.0f);
            levelGainTarget = dbToGain(levelDb);

            // Clipper ceiling for this hit: clipDb below its levelled peak
            clipCeiling = s.clipDb > 0.01f ? std::max(1.0e-4f, hitPeak * levelGainTarget * dbToGain(-s.clipDb)) : 0.0f;
        }

        onTrigger(TriggerEvent { i, true, strength });
    }

    template <typename OnTrigger>
    void closeGate(int i, OnTrigger&& onTrigger)
    {
        state = State::Closed;
        onTrigger(TriggerEvent { i, false, 0.0f });
    }

    static float msToCoef(double sr, float ms)
    {
        // one-pole coefficient: reaches ~63% in `ms`
        return ms <= 0.0f ? 0.0f : float(std::exp(-1.0 / (sr * ms / 1000.0)));
    }

    float releaseCoefFor(float ms) const
    {
        // Exponential, falling 60 dB over the release time (so the knob means
        // "time to fully close", and the tail sounds natural).
        return float(std::pow(0.001, 1.0 / std::max(1.0, sr * ms / 1000.0)));
    }

    void updateCoefficients(const Settings& s)
    {
        auto changed = [this](float a, float b) { return ! lastSettingsValid || std::fabs(a - b) > 1.0e-3f; };
        if (changed(s.keyHpfHz, last.keyHpfHz)) hpf.setHighPass(sr, s.keyHpfHz);
        if (changed(s.keyLpfHz, last.keyLpfHz)) lpf.setLowPass(sr, s.keyLpfHz);
        if (changed(s.crossoverHz, last.crossoverHz))
            for (auto& x : crossovers) x.setFrequency(sr, s.crossoverHz);

        attackStep = s.attackMs <= 0.0f ? 1.0f : float(1.0 / std::max(1.0, sr * s.attackMs / 1000.0));
        if (changed(s.releaseMs, last.releaseMs) || changed(s.adaptiveDecay, last.adaptiveDecay))
            hitReleaseCoef = releaseCoefFor(s.releaseMs); // the next hit re-scales it
        if (s.levelAmount <= 0.0f) levelGainTarget = 1.0f;

        peakRelCoef = msToCoef(sr, 10.0f);
        rmsCoef     = 1.0f - msToCoef(sr, 5.0f);
        fastAttCoef = 1.0f - msToCoef(sr, 0.05f);
        fastRelCoef = msToCoef(sr, 8.0f);
        slowAttCoef = 1.0f - msToCoef(sr, 15.0f);
        slowRelCoef = msToCoef(sr, 120.0f);
        punchCoef   = msToCoef(sr, 25.0f);
        levelSmoothCoef = 1.0f - msToCoef(sr, 1.0f);

        last = s;
        lastSettingsValid = true;
    }

    double sr = 44100.0;
    int channels = 2;
    int maxDelay = 1;
    int writePos = 0;
    int analysisSamples = 1;
    std::vector<std::vector<float>> delay;
    std::vector<Crossover> crossovers;

    Biquad hpf, lpf;
    std::array<Biquad, numFingerprintBands> bands;
    std::array<float, numFingerprintBands> bandEnergy {};

    float envPeak = 0, envRms = 0, envFast = 0, envSlow = 0;
    float peakRelCoef = 0, rmsCoef = 0, fastAttCoef = 0, fastRelCoef = 0, slowAttCoef = 0, slowRelCoef = 0;
    float attackStep = 1, hitReleaseCoef = 0, punchCoef = 0, levelSmoothCoef = 1;

    State state = State::Closed;
    int holdCounter = 0;
    int evalCounter = 0;
    float evalPeak = 0.0f, evalLevelDb = -120.0f;
    int midiCountdown = -1;
    float midiVelocity = 1.0f;
    int samplesSinceOpen = 1 << 30;
    float openness = 0.0f;
    float punchEnv = 0.0f;
    float levelGain = 1.0f, levelGainTarget = 1.0f;
    float clipCeiling = 0.0f;
    float currentHitPeak = 0.0f;
    float lastGainDb = 0.0f, lastKeyDb = -120.0f;

    Settings last;
    bool lastSettingsValid = false;
};

} // namespace GHSGate
