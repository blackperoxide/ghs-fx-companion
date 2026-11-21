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
};

// mono signal: low bleed everywhere + decaying 200 Hz hits at the given times
static std::vector<float> drumTrack(double seconds, std::vector<double> hitTimes, float bleedDb = -50.0f, float hitDb = -6.0f)
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
                v += hit * float(std::exp(-dt * 25.0) * std::sin(2.0 * M_PI * 200.0 * dt));
        }
        x[i] = v;
    }
    return x;
}

static Run runGate(const std::vector<float>& input, const Settings& s, int block = 256)
{
    Engine e;
    e.prepare(sr, 1);
    Run r;
    r.in = input;
    r.out = input;
    r.gain.resize(input.size());
    for (size_t start = 0; start < input.size(); start += size_t(block))
    {
        const int n = int(std::min(input.size() - start, size_t(block)));
        float* chans[1] = { r.out.data() + start };
        e.process(chans, 1, input.data() + start, n, s,
                  [&](int i, float, float, float g) { r.gain[start + size_t(i)] = g; },
                  [&](const TriggerEvent& t) {
                      if (t.isOpen) { ++r.opens; r.openAt.push_back(int(start) + t.sampleOffset); }
                      else ++r.closes;
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
        const double la = base.lookaheadMs / 1000.0;
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
        const size_t onsetOut = size_t((0.1 + base.lookaheadMs / 1000.0) * sr) + 12; // a quarter-cycle in
        CHECK(r.gain[onsetOut] > 0.95f, "lookahead: gain at transient = %.3f", r.gain[onsetOut]);
        Settings noLa = base; noLa.lookaheadMs = 0.0f; noLa.attackMs = 5.0f;
        auto r2 = runGate(x, noLa);
        CHECK(r2.gain[size_t(0.1 * sr) + 12] < 0.5f, "no lookahead + slow attack: transient clipped (gain %.3f)", r2.gain[size_t(0.1 * sr) + 12]);
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
        const float hitAtt = peakDb(r.out, 0.26, 0.3) - peakDb(r.in, 0.26, 0.3);
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

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
