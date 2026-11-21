#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * The gate engine, kept free of JUCE so it can be unit-tested on its own
 * (see DrumGate/Tests/GateDSPTest.cpp) and so the audio-thread code is easy
 * to read in one place.
 *
 * Signal flow per sample:
 *
 *   key (internal input or external sidechain, summed to mono)
 *     -> key HPF -> key LPF            (focus the detector on the drum)
 *     -> detector (Peak / RMS / Transient)
 *     -> open/close state machine      (threshold + hysteresis, hold,
 *                                        retrigger guard)
 *     -> envelope                      (linear attack, exponential release,
 *                                        optional "punch" accent on open)
 *     -> gain law                      (Gate: floor = Range; Duck: inverted)
 *
 *   main audio -> lookahead delay -> x gain -> dry/wet mix -> output trim
 *
 * Lookahead works by delaying the *audio* while the detector looks at the
 * undelayed key, so the gate is already opening when the transient arrives.
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

    void setHighPass(double sr, double freq, double q = 0.7071)
    {
        const double w0 = 2.0 * M_PI * std::clamp(freq, 10.0, sr * 0.45) / sr;
        const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = float(((1.0 + c) / 2.0) / a0);
        b1 = float(-(1.0 + c) / a0);
        b2 = b0;
        a1 = float((-2.0 * c) / a0);
        a2 = float((1.0 - alpha) / a0);
    }

    void setLowPass(double sr, double freq, double q = 0.7071)
    {
        const double w0 = 2.0 * M_PI * std::clamp(freq, 10.0, sr * 0.45) / sr;
        const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = float(((1.0 - c) / 2.0) / a0);
        b1 = float((1.0 - c) / a0);
        b2 = b0;
        a1 = float((-2.0 * c) / a0);
        a2 = float((1.0 - alpha) / a0);
    }

    float process(float x)
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

enum class DetectMode { Peak = 0, RMS = 1, Transient = 2 };
enum class GateMode   { Gate = 0, Duck = 1 };

struct Settings
{
    float thresholdDb   = -24.0f;
    float hysteresisDb  = 6.0f;   // close threshold = threshold - hysteresis
    float attackMs      = 0.5f;
    float holdMs        = 50.0f;
    float releaseMs     = 150.0f;
    float rangeDb       = -60.0f; // how far the gate closes (-80 = fully shut)
    float lookaheadMs   = 1.0f;
    float keyHpfHz      = 60.0f;
    float keyLpfHz      = 12000.0f;
    DetectMode detect   = DetectMode::Peak;
    float transientSensDb = 6.0f; // Transient mode: how far the fast envelope must jump above the slow one
    float retriggerMs   = 0.0f;   // minimum time between gate openings (double-hit / flam guard)
    GateMode mode       = GateMode::Gate;
    float punchDb       = 0.0f;   // extra gain right at the opening, decays quickly
    float mix           = 1.0f;   // 0..1 dry/wet (parallel gating)
    float outputDb      = 0.0f;
    bool keyListen      = false;
};

/** Called for every gate opening/closing - used for MIDI output and the display's hit markers. */
struct TriggerEvent
{
    int sampleOffset;  // within the current block
    bool isOpen;       // true = gate opened (a hit), false = gate closed
    float strength;    // 0..1, how far the hit exceeded the threshold (for MIDI velocity)
};

class Engine
{
public:
    static constexpr float maxLookaheadMs = 10.0f;

    void prepare(double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = std::max(1, numChannels);
        maxDelay = int(std::ceil(sr * maxLookaheadMs / 1000.0)) + 1;
        delay.assign(size_t(channels + 1), std::vector<float>(size_t(maxDelay), 0.0f)); // +1 = key channel (for key listen)
        writePos = 0;
        reset();
        lastSettingsValid = false;
    }

    void reset()
    {
        hpf.reset(); lpf.reset();
        envPeak = envRms = envFast = envSlow = 0.0f;
        isOpen = false;
        holdCounter = 0;
        samplesSinceOpen = 1 << 30;
        openness = 0.0f;
        punchEnv = 0.0f;
        for (auto& d : delay) std::fill(d.begin(), d.end(), 0.0f);
    }

    /** Latency the host must compensate for at the given settings. */
    int lookaheadSamples(const Settings& s) const
    {
        return std::clamp(int(std::lround(sr * s.lookaheadMs / 1000.0)), 0, maxDelay - 1);
    }

    bool gateIsOpen() const { return isOpen; }
    float currentGainDb() const { return lastGainDb; }
    float currentKeyDb() const { return lastKeyDb; }

    /**
     * Processes in place.
     *  audio:     channel pointers for the main bus (numChannels as passed to prepare()).
     *  key:       mono key signal (already summed), numSamples long. Not modified.
     *  perSample: optional callback(sampleIndex, inputAbs, keyDb, gain) for the display.
     *  onTrigger: optional callback(TriggerEvent).
     */
    template <typename PerSample, typename OnTrigger>
    void process(float* const* audio, int numChannels, const float* key, int numSamples,
                 const Settings& s, PerSample&& perSample, OnTrigger&& onTrigger)
    {
        updateCoefficients(s);

        const int la = lookaheadSamples(s);
        const float openThr  = s.thresholdDb;
        const float closeThr = s.thresholdDb - std::max(0.0f, s.hysteresisDb);
        const float rangeGain = s.rangeDb <= -79.9f ? 0.0f : dbToGain(s.rangeDb);
        const float punchGain = dbToGain(std::max(0.0f, s.punchDb));
        const float outGain = dbToGain(s.outputDb);
        const float wet = std::clamp(s.mix, 0.0f, 1.0f);
        const int holdSamples = int(sr * s.holdMs / 1000.0);
        const int guardSamples = int(sr * s.retriggerMs / 1000.0);
        const int chans = std::min(numChannels, channels);

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- key filter + detection (undelayed) ----
            const float k = lpf.process(hpf.process(key[i]));
            const float ak = std::fabs(k);

            envPeak = ak > envPeak ? ak : envPeak * peakRelCoef;
            envRms  = envRms + rmsCoef * (k * k - envRms);
            envFast = ak > envFast ? envFast + fastAttCoef * (ak - envFast) : envFast * fastRelCoef;
            envSlow = ak > envSlow ? envSlow + slowAttCoef * (ak - envSlow) : envSlow * slowRelCoef;

            float levelDb;
            bool openCondition;
            switch (s.detect)
            {
                case DetectMode::RMS:
                    levelDb = gainToDb(std::sqrt(std::max(0.0f, envRms)) * 1.41421356f); // sine-calibrated so RMS reads like peak
                    openCondition = levelDb > openThr;
                    break;
                case DetectMode::Transient:
                {
                    levelDb = gainToDb(envFast);
                    // Only a *sudden* rise counts as a hit: rejects bleed and
                    // ringing that creeps up slowly past the threshold.
                    const float rise = levelDb - gainToDb(envSlow);
                    openCondition = levelDb > openThr && rise > s.transientSensDb;
                    break;
                }
                case DetectMode::Peak:
                default:
                    levelDb = gainToDb(envPeak);
                    openCondition = levelDb > openThr;
                    break;
            }
            lastKeyDb = levelDb;

            // ---- state machine ----
            if (samplesSinceOpen < (1 << 30)) ++samplesSinceOpen;

            if (! isOpen)
            {
                if (openCondition && samplesSinceOpen >= guardSamples)
                {
                    isOpen = true;
                    holdCounter = holdSamples;
                    samplesSinceOpen = 0;
                    punchEnv = 1.0f;
                    const float strength = std::clamp((levelDb - openThr) / 24.0f, 0.0f, 1.0f);
                    onTrigger(TriggerEvent { i, true, strength });
                }
            }
            else
            {
                if (levelDb > closeThr)
                    holdCounter = holdSamples;
                else if (holdCounter > 0)
                    --holdCounter;
                else
                {
                    isOpen = false;
                    onTrigger(TriggerEvent { i, false, 0.0f });
                }
            }

            // ---- envelope ----
            if (isOpen)
                openness = std::min(1.0f, openness + attackStep);
            else
                openness *= releaseCoef;

            punchEnv *= punchCoef;

            float gain = s.mode == GateMode::Gate
                           ? rangeGain + (1.0f - rangeGain) * openness
                           : 1.0f - (1.0f - rangeGain) * openness;

            if (s.mode == GateMode::Gate && punchGain > 1.0f)
                gain *= 1.0f + (punchGain - 1.0f) * punchEnv * openness;

            lastGainDb = gainToDb(gain);

            // ---- lookahead delay + apply ----
            const int readPos = (writePos - la + maxDelay) % maxDelay;
            float inAbs = 0.0f;

            auto& keyLine = delay[size_t(channels)];
            keyLine[size_t(writePos)] = k;
            const float delayedKey = keyLine[size_t(readPos)];

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = delay[size_t(ch)];
                line[size_t(writePos)] = audio[ch][i];
                const float dry = line[size_t(readPos)];
                inAbs = std::max(inAbs, std::fabs(dry));

                const float out = s.keyListen ? delayedKey
                                              : dry * ((1.0f - wet) + wet * gain);
                audio[ch][i] = out * outGain;
            }
            for (int ch = chans; ch < numChannels; ++ch)
                audio[ch][i] = 0.0f;

            writePos = (writePos + 1) % maxDelay;

            perSample(i, inAbs, levelDb, gain);
        }
    }

    template <typename OnTrigger>
    void process(float* const* audio, int numChannels, const float* key, int numSamples,
                 const Settings& s, OnTrigger&& onTrigger)
    {
        process(audio, numChannels, key, numSamples, s, [](int, float, float, float) {}, onTrigger);
    }

private:
    static float msToCoef(double sr, float ms)
    {
        // one-pole coefficient: reaches ~63% in `ms`
        return ms <= 0.0f ? 0.0f : float(std::exp(-1.0 / (sr * ms / 1000.0)));
    }

    void updateCoefficients(const Settings& s)
    {
        if (! lastSettingsValid || std::fabs(s.keyHpfHz - last.keyHpfHz) > 1.0e-3f) hpf.setHighPass(sr, s.keyHpfHz);
        if (! lastSettingsValid || std::fabs(s.keyLpfHz - last.keyLpfHz) > 1.0e-3f) lpf.setLowPass(sr, s.keyLpfHz);

        attackStep = s.attackMs <= 0.0f ? 1.0f : float(1.0 / std::max(1.0, sr * s.attackMs / 1000.0));
        // Release: exponential, falling 60 dB over the release time (so the
        // knob means "time to fully close", and the tail sounds natural).
        releaseCoef = float(std::pow(0.001, 1.0 / std::max(1.0, sr * s.releaseMs / 1000.0)));

        peakRelCoef = msToCoef(sr, 10.0f);
        rmsCoef     = 1.0f - msToCoef(sr, 5.0f);
        fastAttCoef = 1.0f - msToCoef(sr, 0.05f);
        fastRelCoef = msToCoef(sr, 8.0f);
        slowAttCoef = 1.0f - msToCoef(sr, 15.0f);
        slowRelCoef = msToCoef(sr, 120.0f);
        punchCoef   = msToCoef(sr, 25.0f);

        last = s;
        lastSettingsValid = true;
    }

    double sr = 44100.0;
    int channels = 2;
    int maxDelay = 1;
    int writePos = 0;
    std::vector<std::vector<float>> delay;

    Biquad hpf, lpf;
    float envPeak = 0, envRms = 0, envFast = 0, envSlow = 0;
    float peakRelCoef = 0, rmsCoef = 0, fastAttCoef = 0, fastRelCoef = 0, slowAttCoef = 0, slowRelCoef = 0;
    float attackStep = 1, releaseCoef = 0, punchCoef = 0;

    bool isOpen = false;
    int holdCounter = 0;
    int samplesSinceOpen = 1 << 30;
    float openness = 0.0f;
    float punchEnv = 0.0f;
    float lastGainDb = 0.0f, lastKeyDb = -120.0f;

    Settings last;
    bool lastSettingsValid = false;
};

} // namespace GHSGate
