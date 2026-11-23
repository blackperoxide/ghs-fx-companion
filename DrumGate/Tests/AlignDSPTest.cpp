// Behavioural tests for multi-mic alignment (AlignDSP.h). No JUCE.
#include "AlignDSP.h"
#include <cstdio>
#include <random>

using namespace GHSAlign;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } \
                              else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

constexpr double sr = 48000.0;

static std::vector<double> toD(const std::vector<float>& x) { return std::vector<double>(x.begin(), x.end()); }
static std::vector<float> toF(const std::vector<double>& x) { return std::vector<float>(x.begin(), x.end()); }

// A drum source: decaying tone + noise burst, hits at the given times
static std::vector<double> drum(double seconds, std::vector<double> hits, double freq, double noise, double decay, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nz(0.0, 1.0);
    std::vector<double> x(size_t(seconds * sr), 0.0);
    for (double t0 : hits)
        for (size_t i = size_t(t0 * sr); i < x.size() && i < size_t((t0 + 0.6) * sr); ++i)
        {
            const double dt = double(i) / sr - t0;
            x[i] += std::exp(-dt * decay) * std::sin(2 * M_PI * freq * dt * (1.0 + 0.5 * std::exp(-dt * 30)))
                  + noise * std::exp(-dt * decay * 2.0) * nz(rng);
        }
    return x;
}

static void addInto(std::vector<double>& y, const std::vector<double>& x, double gain, double delaySamples)
{
    const auto s = shifted(x, delaySamples);
    for (size_t i = 0; i < y.size() && i < s.size(); ++i) y[i] += gain * s[i];
}

// Ideal constant phase rotation (frequency domain) - what a "phase-shifted mic" looks like in the test
static std::vector<double> rotateIdeal(const std::vector<double>& x, double rad)
{
    const size_t n = nextPow2(x.size());
    std::vector<cd> X(n);
    for (size_t i = 0; i < x.size(); ++i) X[i] = x[i];
    fft(X, false);
    const cd pos = std::polar(1.0, -rad), neg = std::polar(1.0, rad);
    for (size_t k = 1; k < n / 2; ++k) { X[k] *= pos; X[n - k] *= neg; }
    fft(X, true);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = X[i].real();
    return y;
}

int main()
{
    // 1. Hilbert pair: 90 degrees apart, equal level, across the band
    {
        bool ok = true;
        for (double f : { 60.0, 200.0, 1000.0, 5000.0, 12000.0 })
        {
            HilbertPair h;
            double sII = 0, sQQ = 0, sIQ = 0;
            for (int i = 0; i < 48000; ++i)
            {
                double I, Q;
                h.process(std::sin(2 * M_PI * f * i / sr), I, Q);
                if (i > 24000) { sII += I * I; sQQ += Q * Q; sIQ += I * Q; }
            }
            const double c = sIQ / std::sqrt(sII * sQQ), lvl = 10 * std::log10(sQQ / sII);
            if (std::fabs(c) > 0.02 || std::fabs(lvl) > 0.1) { ok = false; std::printf("   %g Hz: corr %.3f, level %.2f dB\n", f, c, lvl); }
        }
        CHECK(ok, "Hilbert pair is 90 degrees apart and level-matched 60 Hz - 12 kHz");
    }

    // 2. Fractional delay is accurate
    {
        auto x = drum(1.0, { 0.2 }, 200, 0.5, 20, 1);
        auto y = shifted(shifted(x, 12.37), -12.37);
        const double c = correlation(x, y, 1000, x.size() - 1000);
        CHECK(c > 0.995, "fractional shift round trip, full-band noise (corr %.5f)", c);
    }

    // 3. Pure delays, both directions, sub-sample
    {
        auto ref = drum(3.0, { 0.2, 0.7, 1.2, 1.7, 2.2 }, 180, 0.6, 18, 2);
        for (double d : { 37.3, -22.6, 0.4, 150.0 })
        {
            auto tgt = shifted(ref, d);
            auto e = estimateLag(ref, tgt, 480);
            CHECK(std::fabs(e.lag - d) < 0.15, "lag %.1f samples estimated as %.2f", d, e.lag);
        }
        auto far = shifted(ref, 700.0);
        CHECK(estimateLag(ref, far, 480).atLimit, "lag beyond the search range is flagged");
    }

    // 4. Polarity: inverted + delayed mic -> phase 180
    {
        auto ref = drum(3.0, { 0.3, 1.1, 1.9 }, 220, 0.6, 20, 3);
        auto tgt = shifted(ref, 25.0);
        for (auto& v : tgt) v *= -0.5;
        auto r = analyse(toF(ref), toF(tgt), sr, 480, false);
        CHECK(std::fabs(r.lagSamples - 25.0) < 0.2 && std::fabs(r.phaseRad - M_PI) < 1e-9 && r.corrAfter > 0.98,
              "inverted mic: lag %.2f, phase %.0f deg, corr %.3f -> %.3f", r.lagSamples, r.phaseRad * 180 / M_PI, r.corrBefore, r.corrAfter);
    }

    // 5. Phase rotation: a 70 degree rotated mic is matched far better with Phase than with polarity only
    {
        auto ref = drum(3.0, { 0.3, 1.1, 1.9 }, 150, 0.4, 15, 4);
        auto tgt = rotateIdeal(shifted(ref, -14.0), 70.0 * M_PI / 180.0);
        auto pol = analyse(toF(ref), toF(tgt), sr, 480, false);
        auto ph  = analyse(toF(ref), toF(tgt), sr, 480, true);
        CHECK(std::fabs(ph.lagSamples + 14.0) < 1.0, "phase case: lag %.2f", ph.lagSamples);
        CHECK(ph.corrAfter > 0.97 && ph.corrAfter > pol.corrAfter + 0.1,
              "phase: corr %.3f (polarity only %.3f), rotation %.0f deg", ph.corrAfter, pol.corrAfter, ph.phaseRad * 180 / M_PI);
    }

    // 6. A realistic kit: overheads as reference, close mics with bleed, a flipped snare bottom
    {
        const double secs = 4.0;
        std::vector<double> hitsK, hitsS, hitsH;
        for (double t = 0.1; t < secs - 0.6; t += 0.5) { hitsK.push_back(t); hitsS.push_back(t + 0.25); }
        for (double t = 0.1; t < secs - 0.6; t += 0.125) hitsH.push_back(t + 0.01);
        auto kick  = drum(secs, hitsK, 55, 0.2, 12, 5);
        auto snare = drum(secs, hitsS, 200, 1.0, 22, 6);
        auto hat   = drum(secs, hitsH, 9000, 1.5, 60, 7);
        const double ms = sr / 1000.0;

        std::vector<double> oh(kick.size()), kickIn(kick.size()), snTop(kick.size()), snBot(kick.size());
        addInto(oh, kick, 0.5, 3.6 * ms); addInto(oh, snare, 0.6, 2.9 * ms); addInto(oh, hat, 0.4, 2.2 * ms);
        addInto(kickIn, kick, 1.0, 0.15 * ms); addInto(kickIn, snare, 0.1, 1.2 * ms);
        addInto(snTop, snare, 1.0, 0.1 * ms); addInto(snTop, hat, 0.3, 0.6 * ms); addInto(snTop, kick, 0.15, 1.0 * ms);
        addInto(snBot, snare, -0.7, 0.45 * ms); addInto(snBot, kick, 0.1, 0.9 * ms);

        struct Mic { const char* name; std::vector<double>* sig; double expectLagMs; bool flipped; };
        Mic mics[] = { { "kick in", &kickIn, 0.15 - 3.6, false }, { "snare top", &snTop, 0.1 - 2.9, false },
                       { "snare bottom", &snBot, 0.45 - 2.9, true } };
        for (bool usePhase : { false, true })
            for (auto& m : mics)
            {
                auto r = analyse(toF(oh), toF(*m.sig), sr, 480, usePhase);
                const double lagMs = r.lagSamples / ms;
                const bool flipped = std::cos(r.phaseRad) < -0.7;
                CHECK(std::fabs(lagMs - m.expectLagMs) < 0.1 && flipped == m.flipped,
                      "kit (%s): %s vs overheads %.2f ms (expected %.2f), %.0f deg", usePhase ? "phase" : "polarity",
                      m.name, lagMs, m.expectLagMs, r.phaseRad * 180 / M_PI);
            }

        // snare bottom vs snare top directly (the classic case)
        auto r = analyse(toF(snTop), toF(snBot), sr, 480, false);
        CHECK(std::fabs(r.lagSamples / ms - 0.35) < 0.08 && std::fabs(r.phaseRad - M_PI) < 0.1,
              "snare bottom vs top: %.2f ms, phase %.0f deg", r.lagSamples / ms, r.phaseRad * 180 / M_PI);
    }

    // 7. Realtime aligner reproduces the offline result: reference and target come out matched
    {
        auto ref = drum(3.0, { 0.3, 1.1, 1.9 }, 150, 0.5, 15, 8);
        auto tgt = rotateIdeal(shifted(ref, -31.5), 2.4);
        for (bool usePhase : { false, true })
        {
            auto res = analyse(toF(ref), toF(tgt), sr, 480, usePhase);
            const double base = 480 + Aligner::minDelay;
            Aligner aR, aT;
            aR.prepare(1, 1200); aT.prepare(1, 1200);
            aR.set(base, 0.0, usePhase);
            aT.set(base - res.lagSamples, res.phaseRad, usePhase);
            auto fr = toF(ref), ft = toF(tgt);
            for (size_t s = 0; s < fr.size(); s += 512)
            {
                const int n = int(std::min<size_t>(512, fr.size() - s));
                float* pr[1] = { fr.data() + s }; float* pt[1] = { ft.data() + s };
                aR.process(pr, 1, n); aT.process(pt, 1, n);
            }
            const double before = correlation(ref, tgt, 2000, ref.size() - 2000);
            const double after = correlation(toD(fr), toD(ft), 2000, fr.size() - 2000);
            CHECK(after > (usePhase ? 0.97 : 0.5) && after > before, "realtime aligner (%s): corr %.3f -> %.3f",
                  usePhase ? "phase" : "polarity", before, after);
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
