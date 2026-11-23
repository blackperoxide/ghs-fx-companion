#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

/**
 * Drum replacement / reinforcement: plays one-shot samples on every hit the
 * gate accepts. JUCE-free so it can be unit-tested (Tests/SamplerTest.cpp).
 *
 *  - Drop in any number of one-shots: they're sorted into velocity layers
 *    automatically by how loud their attack is, and samples of the same
 *    loudness (within 1.5 dB) become round-robin alternatives in a layer.
 *  - Each sample's silent lead-in is trimmed so its transient lands exactly
 *    where the original hit's did.
 *  - Velocity (from the hit's strength, or a MIDI note) picks the layer and,
 *    by the Dynamics amount, the level.
 *  - Pitch, Length (shorten with a fade), Choke (a new hit fades the previous
 *    one), and a timing Offset. A negative offset (sample earlier than the
 *    detected hit) is possible up to the gate's lookahead.
 *  - Samples play at their own sample rate (cubic interpolation), so any
 *    file works at any session rate.
 */
namespace GHSSampler
{

struct Sample
{
    std::string name;
    std::vector<std::vector<float>> data; // [channel][frame], 1 or 2 channels
    double sampleRate = 48000.0;
    int start = 0;        // first frame of the transient (lead-in trimmed)
    float attackDb = -120.0f; // loudness of the first 30 ms after the start (used for layering)

    int frames() const { return data.empty() ? 0 : int(data[0].size()); }

    /** Finds the transient and measures the attack. Call after filling data. */
    void analyse()
    {
        const int n = frames();
        float peak = 0.0f;
        for (auto& ch : data) for (float v : ch) peak = std::max(peak, std::fabs(v));
        start = 0;
        if (peak > 0.0f)
        {
            const float thr = peak * 0.03f; // -30 dB of the peak
            for (int i = 0; i < n; ++i)
            {
                float a = 0.0f;
                for (auto& ch : data) a = std::max(a, std::fabs(ch[size_t(i)]));
                if (a >= thr) { start = i; break; }
            }
            start = std::max(0, start - int(sampleRate * 0.0002)); // keep 0.2 ms before it
        }
        const int win = std::min(n - start, int(sampleRate * 0.030));
        double e = 0.0;
        for (auto& ch : data)
            for (int i = 0; i < win; ++i) e += double(ch[size_t(start + i)]) * ch[size_t(start + i)];
        attackDb = win > 0 && e > 0.0 ? float(10.0 * std::log10(e / double(win * int(data.size())))) : -120.0f;
    }
};

/** A set of samples arranged into velocity layers. Built once, then read-only (shared with the audio thread). */
struct Bank
{
    struct Layer
    {
        std::vector<int> samples; // round-robin alternatives (indices into Bank::samples)
        float attackDb = 0.0f;
    };

    std::string name;
    std::vector<Sample> samples;
    std::vector<Layer> layers; // softest first

    void build(float sameLayerDb = 1.5f)
    {
        layers.clear();
        std::vector<int> order(samples.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return samples[size_t(a)].attackDb < samples[size_t(b)].attackDb; });
        for (int idx : order)
        {
            const float db = samples[size_t(idx)].attackDb;
            if (! layers.empty() && db - samples[size_t(layers.back().samples.front())].attackDb <= sameLayerDb)
                layers.back().samples.push_back(idx);
            else
                layers.push_back({ { idx }, db });
        }
    }

    /** Velocity 0..1 -> layer index (layers split the range evenly). */
    int layerFor(float velocity) const
    {
        if (layers.empty()) return -1;
        const int L = int(layers.size());
        return std::clamp(int(velocity * float(L)), 0, L - 1);
    }
};

struct Params
{
    float levelDb = 0.0f;
    float dynamics = 0.7f;   // 0 = every hit at full level, 1 = level follows velocity (36 dB range)
    float pitchSemis = 0.0f;
    float length = 1.0f;     // 0.05 .. 1 of the sample
    bool choke = true;
};

class Sampler
{
public:
    static constexpr int maxVoices = 24;

    void prepare(double hostSampleRate) { hostSr = hostSampleRate; reset(); }
    void reset() { for (auto& v : voices) v.active = false; }

    /** The bank must stay alive until another one is set. Stops every voice. */
    void setBank(const Bank* b) { bank = b; reset(); rr.assign(b ? b->layers.size() : 0, 0); lastRR.assign(rr.size(), -1); }
    const Bank* getBank() const { return bank; }

    struct Hit { int layer = -1, sample = -1; float velocity = 0.0f; };
    Hit lastHit; // for the UI

    /** Schedules a hit: it starts `delaySamples` from the start of the next render() block. */
    void trigger(int delaySamples, float velocity, const Params& p)
    {
        if (bank == nullptr || bank->layers.empty()) return;
        velocity = std::clamp(velocity, 0.0f, 1.0f);
        const int layer = bank->layerFor(velocity);
        const auto& L = bank->layers[size_t(layer)];

        // round robin, never the same sample twice in a row when there's a choice
        int pick = rr[size_t(layer)]++ % int(L.samples.size());
        if (L.samples.size() > 1 && pick == lastRR[size_t(layer)]) pick = (pick + 1) % int(L.samples.size());
        lastRR[size_t(layer)] = pick;
        const Sample& s = bank->samples[size_t(L.samples[size_t(pick)])];

        Voice* v = freeVoice();
        v->active = true;
        v->started = false;
        v->sample = &s;
        v->delay = std::max(0, delaySamples);
        v->pos = s.start;
        v->rate = s.sampleRate / hostSr * std::pow(2.0, double(p.pitchSemis) / 12.0);
        const float velDb = 36.0f * std::clamp(p.dynamics, 0.0f, 1.0f) * std::log10(std::max(velocity, 0.001f));
        v->gain = std::pow(10.0f, (velDb + p.levelDb) / 20.0f);
        v->end = s.start + double(s.frames() - s.start) * std::clamp(double(p.length), 0.05, 1.0);
        v->fadeLen = std::max(1.0, std::min(0.020 * s.sampleRate, (v->end - s.start) * 0.25)); // in sample frames
        v->chokeGain = 1.0f;
        v->chokeStep = 0.0f;
        v->choke = p.choke;
        lastHit = { layer, L.samples[size_t(pick)], velocity };
    }

    /** Adds every voice into the output (numChannels 1 or 2+). */
    void render(float* const* out, int numChannels, int numSamples)
    {
        if (bank == nullptr) return;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            int i = 0;
            if (! v.started)
            {
                if (v.delay >= numSamples) { v.delay -= numSamples; continue; }
                i = v.delay;
                v.delay = 0;
                v.started = true;
                if (v.choke) chokeOthers(&v);
            }
            const Sample& s = *v.sample;
            const int chans = int(s.data.size());
            for (; i < numSamples && v.active; ++i)
            {
                if (v.pos >= v.end - 1.0) { v.active = false; break; }
                // fade over the last fadeLen frames when Length shortens the sample
                float g = v.gain * v.chokeGain;
                const double toEnd = v.end - v.pos;
                if (v.end < double(s.frames()) - 1.0 && toEnd < v.fadeLen) g *= float(toEnd / v.fadeLen);

                const float l = interp(s.data[0], v.pos) * g;
                const float r = chans > 1 ? interp(s.data[1], v.pos) * g : l;
                if (numChannels == 1) out[0][i] += 0.5f * (l + r);
                else { out[0][i] += l; out[1][i] += r; for (int c = 2; c < numChannels; ++c) out[c][i] += 0.5f * (l + r); }

                v.pos += v.rate;
                if (v.chokeStep > 0.0f)
                {
                    v.chokeGain -= v.chokeStep;
                    if (v.chokeGain <= 0.0f) v.active = false;
                }
            }
        }
    }

    int activeVoices() const { int n = 0; for (auto& v : voices) n += v.active && v.started; return n; }

private:
    struct Voice
    {
        bool active = false, started = false, choke = true;
        const Sample* sample = nullptr;
        int delay = 0;
        double pos = 0.0, rate = 1.0, end = 0.0, fadeLen = 1.0;
        float gain = 1.0f, chokeGain = 1.0f, chokeStep = 0.0f;
    };

    static float interp(const std::vector<float>& d, double pos)
    {
        // 4-point cubic Hermite
        const int i = int(pos);
        const float t = float(pos - i);
        const int n = int(d.size());
        auto at = [&](int k) { return (k >= 0 && k < n) ? d[size_t(k)] : 0.0f; };
        const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void chokeOthers(const Voice* keep)
    {
        const float step = float(1.0 / std::max(1.0, hostSr * 0.004)); // 4 ms fade
        for (auto& o : voices)
            if (&o != keep && o.active && o.started && o.chokeStep <= 0.0f)
                o.chokeStep = step;
    }

    Voice* freeVoice()
    {
        for (auto& v : voices) if (! v.active) return &v;
        // steal the quietest / oldest playing voice
        Voice* best = &voices[0];
        for (auto& v : voices) if (v.gain * v.chokeGain < best->gain * best->chokeGain) best = &v;
        return best;
    }

    double hostSr = 48000.0;
    const Bank* bank = nullptr;
    Voice voices[maxVoices];
    std::vector<int> rr, lastRR;
};

// ======================= Built-in synthesized one-shots =======================
// So the replacer works out of the box. Each kit: 3 velocity layers x 2
// round-robin variations, generated at 48 kHz.

namespace Synth
{
inline Sample render(const std::string& name, double seconds, unsigned seed, float level,
                     float (*voice)(double t, std::mt19937& rng, float level, float variation))
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> var(-1.0f, 1.0f);
    const float variation = var(rng);
    Sample s;
    s.name = name;
    s.sampleRate = 48000.0;
    const int n = int(seconds * s.sampleRate);
    s.data.assign(2, std::vector<float>(size_t(n)));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / s.sampleRate;
        const float v = voice(t, rng, level, variation);
        s.data[0][size_t(i)] = v;
        s.data[1][size_t(i)] = v;
    }
    // short fade-out at the very end
    for (int i = 0; i < 480 && i < n; ++i) { const float g = float(i) / 480.0f; s.data[0][size_t(n - 1 - i)] *= g; s.data[1][size_t(n - 1 - i)] *= g; }
    s.analyse();
    return s;
}

inline float kick(double t, std::mt19937& rng, float level, float var)
{
    std::normal_distribution<float> nz(0.0f, 1.0f);
    const double sweep = 48.0 + (110.0 + 60.0 * level) * std::exp(-t * (30.0 + 4.0 * var));
    // phase of a swept sine = integral of frequency
    const double k = 30.0 + 4.0 * var;
    const double phase = 2.0 * M_PI * (48.0 * t + (110.0 + 60.0 * level) * (1.0 - std::exp(-t * k)) / k);
    (void) sweep;
    const double body = std::sin(phase) * std::exp(-t * (5.0 - 1.5 * level));
    const double click = t < 0.004 ? nz(rng) * (1.0 - t / 0.004) * 0.35 * level : 0.0;
    return float(level * 0.95 * std::tanh(1.4 * (body + click)));
}

inline float snare(double t, std::mt19937& rng, float level, float var)
{
    std::normal_distribution<float> nz(0.0f, 1.0f);
    const double body = (0.7 * std::sin(2 * M_PI * (185.0 + 8.0 * var) * t) + 0.35 * std::sin(2 * M_PI * 330.0 * t)) * std::exp(-t * 22.0);
    const double wires = nz(rng) * std::exp(-t * (11.0 - 3.0 * level + var)) * (0.45 + 0.35 * level);
    return float(level * 0.9 * std::tanh(1.2 * (body * (0.6 + 0.4 * level) + wires)));
}

inline float tom(double t, std::mt19937&, float level, float var)
{
    const double k = 12.0;
    const double phase = 2.0 * M_PI * ((98.0 + 3.0 * var) * t + 45.0 * (1.0 - std::exp(-t * k)) / k);
    return float(level * 0.9 * std::tanh(1.3 * std::sin(phase) * std::exp(-t * (4.5 - level))));
}

inline float clap(double t, std::mt19937& rng, float level, float var)
{
    std::normal_distribution<float> nz(0.0f, 1.0f);
    double env = 0.0;
    for (int b = 0; b < 3; ++b)
    {
        const double tb = t - b * (0.009 + 0.001 * var);
        if (tb >= 0) env = std::max(env, std::exp(-tb * 180.0));
    }
    env = std::max(env, 0.35 * std::exp(-t * 14.0));
    return float(level * 0.8 * nz(rng) * env);
}

inline Bank makeKit(const std::string& name, double seconds, float (*voice)(double, std::mt19937&, float, float), unsigned seed)
{
    Bank b;
    b.name = name;
    const float levels[3] = { 0.30f, 0.58f, 1.0f };
    for (int l = 0; l < 3; ++l)
        for (int r = 0; r < 2; ++r)
            b.samples.push_back(render(name + " " + std::to_string(l + 1) + (r ? "b" : "a"), seconds, seed + unsigned(l * 7 + r * 101), levels[l], voice));
    // normalise the kit's loudest peak to -1 dBFS (layers keep their relative levels)
    float peak = 0.0f;
    for (auto& smp : b.samples) for (auto& ch : smp.data) for (float v : ch) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f)
        for (auto& smp : b.samples) { for (auto& ch : smp.data) for (float& v : ch) v *= 0.891f / peak; smp.analyse(); }
    b.build();
    return b;
}

inline Bank kickKit()  { return makeKit("Synth Kick", 0.8, kick, 11); }
inline Bank snareKit() { return makeKit("Synth Snare", 0.6, snare, 23); }
inline Bank tomKit()   { return makeKit("Synth Tom", 0.9, tom, 37); }
inline Bank clapKit()  { return makeKit("Synth Clap", 0.5, clap, 41); }
} // namespace Synth

} // namespace GHSSampler
