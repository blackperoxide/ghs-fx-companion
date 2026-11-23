// End-to-end test of cross-instance alignment: four real plugin instances
// (overheads, kick in, snare top, snare bottom) on a synthetic kit with known
// mic distances, a fake host playhead, Analyse through the shared registry,
// then a second pass checking the aligned outputs actually line up.
// Usage: GHSDrumGateAlignE2E [ref|auto|auto-phase] [screenshot.png]
#include "GateProcessor.h"
#include <random>
using namespace GHSAlign;

struct FakePlayHead : juce::AudioPlayHead {
    juce::int64 t = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setTimeInSamples(t); return p; }
};
static void setP(GHSDrumGateProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); }

static std::vector<double> drum(double secs, std::vector<double> hits, double f, double noise, double decay, unsigned seed, double sr) {
    std::mt19937 rng(seed); std::normal_distribution<double> nz(0, 1); std::vector<double> x(size_t(secs * sr));
    for (double t0 : hits) for (size_t i = size_t(t0 * sr); i < x.size() && i < size_t((t0 + 0.6) * sr); ++i) { double dt = i / sr - t0;
        x[i] += std::exp(-dt * decay) * std::sin(2 * M_PI * f * dt * (1 + 0.5 * std::exp(-dt * 30))) + noise * std::exp(-dt * decay * 2) * nz(rng); }
    return x; }
static void addInto(std::vector<double>& y, const std::vector<double>& x, double g, double d) { auto s = shifted(x, d); for (size_t i = 0; i < y.size(); ++i) y[i] += g * s[i]; }

int main(int argc, char** argv)
{
    const bool autoRef = argc > 1 && std::string(argv[1]).find("auto") != std::string::npos;
    const bool usePhase = argc > 1 && std::string(argv[1]).find("phase") != std::string::npos;
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000, ms = sr / 1000; const int bs = 512; const double secs = 12;
    std::vector<double> hk, hs, hh;
    for (double t = 0.1; t < secs - 0.6; t += 0.5) { hk.push_back(t); hs.push_back(t + 0.25); }
    for (double t = 0.1; t < secs - 0.6; t += 0.125) hh.push_back(t + 0.01);
    auto kick = drum(secs, hk, 55, 0.2, 12, 5, sr), snare = drum(secs, hs, 200, 1.0, 22, 6, sr), hat = drum(secs, hh, 9000, 1.5, 60, 7, sr);
    std::vector<double> oh(kick.size()), kin(kick.size()), stop(kick.size()), sbot(kick.size());
    addInto(oh, kick, 0.5, 3.6 * ms); addInto(oh, snare, 0.6, 2.9 * ms); addInto(oh, hat, 0.4, 2.2 * ms);
    addInto(kin, kick, 1.0, 0.15 * ms); addInto(kin, snare, 0.1, 1.2 * ms);
    addInto(stop, snare, 1.0, 0.1 * ms); addInto(stop, hat, 0.3, 0.6 * ms); addInto(stop, kick, 0.15, 1.0 * ms);
    addInto(sbot, snare, -0.7, 0.45 * ms); addInto(sbot, kick, 0.1, 0.9 * ms);
    std::vector<std::vector<double>*> sigs { &oh, &kin, &stop, &sbot };
    const char* names[] = { "Overheads", "Kick In", "Snare Top", "Snare Bottom" };

    std::vector<std::unique_ptr<GHSDrumGateProcessor>> procs;
    FakePlayHead ph;
    for (int i = 0; i < 4; ++i) {
        auto p = std::make_unique<GHSDrumGateProcessor>();
        p->setPlayConfigDetails(1, 1, sr, bs);           // mono tracks
        p->setUserAlignName(names[i]);
        setP(*p, "keySource", 3.f);                         // Align Only: compare alignment alone
        if (i == 0 && ! autoRef) setP(*p, "alignReference", 1.f);
        p->setPlayHead(&ph);
        p->prepareToPlay(sr, bs);
        procs.push_back(std::move(p));
    }
    auto& reg = procs[0]->getAlignRegistry();
    reg.startAnalysis(0, usePhase);

    std::vector<std::vector<double>> outs(4, std::vector<double>(kick.size()));
    juce::AudioBuffer<float> buf(1, bs); juce::MidiBuffer midi;
    bool analysed = false; size_t analysedAt = 0;
    for (size_t s = 0; s + bs <= kick.size(); s += bs) {
        ph.t = juce::int64(s);
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < bs; ++k) buf.setSample(0, k, float((*sigs[size_t(i)])[s + size_t(k)]));
            midi.clear(); procs[size_t(i)]->processBlock(buf, midi);
            for (int k = 0; k < bs; ++k) outs[size_t(i)][s + size_t(k)] = buf.getSample(0, k);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
        if (! analysed && reg.getStatus(0).phase == Registry::Phase::Done) { analysed = true; analysedAt = s; }
    }
    auto t0 = juce::Time::getMillisecondCounter(); for (int i = 0; i < 1200 && ! analysed; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); analysed = reg.getStatus(0).phase == Registry::Phase::Done; }

    printf("waited %u ms after the song ended\n", juce::Time::getMillisecondCounter() - t0);
    auto st = reg.getStatus(0);
    printf("status: %s (done at %.2f s)\n", st.message.toRawUTF8(), analysedAt / sr);
    int fails = 0;
    const double expect[] = { 0, 0.15 - 3.6, 0.1 - 2.9, 0.45 - 2.9 };
    const bool flip[] = { false, false, false, true };
    for (auto& r : st.reports) {
        int i = 0; for (; i < 4; ++i) if (r.name == names[i]) break;
        bool ok = r.isReference == (i == 0) && std::fabs(r.lagMs - expect[i]) < 0.1 && (usePhase ? (std::fabs(std::fabs(r.phaseDeg) - 180) < 45) == flip[i] : (std::fabs(std::fabs(r.phaseDeg) - 180) < 1) == flip[i]);
        fails += ! ok;
        printf("%s %-13s ref=%d lag %6.2f ms (expect %6.2f) phase %4.0f corr %.2f -> %.2f\n", ok ? "ok  " : "FAIL", r.name.toRawUTF8(), r.isReference, r.lagMs, expect[i], r.phaseDeg, r.corrBefore, r.corrAfter);
    }
    // second pass of the song, now aligned
    analysedAt = 0;
    for (size_t s = 0; s + bs <= kick.size(); s += bs) {
        ph.t = juce::int64(s);
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < bs; ++k) buf.setSample(0, k, float((*sigs[size_t(i)])[s + size_t(k)]));
            midi.clear(); procs[size_t(i)]->processBlock(buf, midi);
            for (int k = 0; k < bs; ++k) outs[size_t(i)][s + size_t(k)] = buf.getSample(0, k);
        }
    }
    for (int i = 0; i < 4; ++i) {
        printf("%s latency %d, alignOn %d\n", names[i], procs[size_t(i)]->getLatencySamples(), int(procs[size_t(i)]->apvts.getRawParameterValue("alignOn")->load()));
    }
    // After alignment: in the second half, each track's snare/kick component should line up with the overhead's.
    // Check: correlation of outputs (masked to each close mic's own hits) vs overhead output at zero lag, compared to inputs.
    auto corr = [&](const std::vector<double>& a, const std::vector<double>& b, size_t from, size_t to) { double ab=0,aa=0,bb=0; for (size_t i=from;i<to;++i){ab+=a[i]*b[i];aa+=a[i]*a[i];bb+=b[i]*b[i];} return ab/std::sqrt(aa*bb); };
    const size_t from = analysedAt + size_t(sr * 0.5), to = kick.size() - 2000;
    for (int i = 1; i < 4; ++i) {
        auto m = focusMask(*sigs[size_t(i)], sr);
        std::vector<double> a(oh.size()), b(oh.size()), ai(oh.size()), bi(oh.size());
        for (size_t k = 0; k < oh.size(); ++k) { a[k] = outs[0][k] * m[k]; b[k] = outs[size_t(i)][k] * m[k]; ai[k] = oh[k] * m[k]; bi[k] = (*sigs[size_t(i)])[k] * m[k]; }
        // masks are in input time; outputs are delayed by the common latency, so shift the mask
        const int lat = procs[0]->getLatencySamples();
        std::vector<double> mo(oh.size()); for (size_t k = size_t(lat); k < oh.size(); ++k) mo[k] = m[k - size_t(lat)];
        for (size_t k = 0; k < oh.size(); ++k) { a[k] = outs[0][k] * mo[k]; b[k] = outs[size_t(i)][k] * mo[k]; }
        const double before = corr(ai, bi, from, to), after = corr(a, b, from, to);
        const bool ok = after > 0.5 && after > before + 0.2;
        fails += ! ok;
        printf("%s %-13s vs overheads at the outputs: corr %.2f -> %.2f\n", ok ? "ok  " : "FAIL", names[i], before, after);
    }
    if (argc > 2) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(procs[2]->createEditor());
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "ALIGN") b->triggerClick();
        for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        juce::File out(argv[2]); out.deleteFile();
        juce::FileOutputStream os(out); juce::PNGImageFormat().writeImageToStream(img, os);
    }
    printf(fails ? "E2E FAILED\n" : "E2E PASSED\n");
    return fails;
}
