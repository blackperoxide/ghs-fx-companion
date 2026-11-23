#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "GateDSP.h"

/**
 * Multi-mic time / polarity / phase alignment - the DSP half, JUCE-free so it
 * can be unit-tested (DrumGate/Tests/AlignDSPTest.cpp).
 *
 * The idea: every mic on the kit hears the same hits, just at slightly
 * different times (distance), sometimes inverted (a bottom-snare mic, a
 * kick-in vs kick-out), and with some phase rotation. Lining them up to one
 * reference (usually the overheads) gives a tighter low end and punchier
 * shells before any EQ.
 *
 *  1. Time: generalised cross-correlation with phase transform (GCC-PHAT)
 *     between each mic and the reference gives a sharp peak at the arrival
 *     difference even when the two mics' tones are very different (a close
 *     snare vs an overhead full of cymbals). Refined to a fraction of a
 *     sample with a parabolic fit.
 *  2. Polarity / phase: after the time shift, the best constant phase
 *     rotation is found in closed form using a Hilbert (90 degree) allpass
 *     pair: out = cos(phi) * I + sin(phi) * Q. phi = 180 degrees is a plain
 *     polarity flip. In "polarity only" mode phi is restricted to 0 / 180.
 *  3. Realtime: every aligned instance delays its output by
 *     (fixed base - its lag), so mics that arrive early are delayed and mics
 *     that arrive late are "advanced" relative to the fixed base. All
 *     aligned instances report the same latency (the base), so the host's
 *     delay compensation keeps the rest of the session in time.
 */
namespace GHSAlign
{

using cd = std::complex<double>;

/** In-place iterative radix-2 FFT. size must be a power of two. */
inline void fft(std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * M_PI / double(len) * (inverse ? 1.0 : -1.0);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= double(n);
}

inline size_t nextPow2(size_t n) { size_t p = 1; while (p < n) p <<= 1; return p; }

/**
 * IIR Hilbert transformer: two chains of 2nd-order allpasses whose outputs
 * are ~90 degrees apart from ~20 Hz to ~20 kHz at 44.1/48 kHz (Olli
 * Niemitalo's coefficients). Both outputs have the same (flat) magnitude.
 */
struct HilbertPair
{
    static constexpr double c1[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double c2[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    struct Section { double a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double process(double x) { const double y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };

    Section s1[4], s2[4];
    double delayed = 0.0;

    HilbertPair() { reset(); }
    void reset()
    {
        for (int i = 0; i < 4; ++i) { s1[i] = Section {}; s1[i].a2 = c1[i] * c1[i]; s2[i] = Section {}; s2[i].a2 = c2[i] * c2[i]; }
        delayed = 0.0;
    }

    /** I = in-phase (allpass), Q = the same allpass rotated by 90 degrees. */
    void process(double x, double& I, double& Q)
    {
        double a = x, b = x;
        for (auto& s : s1) a = s.process(a);
        for (auto& s : s2) b = s.process(b);
        I = delayed; // chain 1, one sample later
        delayed = a;
        Q = b;
    }
};

/**
 * Windowed-sinc fractional delay kernel: 32 taps, Blackman-Harris window.
 * Flat to ~18 kHz at 44.1/48 kHz (a cubic Lagrange would audibly dull the
 * cymbals on a fractional delay). Taps cover delays (i - 15) .. (i + 16)
 * where i = floor(delay); weights normalised for exact DC gain.
 */
static constexpr int sincTaps = 32;
static constexpr int sincHalf = 16;

inline double windowedSinc(double x)
{
    if (std::fabs(x) >= double(sincHalf)) return 0.0;
    const double s = std::fabs(x) < 1e-9 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
    const double u = (x + sincHalf) / (2.0 * sincHalf); // 0..1 across the window
    const double w = 0.35875 - 0.48829 * std::cos(2 * M_PI * u) + 0.14128 * std::cos(4 * M_PI * u) - 0.01168 * std::cos(6 * M_PI * u);
    return s * w;
}

inline void sincWeights(double delay, int& firstOffset, double h[sincTaps])
{
    const int i = int(std::floor(delay));
    firstOffset = i - (sincHalf - 1);
    double sum = 0.0;
    for (int t = 0; t < sincTaps; ++t) { h[t] = windowedSinc(delay - double(firstOffset + t)); sum += h[t]; }
    for (int t = 0; t < sincTaps; ++t) h[t] /= sum;
}

/** Offline fractional shift: y[n] = x(n - d), d may be negative; zero outside the signal. */
inline std::vector<double> shifted(const std::vector<double>& x, double d)
{
    std::vector<double> y(x.size(), 0.0);
    int first;
    double h[sincTaps];
    sincWeights(d, first, h);
    const int n = int(x.size());
    for (int k = 0; k < n; ++k)
    {
        double acc = 0.0;
        for (int t = 0; t < sincTaps; ++t)
        {
            const int idx = k - (first + t);
            if (idx >= 0 && idx < n) acc += h[t] * x[size_t(idx)];
        }
        y[size_t(k)] = acc;
    }
    return y;
}

inline double correlation(const std::vector<double>& a, const std::vector<double>& b, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > a.size()) to = std::min(a.size(), b.size());
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < to; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return (aa <= 0 || bb <= 0) ? 0.0 : ab / std::sqrt(aa * bb);
}

/** High-passes a copy (removes rumble/DC that would otherwise dominate the correlation). */
inline std::vector<double> conditioned(const std::vector<float>& x, double sr)
{
    GHSGate::Biquad h1, h2;
    h1.setHighPass(sr, 40.0); h2.setHighPass(sr, 40.0);
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = h2.process(h1.process(x[i]));
    return y;
}

struct LagEstimate
{
    double lag = 0.0;     // samples; positive = target arrives LATER than the reference
    double peak = 0.0;    // GCC-PHAT peak height (confidence, roughly 0..1)
    bool atLimit = false; // best lag was at the search boundary (probably out of range)
    std::vector<int> candidates; // the strongest few correlation peaks (integer lags), best first
};

/** GCC-PHAT lag between reference and target, searched within +-maxLag samples. */
inline LagEstimate estimateLag(const std::vector<double>& ref, const std::vector<double>& tgt, int maxLag, double beta = 0.7)
{
    const size_t n = std::min(ref.size(), tgt.size());
    const size_t size = nextPow2(2 * n);
    std::vector<cd> R(size), T(size);
    for (size_t i = 0; i < n; ++i) { R[i] = ref[i]; T[i] = tgt[i]; }
    fft(R, false);
    fft(T, false);
    for (size_t i = 0; i < size; ++i)
    {
        const cd g = T[i] * std::conj(R[i]);
        const double m = std::abs(g);
        T[i] = m > 1e-20 ? g / std::pow(m, beta) : cd(0.0, 0.0);
    }
    fft(T, true);

    auto at = [&](int k) { return (k >= 0 ? T[size_t(k)] : T[size - size_t(-k)]).real(); };

    // Search twice the allowed range, so a true lag outside it is recognised
    // as such instead of being mistaken for the best lag inside it.
    const int searchLag = std::min<int>(2 * maxLag, int(size / 2) - sincHalf - 2);
    int best = 0;
    double bestV = -1.0, sum = 0.0;
    for (int k = -searchLag; k <= searchLag; ++k)
    {
        const double v = std::fabs(at(k));
        sum += v;
        if (v > bestV) { bestV = v; best = k; }
    }

    LagEstimate e;
    e.atLimit = std::abs(best) > maxLag;

    // the strongest local maxima within the allowed range (other plausible alignments)
    {
        std::vector<std::pair<double, int>> peaks;
        for (int k = -maxLag + 1; k < maxLag; ++k)
        {
            const double v = std::fabs(at(k));
            if (v >= std::fabs(at(k - 1)) && v >= std::fabs(at(k + 1))) peaks.push_back({ v, k });
        }
        std::sort(peaks.begin(), peaks.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < peaks.size() && i < 4; ++i) e.candidates.push_back(peaks[i].second);
    }

    // Sub-sample: band-limited (sinc) interpolation of the correlation around the peak
    auto interp = [&](double tau)
    {
        double acc = 0.0;
        for (int k = int(std::floor(tau)) - sincHalf + 1; k <= int(std::floor(tau)) + sincHalf; ++k)
            acc += at(k) * windowedSinc(tau - double(k));
        return std::fabs(acc);
    };
    double bestTau = best, bestI = interp(best);
    for (double tau = best - 1.0; tau <= best + 1.0; tau += 0.01)
        if (const double v = interp(tau); v > bestI) { bestI = v; bestTau = tau; }
    for (double tau = bestTau - 0.01; tau <= bestTau + 0.01; tau += 0.0005)
        if (const double v = interp(tau); v > bestI) { bestI = v; bestTau = tau; }
    e.lag = std::clamp(bestTau, double(-maxLag), double(maxLag));

    const double mean = sum / double(2 * searchLag + 1);
    e.peak = bestV / std::max(1e-20, bestV + mean * 8.0); // how far the peak stands above the floor
    return e;
}

/**
 * Focus on the moments where the target mic's own drum is playing: a smooth
 * 0..1 weight from the target's envelope (within ~18 dB of its loudest hits,
 * held for 30 ms). Without this, a kick mic's snare bleed - bright and noisy -
 * can dominate the correlation and the kick mic gets aligned to the snare.
 */
inline std::vector<double> focusMask(const std::vector<double>& tgt, double sr)
{
    std::vector<double> env(tgt.size());
    const double rel = std::exp(-1.0 / (sr * 0.010));
    const int hold = int(sr * 0.030);
    double e = 0.0, peak = 0.0;
    int holdLeft = 0;
    for (size_t i = 0; i < tgt.size(); ++i)
    {
        const double a = std::fabs(tgt[i]);
        if (a >= e) { e = a; holdLeft = hold; }
        else if (holdLeft > 0) --holdLeft;
        else e *= rel;
        env[i] = e;
        peak = std::max(peak, e);
    }
    // smooth the hold edges symmetrically so the mask doesn't favour either lag direction
    std::vector<double> m(tgt.size());
    const double floorDb = 20.0 * std::log10(std::max(peak, 1e-12)) - 18.0;
    for (size_t i = 0; i < tgt.size(); ++i)
    {
        const double db = 20.0 * std::log10(std::max(env[i], 1e-12));
        m[i] = std::clamp((db - floorDb) / 6.0, 0.0, 1.0);
    }
    const int pre = hold / 2; // extend backwards too (the envelope only reacts after the onset)
    std::vector<double> out(m);
    for (size_t i = 0; i < m.size(); ++i)
        for (int k = 1; k <= pre && i + size_t(k) < m.size(); k += 8)
            out[i] = std::max(out[i], m[i + size_t(k)]);
    return out;
}

struct Result
{
    double lagSamples = 0.0;     // target relative to reference (positive = later)
    double phaseRad = 0.0;       // rotation to apply to the target (pi = polarity flip)
    double corrBefore = 0.0;     // normalised correlation with the reference as recorded
    double corrAfter = 0.0;      // ... after alignment
    double confidence = 0.0;
    bool outOfRange = false;
};

/**
 * Full analysis of one target against the reference.
 *  usePhase = false: only time + polarity (phaseRad is 0 or pi).
 */
inline Result analyse(const std::vector<float>& refIn, const std::vector<float>& tgtIn, double sr, int maxLag, bool usePhase)
{
    Result r;
    auto ref = conditioned(refIn, sr);
    auto tgt = conditioned(tgtIn, sr);
    const auto mask = focusMask(tgt, sr);
    for (size_t i = 0; i < std::min(ref.size(), tgt.size()); ++i) { ref[i] *= mask[i]; tgt[i] *= mask[i]; }
    r.corrBefore = correlation(ref, tgt);

    const auto lag = estimateLag(ref, tgt, maxLag);
    const auto plain = estimateLag(ref, tgt, maxLag, 0.0); // unwhitened: favours the loud, low drum body
    r.lagSamples = lag.lag;
    r.outOfRange = lag.atLimit;
    r.confidence = lag.peak;

    const size_t n = std::min(ref.size(), tgt.size());
    const size_t guard = std::min(n / 2, size_t(maxLag) + size_t(sincTaps));
    const size_t end = n - guard;

    // Reference through the same allpass the realtime path uses (only needed with phase)
    std::vector<double> Ir;
    if (usePhase)
    {
        HilbertPair hr;
        Ir.resize(ref.size());
        double dummy;
        for (size_t i = 0; i < ref.size(); ++i) hr.process(ref[i], Ir[i], dummy);
    }

    // Score one candidate lag: the best polarity / rotation in closed form, and the resulting match
    auto evaluate = [&](double lagS, double& phase) -> double
    {
        const auto t = shifted(tgt, -lagS); // offline we can look ahead
        if (! usePhase)
        {
            const double c = correlation(ref, t, guard, end);
            phase = c < 0.0 ? M_PI : 0.0;
            return std::fabs(c);
        }
        // Compare I(ref) against cos*I(tgt) + sin*Q(tgt)
        HilbertPair ht;
        std::vector<double> It(t.size()), Qt(t.size());
        for (size_t i = 0; i < t.size(); ++i) ht.process(t[i], It[i], Qt[i]);
        double sIQ = 0, sII = 0;
        for (size_t i = guard; i < end; ++i) { sIQ += Ir[i] * Qt[i]; sII += Ir[i] * It[i]; }
        phase = std::atan2(sIQ, sII);
        const double c = std::cos(phase), sn = std::sin(phase);
        std::vector<double> rot(t.size());
        for (size_t i = 0; i < t.size(); ++i) rot[i] = c * It[i] + sn * Qt[i];
        return correlation(Ir, rot, guard, end);
    };

    // The arrival time is settled first with time + polarity only: a phase
    // rotation on a tonal drum (a 55 Hz kick) can imitate a time shift of a
    // millisecond or more, and given that freedom the search would happily
    // trade real delay for rotation to line up the bleed as well. Phase mode
    // then refines rotation (and the lag by at most +-1.5 samples) on top.
    const bool wantPhase = usePhase;
    usePhase = false;
    double bestPhase = 0.0, bestLag = r.lagSamples;
    double bestScore = evaluate(bestLag, bestPhase);

    // A narrow-band drum (a 50 Hz kick) gives a weak whitened peak, and bleed
    // from a brighter drum can out-shout it. Try every plausible peak from
    // both correlations and keep whichever actually lines the signals up best.
    std::vector<double> tried { bestLag };
    auto tryLag = [&](double l)
    {
        for (double t : tried) if (std::fabs(t - l) < 0.75) return;
        tried.push_back(l);
        double ph;
        const double sc = evaluate(l, ph);
        if (sc > bestScore) { bestScore = sc; bestLag = l; bestPhase = ph; }
    };
    tryLag(plain.lag);
    for (int c : lag.candidates) tryLag(c);
    for (int c : plain.candidates) tryLag(c);

    auto refine = [&](double span)
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            const double step = pass == 0 ? 0.5 : pass == 1 ? 0.1 : 0.02;
            const double centre = bestLag;
            const double width = pass == 0 ? span : step * 5.0;
            for (double l = centre - width; l <= centre + width + 1e-9; l += step)
            {
                if (std::fabs(l) > maxLag) continue;
                double ph;
                const double sc = evaluate(l, ph);
                if (sc > bestScore) { bestScore = sc; bestLag = l; bestPhase = ph; }
            }
        }
    };
    refine(1.0);

    if (wantPhase)
    {
        usePhase = true;
        bestScore = evaluate(bestLag, bestPhase);
        refine(1.5);

        // Where the whitened correlation's peak disagrees with the time-domain
        // lag, the difference may really be a phase rotation (it's what
        // GCC-PHAT is least fooled by). Take it only if it explains the
        // signal clearly better - otherwise keep the physical arrival time.
        if (std::fabs(lag.lag - bestLag) > 1.5)
        {
            const double keepLag = bestLag, keepPhase = bestPhase, keepScore = bestScore;
            bestLag = lag.lag;
            bestScore = evaluate(bestLag, bestPhase);
            refine(1.5);
            if (bestScore < keepScore + 0.1) { bestLag = keepLag; bestPhase = keepPhase; bestScore = keepScore; }
        }
    }
    r.lagSamples = bestLag;
    r.phaseRad = bestPhase;
    r.corrAfter = bestScore;
    return r;
}

/**
 * Realtime per-channel aligner: fractional delay (cubic Lagrange) followed by
 * either a polarity sign or a Hilbert phase rotation. Allocation happens in
 * prepare() only.
 */
class Aligner
{
public:
    /** Smallest delay the aligner can produce (the sinc kernel needs this many samples of history). */
    static constexpr int minDelay = sincHalf;

    void prepare(int numChannels, int maxDelaySamples)
    {
        capacity = maxDelaySamples + sincTaps + 8;
        lines.assign(size_t(std::max(1, numChannels)), std::vector<float>(size_t(capacity), 0.0f));
        hilbert.assign(lines.size(), HilbertPair {});
        pos = 0;
    }

    void reset()
    {
        for (auto& l : lines) std::fill(l.begin(), l.end(), 0.0f);
        for (auto& h : hilbert) h.reset();
        pos = 0;
    }

    /** delaySamples >= minDelay. With usePhase the output is rotated by phaseRad; otherwise only its sign is used. */
    void set(double delaySamples, double phaseRad, bool usePhase)
    {
        const double d = std::clamp(delaySamples, double(minDelay), double(capacity - sincTaps - 2));
        if (usePhase != useHilbert)
            for (auto& h : hilbert) h.reset();
        useHilbert = usePhase;
        cosP = std::cos(phaseRad);
        sinP = std::sin(phaseRad);
        sign = cosP < 0.0 ? -1.0f : 1.0f;
        double h[sincTaps];
        sincWeights(d, firstOffset, h);
        for (int i = 0; i < sincTaps; ++i) w[i] = float(h[i]);
    }

    void process(float* const* audio, int numChannels, int numSamples)
    {
        const int chans = std::min(numChannels, int(lines.size()));
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = lines[size_t(ch)];
                line[size_t(pos)] = audio[ch][i];
                float y = 0.0f;
                int idx = pos - firstOffset;
                if (idx < 0) idx += capacity;
                for (int t = 0; t < sincTaps; ++t)
                {
                    y += w[t] * line[size_t(idx)];
                    if (--idx < 0) idx += capacity;
                }
                if (useHilbert)
                {
                    double I, Q;
                    hilbert[size_t(ch)].process(y, I, Q);
                    audio[ch][i] = float(cosP * I + sinP * Q);
                }
                else
                    audio[ch][i] = sign * y;
            }
            pos = (pos + 1) % capacity;
        }
    }

private:
    std::vector<std::vector<float>> lines;
    std::vector<HilbertPair> hilbert;
    int capacity = 64, pos = 0, firstOffset = 0;
    double cosP = 1.0, sinP = 0.0;
    float w[sincTaps] {};
    float sign = 1.0f;
    bool useHilbert = false;
};

} // namespace GHSAlign
