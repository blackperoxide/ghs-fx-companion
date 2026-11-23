// Behavioural tests for the gate engine (GateDSP.h). No JUCE, no framework:
// returns non-zero on failure so ctest / CI can run it.
#include "GateDSP.h"
#include <cstdio>
#include <random>

using namespace GHSGate;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } \
                              else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double sr = 48000.0;

struct Run
{
    std::vector<float> in, out, gain;
    int opens = 0, closes = 0;
    std::vector<int> openAt;
    std::vector<float> strengths;
    int latency = 0;
};

// mono signal: low bleed everywhere + decaying 200 Hz hits at the given times
static std::vector<float> drumTrack(double seconds, std::vector<double> hitTimes, float bleedDb = -50.0f, float hitDb = -6.0f,
                                    double freq = 200.0, float noiseAmt = 0.0f)
{
    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::vector<float> x(size_t(seconds * sr));
    const float bleed = dbToGain(bleedDb), hit = dbToGain(hitDb);
    for (size_t i = 0; i < x.size(); ++i)
    {
        float v = bleed * noise(rng) * 0.5f;
        for (double t : hitTimes)
        {
            const double dt = double(i) / sr - t;
            if (dt >= 0.0 && dt < 0.4)
                v += hit * float(std::exp(-dt * 25.0) * std::sin(2.0 * M_PI * freq * dt)
                                 + noiseAmt * std::exp(-dt * 40.0) * noise(rng));
        }
        x[i] = v;
    }
    return x;
}

static Run runGate(const std::vector<float>& input, const Settings& s, int block = 256,
                   Engine* reuse = nullptr, std::vector<MidiTrigger> midi = {})
{
    Engine local;
    Engine& e = reuse ? *reuse : local;
    e.prepare(sr, 1);
    Run r;
    r.latency = e.latencySamples(s);
    r.in = input;
    r.out = input;
    r.gain.resize(input.size());
    for (size_t start = 0; start < input.size(); start += size_t(block))
    {
        const int n = int(std::min(input.size() - start, size_t(block)));
        float* chans[1] = { r.out.data() + start };
        std::vector<MidiTrigger> blockMidi;
        for (auto m : midi)
            if (m.sampleOffset >= int(start) && m.sampleOffset < int(start) + n)
                blockMidi.push_back({ m.sampleOffset - int(start), m.velocity });
        e.process(chans, 1, input.data() + start, n, blockMidi.data(), int(blockMidi.size()), s,
                  [&](int i, float, float, float g) { r.gain[start + size_t(i)] = g; },
                  [&](const TriggerEvent& t) {
                      if (t.isOpen) { ++r.opens; r.openAt.push_back(int(start) + t.sampleOffset); r.strengths.push_back(t.strength); }
                      else if (! t.rejected) ++r.closes;
                  });
    }
    return r;
}

static float peakDb(const std::vector<float>& x, double t0, double t1)
{
    float p = 0.0f;
    for (size_t i = size_t(t0 * sr); i < size_t(t1 * sr) && i < x.size(); ++i) p = std::max(p, std::fabs(x[i]));
    return gainToDb(p);
}

int main()
{
    Settings base;
    base.thresholdDb = -30.0f;
    base.hysteresisDb = 6.0f;
    base.attackMs = 0.2f;
    base.holdMs = 30.0f;
    base.releaseMs = 100.0f;
    base.rangeDb = -80.0f;
    base.lookaheadMs = 2.0f;
    base.keyHpfHz = 20.0f;
    base.keyLpfHz = 20000.0f;

    // 1. One hit: bleed removed before it, hit passes, exactly one trigger
    {
        auto x = drumTrack(1.0, { 0.25 });
        auto r = runGate(x, base);
        const double la = r.latency / sr;
        CHECK(r.opens == 1, "single hit -> one opening (got %d)", r.opens);
        CHECK(r.closes == 1, "single hit -> gate closes again (got %d)", r.closes);
        CHECK(peakDb(r.out, 0.05, 0.2) < -100.0f, "bleed before hit removed (%.1f dB)", peakDb(r.out, 0.05, 0.2));
        const float inHit = peakDb(r.in, 0.25, 0.30), outHit = peakDb(r.out, 0.25 + la, 0.30 + la);
        CHECK(std::fabs(inHit - outHit) < 0.5f, "hit passes intact (in %.2f, out %.2f dB)", inHit, outHit);
        CHECK(peakDb(r.out, 0.9, 1.0) < -100.0f, "bleed after release removed (%.1f dB)", peakDb(r.out, 0.9, 1.0));
    }

    // 2. Lookahead: gate is (nearly) fully open when the delayed transient reaches the output
    {
        auto x = drumTrack(0.5, { 0.1 });
        auto r = runGate(x, base);
        const size_t onsetOut = size_t(0.1 * sr) + size_t(r.latency) + 12; // a quarter-cycle in
        CHECK(r.gain[onsetOut] > 0.95f, "lookahead: gain at transient = %.3f", r.gain[onsetOut]);
        Settings noLa = base; noLa.lookaheadMs = 0.0f; noLa.attackMs = 5.0f;
        auto r2 = runGate(x, noLa);
        const size_t on2 = size_t(0.1 * sr) + size_t(r2.latency) + 12;
        CHECK(r2.gain[on2] < 0.5f, "no lookahead + slow attack: transient clipped (gain %.3f)", r2.gain[on2]);
    }

    // 3. Range: closed gate attenuates by exactly Range
    {
        auto x = drumTrack(0.5, {}, -40.0f);
        Settings s = base; s.rangeDb = -20.0f; s.thresholdDb = -10.0f;
        auto r = runGate(x, s);
        const float att = peakDb(r.out, 0.1, 0.5) - peakDb(r.in, 0.1, 0.5);
        CHECK(std::fabs(att + 20.0f) < 0.6f, "range -20 dB -> attenuation %.2f dB", att);
    }

    // 4. Retrigger guard: a flam 12 ms after a hit doesn't re-fire
    {
        auto x = drumTrack(1.0, { 0.2, 0.212 });
        Settings s = base; s.holdMs = 0.0f; s.releaseMs = 5.0f; s.hysteresisDb = 0.0f; s.thresholdDb = -12.0f;
        auto noGuard = runGate(x, s);
        s.retriggerMs = 40.0f;
        auto guarded = runGate(x, s);
        CHECK(guarded.opens <= 1 && guarded.opens <= noGuard.opens,
              "retrigger guard: %d openings with guard (vs %d without)", guarded.opens, noGuard.opens);
    }

    // 5. Hysteresis keeps the gate from chattering on a decaying tail
    {
        auto x = drumTrack(1.0, { 0.2 });
        Settings s = base; s.holdMs = 0.0f; s.releaseMs = 2.0f; s.hysteresisDb = 0.0f;
        auto chatter = runGate(x, s);
        s.hysteresisDb = 10.0f;
        auto steady = runGate(x, s);
        CHECK(steady.opens == 1, "hysteresis: one opening (no hysteresis gave %d)", chatter.opens);
    }

    // 6. Duck mode inverts: the hit is pushed down, the quiet part passes
    {
        auto x = drumTrack(1.0, { 0.25 }, -40.0f);
        Settings s = base; s.mode = GateMode::Duck; s.rangeDb = -18.0f;
        auto r = runGate(x, s);
        CHECK(std::fabs(peakDb(r.out, 0.05, 0.2) - peakDb(r.in, 0.05, 0.2)) < 0.1f, "duck: quiet part untouched");
        const double la = r.latency / sr;
        const float hitAtt = peakDb(r.out, 0.26 + la, 0.3 + la) - peakDb(r.in, 0.26, 0.3);
        CHECK(hitAtt < -17.0f, "duck: hit ducked by %.1f dB", hitAtt);
    }

    // 7. Transient mode rejects a slow swell that crosses the threshold; peak mode doesn't
    {
        std::vector<float> x(static_cast<size_t>(sr));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = double(i) / sr;
            const float amp = dbToGain(float(-60.0 + 50.0 * std::min(1.0, t / 0.8))); // -60 -> -10 dB over 800 ms
            x[i] = amp * float(std::sin(2.0 * M_PI * 150.0 * t));
        }
        Settings s = base;
        s.detect = DetectMode::Peak;
        auto peak = runGate(x, s);
        s.detect = DetectMode::Transient; s.transientSensDb = 6.0f;
        auto trans = runGate(x, s);
        CHECK(peak.opens >= 1, "peak mode opens on swell (%d)", peak.opens);
        CHECK(trans.opens == 0, "transient mode ignores swell (%d)", trans.opens);

        auto hits = drumTrack(1.0, { 0.3 });
        auto t2 = runGate(hits, s);
        CHECK(t2.opens == 1, "transient mode still catches a real hit (%d)", t2.opens);
    }

    // 8. Key filter: a low-frequency hit is ignored when the HPF is set above it
    {
        auto x = drumTrack(0.6, { 0.2 }); // 200 Hz hit
        Settings s = base; s.keyHpfHz = 2000.0f;
        auto r = runGate(x, s);
        CHECK(r.opens == 0, "key HPF at 2 kHz ignores a 200 Hz hit (%d)", r.opens);
    }

    // 9. Block size independence
    {
        auto x = drumTrack(1.0, { 0.2, 0.5, 0.8 });
        auto a = runGate(x, base, 64), b = runGate(x, base, 1024);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < x.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(a.out[i] - b.out[i]));
        CHECK(maxDiff < 1.0e-6f && a.opens == 3, "same result at block 64 and 1024 (diff %g, opens %d)", maxDiff, a.opens);
    }


    // 10. Two-band: with the high band closed harder, a bright noise floor is cut more than a low one
    {
        std::mt19937 rng(7);
        std::normal_distribution<float> nz(0.0f, 1.0f);
        std::vector<float> lowTone(static_cast<size_t>(sr)), hiss(static_cast<size_t>(sr));
        for (size_t i = 0; i < lowTone.size(); ++i)
        {
            lowTone[i] = 0.01f * float(std::sin(2.0 * M_PI * 100.0 * double(i) / sr));
            hiss[i] = 0.01f * nz(rng);
        }
        Settings s = base; s.thresholdDb = 0.0f; s.bandSplit = true; s.crossoverHz = 1000.0f;
        s.rangeDb = -6.0f; s.rangeHighDb = -40.0f;
        auto a = runGate(lowTone, s), b = runGate(hiss, s);
        const float lowAtt = peakDb(a.out, 0.3, 1.0) - peakDb(a.in, 0.3, 1.0);
        float hp = 0, hi = 0;
        for (size_t i = size_t(0.3 * sr); i < hiss.size(); ++i) { hp += b.out[i] * b.out[i]; hi += hiss[i] * hiss[i]; }
        const float hissAtt = 10.0f * std::log10(hp / hi);
        CHECK(std::fabs(lowAtt + 6.0f) < 1.0f, "two-band: 100 Hz tone follows low range (%.1f dB)", lowAtt);
        CHECK(hissAtt < -20.0f, "two-band: hiss mostly follows high range (%.1f dB)", hissAtt);

        // fully open, split vs unsplit: same level (LR4 sums flat)
        Settings o = base; o.thresholdDb = -80.0f; o.bandSplit = true;
        auto x = drumTrack(0.5, { 0.1 }, -50.0f, -6.0f, 1000.0, 0.3f);
        auto split = runGate(x, o);
        CHECK(std::fabs(peakDb(split.out, 0.1, 0.3) - peakDb(x, 0.1, 0.3)) < 1.0f, "two-band open is transparent (%.2f dB)",
              peakDb(split.out, 0.1, 0.3) - peakDb(x, 0.1, 0.3));
    }

    // 11. Adaptive decay: a harder hit keeps more of its tail than a soft one
    {
        Settings s = base; s.holdMs = 0.0f; s.releaseMs = 60.0f; s.hysteresisDb = 0.0f; s.thresholdDb = -30.0f;
        auto loud = drumTrack(0.8, { 0.1 }, -80.0f, -3.0f), soft = drumTrack(0.8, { 0.1 }, -80.0f, -24.0f);
        auto tailGain = [&](const Run& r) { size_t i = size_t(0.1 * sr); while (i < r.gain.size() && r.gain[i] < 0.5f) ++i;
                                            while (i < r.gain.size() && r.gain[i] > 0.5f) ++i;
                                            for (; i < r.gain.size() && r.gain[i] > 0.01f; ++i) {} return double(i) / sr - 0.1; };
        const double l0 = tailGain(runGate(loud, s)), s0 = tailGain(runGate(soft, s));
        s.adaptiveDecay = 1.0f;
        const double l1 = tailGain(runGate(loud, s)), s1 = tailGain(runGate(soft, s));
        CHECK(l1 > l0 && s1 < s0, "adaptive decay: loud tail %.0f->%.0f ms, soft %.0f->%.0f ms", l0 * 1e3, l1 * 1e3, s0 * 1e3, s1 * 1e3);
    }

    // 12. Leveller: two hits 12 dB apart end up much closer
    {
        auto x = drumTrack(1.0, { 0.1 }, -80.0f, -6.0f);
        auto y = drumTrack(1.0, { 0.5 }, -80.0f, -18.0f);
        for (size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        Settings s = base; s.levelAmount = 0.75f; s.levelTargetDb = -12.0f; s.holdMs = 0.0f; s.releaseMs = 50.0f;
        auto r = runGate(x, s);
        const double la = r.latency / sr;
        const float d0 = peakDb(x, 0.1, 0.2) - peakDb(x, 0.5, 0.6);
        const float d1 = peakDb(r.out, 0.1 + la, 0.2 + la) - peakDb(r.out, 0.5 + la, 0.6 + la);
        CHECK(r.opens == 2 && d1 < d0 * 0.4f, "leveller: hit difference %.1f dB -> %.1f dB", d0, d1);
    }

    // 13. Clipper: every hit's peak is shaved by about the same amount, whatever its level
    {
        auto x = drumTrack(1.0, { 0.1 }, -80.0f, -3.0f, 200.0, 0.0f);
        auto y = drumTrack(1.0, { 0.5 }, -80.0f, -15.0f, 200.0, 0.0f);
        for (size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        Settings s = base; s.clipDb = 4.0f; s.holdMs = 0.0f; s.releaseMs = 50.0f;
        auto r = runGate(x, s);
        const double la = r.latency / sr;
        const float c1 = peakDb(r.out, 0.1 + la, 0.2 + la) - peakDb(x, 0.1, 0.2);
        const float c2 = peakDb(r.out, 0.5 + la, 0.6 + la) - peakDb(x, 0.5, 0.6);
        CHECK(c1 < -1.5f && c2 < -1.5f && std::fabs(c1 - c2) < 1.0f, "clipper: loud hit %.1f dB, quiet hit %.1f dB", c1, c2);
    }

    // 14. Hit Match: learn the snare, then louder kick spill is rejected while snare hits (even quiet ones) pass
    {
        auto snareOnly = drumTrack(1.5, { 0.1, 0.4, 0.7, 1.0, 1.3 }, -80.0f, -12.0f, 220.0, 0.6f);
        Settings s = base; s.thresholdDb = -36.0f;
        Engine e;
        s.learn = LearnMode::Hits;
        runGate(snareOnly, s, 256, &e);
        const Profile learned = e.hitProfile;
        CHECK(learned.count == 5, "learned %d snare hits", learned.count);

        auto snare = drumTrack(2.0, { 0.2, 1.2 }, -80.0f, -12.0f, 220.0, 0.6f);
        auto ghost = drumTrack(2.0, { 1.6 }, -80.0f, -30.0f, 220.0, 0.6f);
        auto kick  = drumTrack(2.0, { 0.6, 0.9 }, -80.0f, -6.0f, 55.0, 0.0f); // spill louder than the snare
        for (size_t i = 0; i < snare.size(); ++i) snare[i] += ghost[i] + kick[i];

        s.learn = LearnMode::Off;
        auto plain = runGate(snare, s);
        s.hitMatch = true; s.matchToleranceDb = 4.0f;
        // a fresh engine with the learned profile restored, as the plugin does from its saved state
        Engine m2; m2.hitProfile = learned;
        auto r = runGate(snare, s, 256, &m2);
        CHECK(plain.opens == 5, "threshold-only gate opens on all 5 (snare, ghost, kick spill): %d", plain.opens);
        bool kickRejected = true, snareKept = r.opens == 3;
        for (int at : r.openAt) { const double t = at / sr; if ((t > 0.55 && t < 0.7) || (t > 0.85 && t < 1.0)) kickRejected = false; }
        CHECK(kickRejected && snareKept, "hit match: %d openings, kick spill rejected=%d", r.opens, int(kickRejected));

        // Learning the spill instead works too
        Engine sp; s.hitMatch = false; s.learn = LearnMode::Spill;
        runGate(kick, s, 256, &sp);
        Engine sp2; sp2.spillProfile = sp.spillProfile;
        s.learn = LearnMode::Off; s.hitMatch = true;
        auto r2 = runGate(snare, s, 256, &sp2);
        CHECK(r2.opens == 3, "spill profile: kick rejected, snare kept (%d openings)", r2.opens);
    }

    // 15. MIDI trigger: the gate opens on notes, ignoring the audio level, aligned with the delayed audio
    {
        auto x = drumTrack(1.0, { 0.2, 0.6 }, -40.0f);
        Settings s = base; s.triggerFromMidi = true; s.thresholdDb = 0.0f; // audio would never open it
        const int note = int(0.2 * sr);
        auto r = runGate(x, s, 256, nullptr, { { note, 0.8f } });
        CHECK(r.opens == 1, "MIDI: one note -> one opening (%d)", r.opens);
        CHECK(r.strengths.size() == 1 && std::fabs(r.strengths[0] - 0.8f) < 1e-6f, "MIDI: velocity carried through");
        const size_t at = size_t(note + r.latency) + 12;
        CHECK(r.gain[at] > 0.95f && r.gain[size_t(note + r.latency - int(base.lookaheadMs * sr / 1000.0)) - 10] < 0.05f,
              "MIDI: gate opens just before the hit reaches the output");
    }

    // 16. Align Only (gate bypass): audio passes untouched apart from the fixed delay, no triggers
    {
        auto x = drumTrack(1.0, { 0.25 }, -40.0f);
        Settings s = base; s.gateBypass = true; s.punchDb = 6.0f; s.levelAmount = 1.0f; s.clipDb = 6.0f; s.bandSplit = true;
        auto r = runGate(x, s);
        float maxErr = 0.0f;
        for (size_t i = size_t(r.latency) + 2000; i < x.size(); ++i)
            maxErr = std::max(maxErr, std::fabs(r.out[i] - x[i - size_t(r.latency)]));
        CHECK(r.opens == 0 && maxErr < 1e-3f, "gate bypass: no triggers, output = delayed input (max err %.2g)", maxErr);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
