// Behavioural tests for the drum replacer (SamplerDSP.h). No JUCE.
#include "SamplerDSP.h"
#include <cstdio>

using namespace GHSSampler;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } \
                              else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

// a one-shot: `lead` seconds of silence, then a decaying 200 Hz hit at `gain`
static Sample oneShot(const char* name, float gain, double lead = 0.0, double sr = 48000.0, double freq = 200.0)
{
    Sample s;
    s.name = name;
    s.sampleRate = sr;
    const int n = int(sr * (lead + 0.5));
    s.data.assign(1, std::vector<float>(size_t(n), 0.0f));
    for (int i = int(lead * sr); i < n; ++i)
    {
        const double t = i / sr - lead;
        s.data[0][size_t(i)] = gain * float(std::exp(-t * 10.0) * std::sin(2 * M_PI * freq * t + 0.3));
    }
    s.analyse();
    return s;
}

static std::vector<float> renderHits(Sampler& sm, int total, std::vector<std::pair<int, float>> hits, const Params& p, int block = 256)
{
    std::vector<float> out(size_t(total), 0.0f);
    size_t h = 0;
    for (int s = 0; s < total; s += block)
    {
        const int n = std::min(block, total - s);
        // triggers whose time falls in this block are scheduled relative to its start
        while (h < hits.size() && hits[h].first < s + n) { sm.trigger(hits[h].first - s, hits[h].second, p); ++h; }
        float* ch[1] = { out.data() + s };
        sm.render(ch, 1, n);
    }
    return out;
}

static int firstAbove(const std::vector<float>& x, float thr, int from = 0)
{
    for (int i = from; i < int(x.size()); ++i) if (std::fabs(x[size_t(i)]) > thr) return i;
    return -1;
}

static float peakDb(const std::vector<float>& x, int from, int to)
{
    float p = 0.0f;
    for (int i = std::max(0, from); i < to && i < int(x.size()); ++i) p = std::max(p, std::fabs(x[size_t(i)]));
    return 20.0f * std::log10(std::max(p, 1e-9f));
}

int main()
{
    // 1. Automatic layering: 3 loudness levels x 2 alternatives -> 3 layers of 2
    {
        Bank b;
        for (float g : { 0.1f, 0.105f, 0.4f, 0.41f, 1.0f, 0.98f }) b.samples.push_back(oneShot("s", g));
        b.build();
        bool ok = b.layers.size() == 3;
        for (auto& l : b.layers) ok &= l.samples.size() == 2;
        ok &= b.samples[size_t(b.layers[0].samples[0])].attackDb < b.samples[size_t(b.layers[2].samples[0])].attackDb;
        CHECK(ok, "6 one-shots -> %d velocity layers (softest first), 2 round-robins each", int(b.layers.size()));
        CHECK(b.layerFor(0.1f) == 0 && b.layerFor(0.5f) == 1 && b.layerFor(1.0f) == 2, "velocity picks the layer");
    }

    // 2. Lead-in trimmed: transient lands exactly at the trigger time
    {
        Bank b;
        b.samples.push_back(oneShot("lead", 1.0f, 0.012)); // 12 ms of silence first
        b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.dynamics = 0.0f;
        auto out = renderHits(sm, 48000, { { 10000, 1.0f } }, p);
        const int at = firstAbove(out, 0.05f);
        CHECK(std::abs(at - 10000) <= 12, "12 ms lead-in trimmed: transient at %d (trigger at 10000)", at);
    }

    // 3. Scheduling across blocks and inside a block, any block size
    {
        Bank b; b.samples.push_back(oneShot("x", 1.0f)); b.build();
        bool ok = true;
        for (int block : { 32, 256, 1000 })
            for (int when : { 5, 777, 4321 })
            {
                Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
                Params p; p.dynamics = 0.0f;
                // trigger issued at block start with a delay (like the gate does with its lookahead)
                std::vector<float> out(9000, 0.0f);
                for (int s = 0; s < 9000; s += block)
                {
                    const int n = std::min(block, 9000 - s);
                    if (s == 0) sm.trigger(when, 1.0f, p);
                    float* ch[1] = { out.data() + s };
                    sm.render(ch, 1, n);
                }
                const int at = firstAbove(out, 0.05f);
                ok &= std::abs(at - when) <= 12;
            }
        CHECK(ok, "delayed start is sample-accurate across block sizes");
    }

    // 4. Dynamics: level follows velocity by the amount set
    {
        Bank b; b.samples.push_back(oneShot("x", 1.0f)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.dynamics = 1.0f;
        auto out = renderHits(sm, 48000, { { 100, 1.0f }, { 24100, 0.5f } }, p);
        const float d1 = peakDb(out, 24000, 48000) - peakDb(out, 0, 24000);
        sm.reset(); p.dynamics = 0.0f;
        auto out0 = renderHits(sm, 48000, { { 100, 1.0f }, { 24100, 0.5f } }, p);
        const float d0 = peakDb(out0, 24000, 48000) - peakDb(out0, 0, 24000);
        CHECK(std::fabs(d1 + 10.84f) < 0.5f && std::fabs(d0) < 0.3f, "velocity 0.5: %.1f dB at Dynamics 100%%, %.1f dB at 0%%", d1, d0);
    }

    // 5. Round robin alternates, never repeats
    {
        Bank b; b.samples.push_back(oneShot("a", 1.0f)); b.samples.push_back(oneShot("b", 0.99f)); b.samples.push_back(oneShot("c", 1.01f)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p;
        int last = -1; bool ok = true; int distinct = 0; std::vector<int> seen(3, 0);
        for (int k = 0; k < 12; ++k) { sm.trigger(0, 1.0f, p); ok &= sm.lastHit.sample != last; last = sm.lastHit.sample; seen[size_t(last)]++; }
        for (int c : seen) distinct += c > 0;
        CHECK(ok && distinct == 3, "round robin over 3 samples, no repeats (used %d)", distinct);
    }

    // 6. Pitch and sample-rate conversion
    {
        auto countCross = [](const std::vector<float>& x, int from, int to) { int c = 0; for (int i = from + 1; i < to; ++i) c += (x[size_t(i - 1)] < 0) != (x[size_t(i)] < 0); return c; };
        Bank b; b.samples.push_back(oneShot("x", 1.0f, 0.0, 44100.0)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.dynamics = 0.0f;
        auto out = renderHits(sm, 48000, { { 0, 1.0f } }, p);
        const int c0 = countCross(out, 0, 4800); // 0.1 s of a 200 Hz tone -> ~40 crossings
        p.pitchSemis = 12.0f; sm.reset();
        auto up = renderHits(sm, 48000, { { 0, 1.0f } }, p);
        const int c12 = countCross(up, 0, 4800);
        CHECK(std::abs(c0 - 40) <= 1, "44.1 kHz sample at 48 kHz keeps its pitch (%d crossings in 0.1 s, expect 40)", c0);
        CHECK(std::abs(c12 - 80) <= 2, "+12 semitones doubles the frequency (%d crossings)", c12);
    }

    // 7. Choke: a new hit fades the previous one within ~4 ms; without choke they overlap
    {
        Bank b; b.samples.push_back(oneShot("x", 1.0f)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.dynamics = 0.0f; p.choke = true;
        renderHits(sm, 12000, { { 0, 1.0f }, { 6000, 1.0f } }, p);
        const int withChoke = sm.activeVoices();
        sm.reset(); p.choke = false;
        renderHits(sm, 12000, { { 0, 1.0f }, { 6000, 1.0f } }, p);
        CHECK(withChoke == 1 && sm.activeVoices() == 2, "choke: %d voice(s) left (without choke: %d)", withChoke, sm.activeVoices());
    }

    // 8. Length shortens with a fade, no click
    {
        Bank b; b.samples.push_back(oneShot("x", 1.0f)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.dynamics = 0.0f; p.length = 0.25f;
        auto out = renderHits(sm, 48000, { { 0, 1.0f } }, p);
        const int cut = int(0.25 * 0.5 * 48000);
        float maxStep = 0.0f;
        for (int i = cut - 1000; i < cut + 100; ++i) maxStep = std::max(maxStep, std::fabs(out[size_t(i + 1)] - out[size_t(i)]));
        CHECK(peakDb(out, cut + 10, 48000) < -100.0f && maxStep < 0.05f, "length 25%%: silent after %d, smooth fade (max step %.3f)", cut, maxStep);
    }

    // 9. Built-in kits are valid
    {
        bool ok = true;
        for (auto kit : { Synth::kickKit(), Synth::snareKit(), Synth::tomKit(), Synth::clapKit() })
        {
            float peak = 0.0f; bool finite = true;
            for (auto& s : kit.samples) for (auto& ch : s.data) for (float v : ch) { peak = std::max(peak, std::fabs(v)); finite &= std::isfinite(v); }
            std::printf("      %-11s %d samples, %d layers, peak %.2f\n", kit.name.c_str(), int(kit.samples.size()), int(kit.layers.size()), peak);
            ok &= finite && peak <= 1.0f && peak > 0.2f && kit.layers.size() >= 3;
        }
        CHECK(ok, "built-in kits: finite, under 0 dBFS, at least 3 velocity layers");
    }

    // 10. Voice stealing never crashes or exceeds the pool
    {
        Bank b; b.samples.push_back(oneShot("x", 1.0f)); b.build();
        Sampler sm; sm.prepare(48000.0); sm.setBank(&b);
        Params p; p.choke = false;
        std::vector<std::pair<int, float>> hits;
        for (int k = 0; k < 100; ++k) hits.push_back({ k * 200, 1.0f });
        renderHits(sm, 21000, hits, p);
        CHECK(sm.activeVoices() <= Sampler::maxVoices, "100 rapid hits: %d voices (max %d)", sm.activeVoices(), Sampler::maxVoices);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
