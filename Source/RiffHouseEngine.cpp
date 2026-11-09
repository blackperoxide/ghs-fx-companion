#include "RiffHouseEngine.h"

namespace RiffHouse
{
// ============================================================ folders
juce::File rootFolder()
{
    auto f = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("GHS").getChildFile("RiffHouse");
    f.createDirectory();
    return f;
}
juce::File chartsFolder() { auto f = rootFolder().getChildFile("Charts"); f.createDirectory(); return f; }
juce::File journalFolder() { auto f = rootFolder().getChildFile("Journal"); f.createDirectory(); return f; }
juce::File inboxFolder()
{
    auto f = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("GHS").getChildFile("Inbox");
    f.createDirectory();
    return f;
}

namespace
{
    juce::String slugify(const juce::String& s)
    {
        juce::String out;
        for (auto c : s.toLowerCase())
            out << (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : juce::String("-"));
        while (out.contains("--")) out = out.replace("--", "-");
        return out.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    }

    juce::String stamp() { return juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"); }

    /** Box-filter decimation to roughly 12 kHz - enough for pitch up to ~1.4 kHz, ~16x cheaper YIN. */
    std::vector<float> decimate(const float* x, int n, double sr, double& outSr)
    {
        const int d = juce::jmax(1, (int) std::round(sr / 12000.0));
        outSr = sr / d;
        std::vector<float> y((size_t) (n / d));
        for (size_t i = 0; i < y.size(); ++i)
        {
            float acc = 0;
            for (int k = 0; k < d; ++k) acc += x[i * (size_t) d + (size_t) k];
            y[i] = acc / (float) d;
        }
        return y;
    }

    float rmsOf(const float* x, int n)
    {
        double a = 0;
        for (int i = 0; i < n; ++i) a += (double) x[i] * x[i];
        return n > 0 ? (float) std::sqrt(a / n) : 0.0f;
    }
}

// ============================================================ YIN
float detectPitchYin(const float* x, int n, double sr, float& clarity, float minHz, float maxHz)
{
    clarity = 0;
    const int W = n / 2;
    const int tauMin = juce::jmax(2, (int) (sr / maxHz));
    const int tauMax = juce::jmin(W - 1, (int) (sr / minHz));
    if (tauMax <= tauMin + 2) return 0;

    std::vector<float> d((size_t) tauMax + 2, 0.0f);
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        float s = 0;
        for (int j = 0; j < W; ++j) { const float v = x[j] - x[j + tau]; s += v * v; }
        d[(size_t) tau] = s;
    }
    // cumulative mean normalised difference
    float running = 0;
    d[0] = 1;
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        running += d[(size_t) tau];
        d[(size_t) tau] = running > 0 ? d[(size_t) tau] * (float) tau / running : 1.0f;
    }

    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (d[(size_t) tau] < 0.12f)
        {
            while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        if (d[(size_t) best] > 0.35f) return 0;
    }
    clarity = juce::jlimit(0.0f, 1.0f, 1.0f - d[(size_t) best]);

    float t = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const float den = a - 2 * b + c;
        if (std::abs(den) > 1e-9f) t += 0.5f * (a - c) / den;
    }
    return (float) (sr / t);
}

// ============================================================ WSOLA
juce::AudioBuffer<float> timeStretch(const juce::AudioBuffer<float>& in, double rate)
{
    if (std::abs(rate - 1.0) < 1e-3 || in.getNumSamples() < 8192) return in;

    const int N = 2048, Hs = N / 4, tol = 256;
    const double Ha = Hs * rate;
    const int chans = in.getNumChannels(), inLen = in.getNumSamples();
    const int outLen = (int) (inLen / rate);
    juce::AudioBuffer<float> out(chans, outLen + N);
    out.clear();
    std::vector<float> norm((size_t) outLen + N, 0.0f), win((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / N);

    std::vector<float> mono((size_t) inLen);
    for (int i = 0; i < inLen; ++i)
    {
        float s = 0;
        for (int c = 0; c < chans; ++c) s += in.getSample(c, i);
        mono[(size_t) i] = s;
    }

    int prev = 0;
    for (int k = 0;; ++k)
    {
        const int outPos = k * Hs;
        if (outPos + N > outLen + N) break;
        int nominal = (int) (k * Ha);
        if (nominal + N + tol >= inLen) break;

        int chosen = nominal;
        if (k > 0)
        {
            const int natural = prev + Hs;
            double bestC = -1e30;
            for (int off = -tol; off <= tol; off += 4)
            {
                const int p = nominal + off;
                if (p < 0 || p + N >= inLen || natural + N >= inLen) continue;
                double c = 0;
                for (int j = 0; j < N; j += 4) c += (double) mono[(size_t) (p + j)] * mono[(size_t) (natural + j)];
                if (c > bestC) { bestC = c; chosen = p; }
            }
        }
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* o = out.getWritePointer(ch) + outPos;
            auto* x = in.getReadPointer(ch) + chosen;
            for (int j = 0; j < N; ++j) o[j] += x[j] * win[(size_t) j];
        }
        for (int j = 0; j < N; ++j) norm[(size_t) (outPos + j)] += win[(size_t) j];
        prev = chosen;
    }
    for (int ch = 0; ch < chans; ++ch)
    {
        auto* o = out.getWritePointer(ch);
        for (int i = 0; i < outLen; ++i) if (norm[(size_t) i] > 1e-3f) o[i] /= norm[(size_t) i];
    }
    out.setSize(chans, outLen, true);
    return out;
}

// ============================================================ Engine
Engine::Engine() : juce::Thread("RiffHouse analysis")
{
    for (int i = 0; i < 128; ++i) { ccValue[i] = 0; ccSerial[i] = 0; }
    formats.registerBasicFormats(); // WAV/AIFF/FLAC/OGG everywhere; + MP3/M4A via CoreAudio on macOS
    startThread(juce::Thread::Priority::low);
}

Engine::~Engine()
{
    stopThread(2000);
    pool.removeAllJobs(true, 5000);
}

void Engine::prepare(double sr, int)
{
    if (sr == sampleRate && ring != nullptr) return;
    sampleRate = sr;
    ringSize = (int) (sr * 60.0);
    ring.calloc((size_t) ringSize);
    totalWritten = 0;
}

void Engine::processInput(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const auto base = totalWritten.load(std::memory_order_relaxed);

    if (ring != nullptr && buffer.getNumChannels() > 0)
    {
        const int n = buffer.getNumSamples(), chans = buffer.getNumChannels();
        const float g = 1.0f / (float) chans;
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            for (int c = 0; c < chans; ++c) s += buffer.getSample(c, i);
            ring[(size_t) ((base + i) % ringSize)] = s * g;
        }
        totalWritten.store(base + n, std::memory_order_release);
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            ccValue[m.getControllerNumber()] = m.getControllerValue();
            ccSerial[m.getControllerNumber()].fetch_add(1);
            continue;
        }
        if (!(m.isNoteOn() || m.isNoteOff())) continue;
        const int note = m.getNoteNumber();
        const auto bit = (juce::uint64) 1 << (note & 63);
        if (m.isNoteOn()) held[note >> 6].fetch_or(bit); else held[note >> 6].fetch_and(~bit);

        int s1, n1, s2, n2;
        midiFifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0)
        {
            midiEvBuf[(size_t) s1] = { base + meta.samplePosition, (juce::uint8) (m.isNoteOn() ? 0x90 : 0x80),
                                       (juce::uint8) note, (juce::uint8) (m.isNoteOn() ? m.getVelocity() : 0) };
            midiFifo.finishedWrite(1);
        }
    }
}

void Engine::renderBacking(juce::AudioBuffer<float>& buffer) noexcept
{
    if (!playing.load()) return;
    const juce::SpinLock::ScopedTryLockType lock(backingLock);
    if (!lock.isLocked() || backingPlay.getNumSamples() == 0) return;

    auto pos = playPos.load();
    const int n = buffer.getNumSamples();
    const int avail = (int) juce::jmin((juce::int64) n, (juce::int64) backingPlay.getNumSamples() - pos);
    if (avail <= 0) { playing = false; return; }

    const float g = backingGain.load();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        buffer.addFrom(c, 0, backingPlay, c % backingPlay.getNumChannels(), (int) pos, avail, g);
    playPos = pos + n;
}

void Engine::run()
{
    std::vector<float> win;
    juce::dsp::FFT fft(12);
    std::vector<float> fftBuf(8192);

    while (!threadShouldExit())
    {
        wait(12);

        // drain MIDI events into the 60 s log
        {
            int s1, n1, s2, n2;
            const int ready = midiFifo.getNumReady();
            midiFifo.prepareToRead(ready, s1, n1, s2, n2);
            std::lock_guard<std::mutex> l(midiLogLock);
            for (int i = 0; i < n1; ++i) midiLog.push_back(midiEvBuf[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) midiLog.push_back(midiEvBuf[(size_t) (s2 + i)]);
            midiFifo.finishedRead(n1 + n2);
            const auto cutoff = totalWritten.load() - (juce::int64) (sampleRate * 60.0);
            while (!midiLog.empty() && midiLog.front().pos < cutoff) midiLog.erase(midiLog.begin());
        }

        const auto total = totalWritten.load(std::memory_order_acquire);
        const int n = 4096;
        if (ring == nullptr || total < n) continue;
        win.resize((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = ring[(size_t) ((total - n + i) % ringSize)];

        const float r = rmsOf(win.data() + n - 2048, 2048);
        liveRms = r;
        if (r < 0.004f) { liveHz = 0; liveClarity = 0; continue; }

        double dsr;
        auto dec = decimate(win.data(), n, sampleRate, dsr);
        const int dn = juce::jmin((int) dec.size(), 1024);
        float clarity;
        const float hz = detectPitchYin(dec.data() + dec.size() - (size_t) dn, dn, dsr, clarity);
        liveHz = hz;
        liveClarity = clarity;

        // chroma for chord detection (strums)
        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            fftBuf[(size_t) i] = win[(size_t) i] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / n));
        fft.performFrequencyOnlyForwardTransform(fftBuf.data());
        std::array<float, 12> c {};
        for (int b = 1; b < n / 2; ++b)
        {
            const double f = b * sampleRate / n;
            if (f < 60 || f > 2000) continue;
            const int pc = ((int) std::lround(69 + 12 * std::log2(f / 440.0)) % 12 + 12) % 12;
            c[(size_t) pc] += fftBuf[(size_t) b] * fftBuf[(size_t) b];
        }
        const float mx = *std::max_element(c.begin(), c.end());
        if (mx > 0) for (auto& v : c) v /= mx;
        std::lock_guard<std::mutex> l(chromaLock);
        chroma = c;
    }
}

juce::var Engine::getLiveState()
{
    auto* o = new juce::DynamicObject();
    const float hz = liveHz.load();
    o->setProperty("hz", hz);
    o->setProperty("clarity", liveClarity.load());
    o->setProperty("rms", liveRms.load());
    if (hz > 0)
    {
        const double m = 69 + 12 * std::log2(hz / 440.0);
        o->setProperty("midi", m);
    }
    juce::Array<juce::var> heldNotes;
    for (int w = 0; w < 2; ++w)
    {
        const auto bits = held[w].load();
        for (int b = 0; b < 64; ++b) if (bits & ((juce::uint64) 1 << b)) heldNotes.add(w * 64 + b);
    }
    o->setProperty("held", heldNotes);
    {
        std::lock_guard<std::mutex> l(chromaLock);
        juce::Array<juce::var> ch;
        for (auto v : chroma) ch.add(v);
        o->setProperty("chroma", ch);
    }
    o->setProperty("playing", playing.load());
    o->setProperty("pos", (double) playPos.load() / sampleRate * backingRate);
    o->setProperty("busy", busy.load());
    juce::Array<juce::var> cc; // [number, value, serial] for every CC that has ever moved
    for (int i = 0; i < 128; ++i)
        if (const auto ser = ccSerial[i].load()) cc.add(juce::Array<juce::var> { i, ccValue[i].load(), (int) ser });
    o->setProperty("cc", cc);
    return juce::var(o);
}

// ============================================================ files
bool Engine::writeWav(const juce::File& f, const float* data, int n, double sr)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    if (!stream->openedOk()) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.get(), sr, 1, 24, {}, 0));
    if (w == nullptr) return false;
    stream.release(); // writer owns it now
    const float* chans[] = { data };
    return w->writeFromFloatArrays(chans, 1, n);
}

namespace
{
    bool writeMidi(const juce::File& f, const std::vector<std::array<double, 4>>& ev /* sec, on, note, vel */, double bpm)
    {
        const int ppq = 960;
        const double secPerTick = 60.0 / bpm / ppq;
        juce::MidiMessageSequence seq;
        auto tempo = juce::MidiMessage::tempoMetaEvent((int) (60000000.0 / bpm));
        tempo.setTimeStamp(0);
        seq.addEvent(tempo);
        for (auto& e : ev)
        {
            auto m = e[1] > 0 ? juce::MidiMessage::noteOn(1, (int) e[2], (juce::uint8) juce::jlimit(1, 127, (int) e[3]))
                              : juce::MidiMessage::noteOff(1, (int) e[2]);
            m.setTimeStamp(std::round(e[0] / secPerTick));
            seq.addEvent(m);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote(ppq);
        mf.addTrack(seq);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out(f);
        return out.openedOk() && mf.writeTo(out);
    }
}

bool Engine::readMono(const juce::File& f, juce::AudioBuffer<float>& out, double& sr)
{
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (r == nullptr) return false;
    const int len = (int) juce::jmin(r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 15));
    juce::AudioBuffer<float> tmp((int) r->numChannels, len);
    r->read(&tmp, 0, len, 0, true, true);
    out.setSize(1, len);
    out.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c) out.addFrom(0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());
    sr = r->sampleRate;
    return true;
}

// ============================================================ capture
juce::var Engine::saveThat(double seconds, double bpm)
{
    auto* o = new juce::DynamicObject();
    const auto total = totalWritten.load();
    int n = (int) juce::jmin((juce::int64) (seconds * sampleRate), total, (juce::int64) ringSize);
    const auto start = total - n;

    juce::AudioBuffer<float> cap(1, juce::jmax(1, n));
    for (int i = 0; i < n; ++i) cap.setSample(0, i, ring[(size_t) ((start + i) % ringSize)]);
    int lead = 0;
    while (lead < n && std::abs(cap.getSample(0, lead)) < 0.01f) ++lead;
    lead = juce::jmax(0, lead - (int) (0.05 * sampleRate));

    const auto name = "riff-idea-" + stamp();
    o->setProperty("name", name);

    if (n - lead > (int) (0.25 * sampleRate))
    {
        lastCapture.setSize(1, n - lead);
        lastCapture.copyFrom(0, 0, cap, 0, lead, n - lead);
        lastCaptureName = name;
        auto f = inboxFolder().getChildFile("Audio").getChildFile(name + ".wav");
        if (writeWav(f, lastCapture.getReadPointer(0), lastCapture.getNumSamples(), sampleRate))
        {
            o->setProperty("audio", f.getFullPathName());
            o->setProperty("seconds", lastCapture.getNumSamples() / sampleRate);
        }
    }

    std::vector<std::array<double, 4>> ev;
    {
        std::lock_guard<std::mutex> l(midiLogLock);
        juce::int64 first = -1;
        for (auto& e : midiLog)
        {
            if (e.pos < start) continue;
            if (first < 0) first = e.pos;
            ev.push_back({ (double) (e.pos - first) / sampleRate, e.status == 0x90 ? 1.0 : 0.0, (double) e.note, (double) e.vel });
        }
    }
    if (!ev.empty())
    {
        auto f = inboxFolder().getChildFile("MIDI").getChildFile(name + ".mid");
        if (writeMidi(f, ev, bpm)) o->setProperty("midi", f.getFullPathName());
    }
    if (!o->hasProperty("audio") && !o->hasProperty("midi")) o->setProperty("error", "Nothing heard in the last minute.");
    return juce::var(o);
}

juce::var Engine::getCaptureWave(int numPoints)
{
    juce::Array<juce::var> pts;
    const int n = lastCapture.getNumSamples();
    if (n > 0 && numPoints > 0)
    {
        const auto* x = lastCapture.getReadPointer(0);
        for (int p = 0; p < numPoints; ++p)
        {
            const int a = (int) ((juce::int64) p * n / numPoints), b = (int) ((juce::int64) (p + 1) * n / numPoints);
            float m = 0;
            for (int i = a; i < b; ++i) m = juce::jmax(m, std::abs(x[i]));
            pts.add(m);
        }
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("peaks", pts);
    o->setProperty("seconds", n / sampleRate);
    o->setProperty("name", lastCaptureName);
    return juce::var(o);
}

juce::var Engine::exportLoop(double s, double e, double bpm)
{
    auto* o = new juce::DynamicObject();
    const int n = lastCapture.getNumSamples();
    int a = juce::jlimit(0, n, (int) (s * sampleRate)), b = juce::jlimit(0, n, (int) (e * sampleRate));
    if (b - a < 1000) { o->setProperty("error", "Pick a longer section."); return juce::var(o); }
    // snap to whole bars at bpm
    const double bar = sampleRate * 60.0 / bpm * 4.0;
    const int bars = juce::jmax(1, (int) std::round((b - a) / bar));
    b = juce::jmin(n, a + (int) std::round(bars * bar));
    auto f = inboxFolder().getChildFile("Audio").getChildFile(lastCaptureName + "-loop-" + juce::String((int) bpm) + "bpm-" + juce::String(bars) + "bar.wav");
    writeWav(f, lastCapture.getReadPointer(0) + a, b - a, sampleRate);
    o->setProperty("file", f.getFullPathName());
    o->setProperty("bars", bars);
    return juce::var(o);
}

juce::var Engine::exportSlices(double s, double e, double sens, double bpm)
{
    auto* o = new juce::DynamicObject();
    const int n = lastCapture.getNumSamples();
    const int a = juce::jlimit(0, n, (int) (s * sampleRate)), b = juce::jlimit(0, n, (int) (e * sampleRate));
    if (b - a < 1000) { o->setProperty("error", "Pick a longer section."); return juce::var(o); }
    const auto* x = lastCapture.getReadPointer(0);
    const int hop = 512;
    const double th = std::pow(1.0 + (11.0 - sens) * 0.25, 2.0);
    std::vector<int> cuts { a };
    double prev = 0;
    for (int i = a; i + hop < b; i += hop)
    {
        double en = 0;
        for (int j = i; j < i + hop; ++j) en += (double) x[j] * x[j];
        en /= hop;
        if (en > 4e-4 && en > prev * th && i - cuts.back() > (int) (0.08 * sampleRate) && i > a + hop) cuts.push_back(i);
        prev = en * 0.7 + prev * 0.3;
    }
    cuts.push_back(b);

    auto dir = inboxFolder().getChildFile("Audio").getChildFile(lastCaptureName + "-slices");
    std::vector<std::array<double, 4>> ev;
    for (size_t i = 0; i + 1 < cuts.size(); ++i)
    {
        const int len = cuts[i + 1] - cuts[i];
        writeWav(dir.getChildFile("slice-" + juce::String((int) i + 1).paddedLeft('0', 2) + ".wav"), x + cuts[i], len, sampleRate);
        const double t = (cuts[i] - a) / sampleRate;
        ev.push_back({ t, 1, 36.0 + (double) i, 100 });
        ev.push_back({ t + len / sampleRate * 0.95, 0, 36.0 + (double) i, 0 });
    }
    writeMidi(dir.getChildFile("slices-groove.mid"), ev, bpm);
    o->setProperty("folder", dir.getFullPathName());
    o->setProperty("count", (int) cuts.size() - 1);
    return juce::var(o);
}

// ============================================================ charts
juce::var Engine::makeChartVar(const juce::String& title, const juce::String& instrument, double bpm,
                               const std::vector<std::array<double, 3>>& notes, const juce::StringArray& backing,
                               const juce::String& part, const juce::String& source)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("riffChart", 1);
    o->setProperty("title", title);
    o->setProperty("instrument", instrument);
    o->setProperty("bpm", bpm);
    o->setProperty("source", source);
    o->setProperty("part", part);
    juce::Array<juce::var> b;
    for (auto& s : backing) b.add(s);
    o->setProperty("backing", b);
    juce::Array<juce::var> ns;
    for (auto& n : notes)
        ns.add(juce::Array<juce::var> { std::round(n[0] * 1000) / 1000, std::round(n[1] * 1000) / 1000, (int) n[2] });
    o->setProperty("notes", ns);
    return juce::var(o);
}

juce::String Engine::saveChart(const juce::var& chart)
{
    auto id = slugify(chart["title"].toString() + "-" + chart["instrument"].toString());
    chartsFolder().getChildFile(id + ".riffchart.json").replaceWithText(juce::JSON::toString(chart));
    return id;
}

juce::var Engine::transcribeBuffer(const juce::AudioBuffer<float>& mono, double sr, const juce::String& title,
                                   const juce::String& instrument, const juce::StringArray& backing,
                                   const juce::String& part, const juce::String& source)
{
    double dsr;
    auto x = decimate(mono.getReadPointer(0), mono.getNumSamples(), sr, dsr);
    const int hop = (int) (dsr * 0.01), W = 1024;
    const int frames = juce::jmax(0, ((int) x.size() - W) / hop);
    const float minHz = instrument == "bass" ? 30.0f : 60.0f;

    std::vector<float> rms((size_t) frames);
    std::vector<int> midi((size_t) frames, -1);
    for (int f = 0; f < frames; ++f)
    {
        const float* w = x.data() + (size_t) f * (size_t) hop;
        rms[(size_t) f] = rmsOf(w, W);
        float cl;
        const float hz = detectPitchYin(w, W, dsr, cl, minHz, 1400.0f);
        if (hz > 0 && cl > 0.8f) midi[(size_t) f] = (int) std::lround(69 + 12 * std::log2(hz / 440.0));
    }
    // gate relative to the loud parts of the file
    auto sorted = rms;
    std::sort(sorted.begin(), sorted.end());
    const float gate = sorted.empty() ? 0.01f : juce::jmax(0.005f, sorted[(size_t) (sorted.size() * 0.95)] * 0.12f);
    for (int f = 0; f < frames; ++f) if (rms[(size_t) f] < gate) midi[(size_t) f] = -1;

    // 5-frame median to kill octave blips
    auto med = midi;
    for (int f = 2; f + 2 < frames; ++f)
    {
        std::array<int, 5> v { midi[(size_t) f - 2], midi[(size_t) f - 1], midi[(size_t) f], midi[(size_t) f + 1], midi[(size_t) f + 2] };
        std::sort(v.begin(), v.end());
        med[(size_t) f] = v[2];
    }

    std::vector<std::array<double, 3>> notes;
    int cur = -1, startF = 0;
    auto close = [&](int endF)
    {
        const double d = (endF - startF) * 0.01;
        if (cur >= 0 && d >= 0.07) notes.push_back({ startF * 0.01, d, (double) cur });
    };
    for (int f = 0; f < frames; ++f)
    {
        const bool onset = f > 3 && rms[(size_t) f] > 1.6f * rms[(size_t) f - 3] && rms[(size_t) f] > gate * 2 && f - startF > 8;
        if (med[(size_t) f] != cur || (onset && cur >= 0))
        {
            close(f);
            cur = med[(size_t) f];
            startF = f;
        }
    }
    close(frames);

    // tempo: autocorrelate onset strength over 60..180 BPM
    double bpm = 90;
    if (frames > 400)
    {
        std::vector<float> on((size_t) frames, 0);
        for (int f = 1; f < frames; ++f) on[(size_t) f] = juce::jmax(0.0f, rms[(size_t) f] - rms[(size_t) f - 1]);
        double best = 0;
        for (int lag = 33; lag <= 100; ++lag)
        {
            double c = 0;
            for (int f = lag; f < frames; ++f) c += on[(size_t) f] * on[(size_t) f - lag];
            const double b = 6000.0 / lag;
            c *= 1.0 - 0.3 * std::abs(std::log2(b / 105.0)); // gentle bias to common tempos
            if (c > best) { best = c; bpm = b; }
        }
    }
    return makeChartVar(title, instrument, std::round(bpm), notes, backing, part, source);
}

void Engine::importAudioAsync(const juce::File& file, const juce::String& instrument, std::function<void(juce::var)> onDone)
{
    busy = true;
    pool.addJob([this, file, instrument, onDone]
    {
        juce::AudioBuffer<float> mono;
        double sr = 0;
        juce::var result;
        if (!readMono(file, mono, sr))
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("error", "Couldn't read " + file.getFileName() + ". Try WAV or AIFF.");
            result = juce::var(o);
        }
        else
        {
            auto chart = transcribeBuffer(mono, sr, file.getFileNameWithoutExtension(), instrument,
                                          juce::StringArray { file.getFullPathName() }, {}, "audio");
            chart.getDynamicObject()->setProperty("id", saveChart(chart));
            result = chart;
        }
        busy = false;
        juce::MessageManager::callAsync([onDone, result] { onDone(result); });
    });
}

juce::var Engine::midiFileToChart(const juce::File& f, const juce::String& title, const juce::String& instrument,
                                  const juce::StringArray& backing, const juce::String& part)
{
    juce::FileInputStream in(f);
    juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) return {};

    double bpm = 90;
    juce::MidiMessageSequence tempos;
    mf.findAllTempoEvents(tempos);
    if (tempos.getNumEvents() > 0)
        bpm = std::round(60.0 / tempos.getEventPointer(0)->message.getTempoSecondsPerQuarterNote());
    mf.convertTimestampTicksToSeconds();

    std::vector<std::array<double, 3>> notes;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq(*mf.getTrack(t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer(i);
            if (!e->message.isNoteOn()) continue;
            const double on = e->message.getTimeStamp();
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : on + 0.25;
            notes.push_back({ on, juce::jmax(0.05, off - on), (double) e->message.getNoteNumber() });
        }
    }
    std::sort(notes.begin(), notes.end(), [](auto& a, auto& b) { return a[0] < b[0]; });
    return makeChartVar(title, instrument, bpm, notes, backing, part, "midi");
}

juce::var Engine::importMidi(const juce::File& file, const juce::String& instrument)
{
    auto chart = midiFileToChart(file, file.getFileNameWithoutExtension(), instrument, {}, {});
    if (chart.isVoid())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("error", "Couldn't read that MIDI file.");
        return juce::var(o);
    }
    chart.getDynamicObject()->setProperty("id", saveChart(chart));
    return chart;
}

void Engine::importSongFolderAsync(const juce::File& folder, std::function<void(juce::var)> onDone)
{
    auto song = juce::JSON::parse(folder.getChildFile("song.json"));
    auto* stems = song["stems"].getDynamicObject();
    if (stems == nullptr)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("error", "No song.json found. Run tools/riffhouse_import.py on the song first, or import a single audio/MIDI file.");
        onDone(juce::var(o));
        return;
    }
    busy = true;
    const auto title = song["title"].toString();
    juce::StringArray allStems;
    for (auto& p : stems->getProperties()) allStems.add(folder.getChildFile(p.value.toString()).getFullPathName());

    struct Shared { std::mutex m; juce::Array<juce::var> made; int remaining = 0; };
    auto shared = std::make_shared<Shared>();
    juce::Array<juce::NamedValueSet::NamedValue> parts;
    for (auto& p : stems->getProperties())
        if (p.name.toString() != "drums" && p.name.toString() != "other") parts.add(p);
    shared->remaining = parts.size();
    if (parts.isEmpty()) { busy = false; onDone(juce::var(new juce::DynamicObject())); return; }

    // one job per stem - on an M1 Pro/Max the stems transcribe side by side
    for (auto& p : parts)
    {
        pool.addJob([this, folder, song, title, allStems, p, shared, onDone]
        {
            const auto partName = p.name.toString();
            const auto partPath = folder.getChildFile(p.value.toString()).getFullPathName();
            juce::StringArray backing(allStems);
            backing.removeString(partPath);
            const auto instrument = partName == "piano" ? juce::String("keys") : partName;

            juce::var chart;
            auto midiRel = song["midi"][p.name].toString();
            if (midiRel.isNotEmpty() && folder.getChildFile(midiRel).existsAsFile())
                chart = midiFileToChart(folder.getChildFile(midiRel), title, instrument, backing, partPath);
            else
            {
                juce::AudioBuffer<float> mono;
                double sr;
                if (readMono(juce::File(partPath), mono, sr))
                    chart = transcribeBuffer(mono, sr, title, instrument, backing, partPath, "stem");
            }
            std::lock_guard<std::mutex> l(shared->m);
            if (!chart.isVoid() && chart["notes"].size() >= 4)
            {
                if ((double) song["bpm"] > 0) chart.getDynamicObject()->setProperty("bpm", song["bpm"]);
                chart.getDynamicObject()->setProperty("id", saveChart(chart));
                shared->made.add(chart["id"]);
            }
            if (--shared->remaining == 0)
            {
                busy = false;
                auto* o = new juce::DynamicObject();
                o->setProperty("charts", shared->made);
                juce::var result(o);
                juce::MessageManager::callAsync([onDone, result] { onDone(result); });
            }
        });
    }
}

juce::var Engine::listCharts()
{
    juce::Array<juce::var> out;
    for (auto& f : chartsFolder().findChildFiles(juce::File::findFiles, false, "*.riffchart.json"))
    {
        auto c = juce::JSON::parse(f);
        auto* o = new juce::DynamicObject();
        o->setProperty("id", f.getFileName().upToFirstOccurrenceOf(".riffchart.json", false, true));
        o->setProperty("title", c["title"]);
        o->setProperty("instrument", c["instrument"]);
        o->setProperty("notes", c["notes"].size());
        o->setProperty("hasBacking", c["backing"].size() > 0);
        out.add(juce::var(o));
    }
    return out;
}

void Engine::installBacking(juce::AudioBuffer<float>&& raw, double sr)
{
    // resample to host rate once, so playback is a plain copy
    if (std::abs(sr - sampleRate) > 1)
    {
        const double ratio = sr / sampleRate;
        const int outN = (int) (raw.getNumSamples() / ratio);
        juce::AudioBuffer<float> rs(raw.getNumChannels(), outN);
        for (int c = 0; c < raw.getNumChannels(); ++c)
        {
            juce::LagrangeInterpolator li;
            li.process(ratio, raw.getReadPointer(c), rs.getWritePointer(c), outN);
        }
        raw = std::move(rs);
    }
    auto stretched = timeStretch(raw, backingRate);
    const juce::SpinLock::ScopedLockType l(backingLock);
    backingRaw = std::move(raw);
    backingPlay = std::move(stretched);
    playPos = 0;
}

void Engine::loadChartAsync(const juce::String& id, bool hearPart, double rate, std::function<void(juce::var)> onDone)
{
    playing = false;
    busy = true;
    backingRate = juce::jlimit(0.4, 1.5, rate);
    pool.addJob([this, id, hearPart, onDone]
    {
        auto chart = juce::JSON::parse(chartsFolder().getChildFile(id + ".riffchart.json"));
        juce::StringArray files;
        for (auto& b : *chart["backing"].getArray()) files.add(b.toString());
        if (hearPart && chart["part"].toString().isNotEmpty()) files.add(chart["part"].toString());

        juce::AudioBuffer<float> mix;
        double sr = 0;
        for (auto& path : files)
        {
            std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(juce::File(path)));
            if (r == nullptr) continue;
            if (sr == 0) sr = r->sampleRate;
            const int len = (int) juce::jmin(r->lengthInSamples, (juce::int64) (r->sampleRate * 900));
            juce::AudioBuffer<float> b(2, len);
            r->read(&b, 0, len, 0, true, true);
            if (mix.getNumSamples() < len) mix.setSize(2, len, true, true);
            for (int c = 0; c < 2; ++c) mix.addFrom(c, 0, b, c, 0, len);
        }
        if (mix.getNumSamples() > 0) installBacking(std::move(mix), sr);
        else { const juce::SpinLock::ScopedLockType l(backingLock); backingPlay.setSize(0, 0); backingRaw.setSize(0, 0); }
        if (auto* o = chart.getDynamicObject()) { o->setProperty("id", id); o->setProperty("backingLoaded", mix.getNumSamples() > 0); }
        busy = false;
        juce::MessageManager::callAsync([onDone, chart] { onDone(chart); });
    });
}

void Engine::setTransport(bool shouldPlay, double positionSec, float gain)
{
    backingGain = gain;
    playPos = (juce::int64) (juce::jmax(0.0, positionSec) / backingRate * sampleRate);
    playing = shouldPlay;
}

void Engine::setRateAsync(double rate, std::function<void()> onDone)
{
    rate = juce::jlimit(0.4, 1.5, rate);
    if (std::abs(rate - backingRate) < 1e-3) { if (onDone) onDone(); return; }
    busy = true;
    pool.addJob([this, rate, onDone]
    {
        juce::AudioBuffer<float> raw;
        { const juce::SpinLock::ScopedLockType l(backingLock); raw = backingRaw; }
        auto stretched = timeStretch(raw, rate);
        {
            const juce::SpinLock::ScopedLockType l(backingLock);
            const double chartPos = (double) playPos.load() / sampleRate * backingRate;
            backingPlay = std::move(stretched);
            backingRate = rate;
            playPos = (juce::int64) (chartPos / rate * sampleRate);
        }
        busy = false;
        if (onDone) juce::MessageManager::callAsync(onDone);
    });
}
}
