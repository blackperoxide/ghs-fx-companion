#include "AlignRegistry.h"
#include "AlignDSP.h"
#include <climits>
#include <thread>

namespace GHSAlign
{

// ============================ CaptureBuffer ============================

void CaptureBuffer::arm(double sr, double seconds)
{
    freeze(); // make sure the audio thread isn't writing while we resize
    sampleRate = sr;
    data.assign(size_t(std::max(1.0, sr * seconds)), 0.0f);
    written = 0;
    startTime = 0;
    state = Armed;
}

void CaptureBuffer::write(const float* const* channels, int numChannels, int numSamples,
                          bool playing, std::optional<juce::int64> timeInSamples)
{
    int st = state.load(std::memory_order_acquire);
    if (st != Armed && st != Recording) return;

    busy.store(true, std::memory_order_seq_cst);
    st = state.load(std::memory_order_seq_cst); // re-check now that freeze() can see us
    if ((st == Armed || st == Recording) && playing && timeInSamples.has_value() && numChannels > 0)
    {
        const auto t = *timeInSamples;
        int w = written.load();
        if (st == Armed || t != startTime.load() + w)
        {
            // first block, or the playhead jumped (loop, locate): start over here.
            // CAS, never a plain store: freeze() may have just claimed the buffer.
            if (st == Armed && ! state.compare_exchange_strong(st, Recording))
            {
                busy.store(false, std::memory_order_seq_cst);
                return;
            }
            startTime = t;
            w = 0;
        }
        const int cap = int(data.size());
        const int n = std::min(numSamples, cap - w);
        const float scale = 1.0f / float(numChannels);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) v += channels[ch][i];
            data[size_t(w + i)] = v * scale;
        }
        written.store(w + n, std::memory_order_release);
        if (w + n >= cap)
        {
            int expected = Recording;
            state.compare_exchange_strong(expected, Full);
        }
    }
    busy.store(false, std::memory_order_seq_cst);
}

void CaptureBuffer::freeze()
{
    int st = state.load();
    while (st == Armed || st == Recording)
        if (state.compare_exchange_weak(st, Frozen)) break;
    while (busy.load(std::memory_order_seq_cst))
        std::this_thread::yield();
}

// ============================ Analysis job ============================

struct Registry::Job
{
    struct Input { int id; juce::String name; bool isRef; std::vector<float> samples; };
    std::vector<Input> inputs;
    double sampleRate = 44100.0;
    bool usePhase = false;

    // results
    std::atomic<bool> done { false };
    std::vector<MemberReport> reports;
    std::vector<std::pair<int, MemberAlignment>> alignments;

    void run()
    {
        const int maxLag = int(std::lround(maxAlignMs * sampleRate / 1000.0));

        // Reference: the instance marked as Reference, or else the mic that
        // hears the kit last (normally the overheads / room) - lining the
        // close mics up to it is the usual convention.
        int refIndex = -1;
        for (size_t i = 0; i < inputs.size(); ++i)
            if (inputs[i].isRef) { refIndex = int(i); break; }
        if (refIndex < 0)
        {
            refIndex = 0;
            const auto base = conditioned(inputs[0].samples, sampleRate);
            double latest = 0.0;
            for (size_t i = 1; i < inputs.size(); ++i)
            {
                const auto e = estimateLag(base, conditioned(inputs[i].samples, sampleRate), maxLag);
                if (! e.atLimit && e.lag > latest) { latest = e.lag; refIndex = int(i); }
            }
        }

        const auto& ref = inputs[size_t(refIndex)];
        reports.resize(inputs.size());
        alignments.resize(inputs.size());

        auto analyseOne = [&](size_t i)
        {
            const auto& in = inputs[i];
            MemberReport rep;
            rep.memberId = in.id;
            rep.name = in.name;
            rep.isReference = int(i) == refIndex;
            rep.analysed = true;

            MemberAlignment al;
            al.valid = true;
            al.usePhase = usePhase;

            if (int(i) != refIndex)
            {
                const auto r = analyse(ref.samples, in.samples, sampleRate, maxLag, usePhase);
                rep.corrBefore = r.corrBefore;
                rep.confidence = r.confidence;
                rep.outOfRange = r.outOfRange;
                if (! r.outOfRange)
                {
                    rep.lagMs = r.lagSamples * 1000.0 / sampleRate;
                    rep.phaseDeg = r.phaseRad * 180.0 / M_PI;
                    rep.corrAfter = r.corrAfter;
                    al.offsetSeconds = r.lagSamples / sampleRate;
                    al.phaseRad = r.phaseRad;
                }
                else
                    rep.corrAfter = r.corrBefore; // left as recorded
            }
            else
                rep.corrBefore = rep.corrAfter = 1.0;

            reports[i] = rep;
            alignments[i] = { in.id, al };
        };

        // one worker per track (they're independent), a few at a time
        const size_t workers = std::max<size_t>(1, std::min<size_t>(inputs.size(), std::thread::hardware_concurrency()));
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> pool;
        for (size_t w = 0; w < workers; ++w)
            pool.emplace_back([&] { for (size_t i; (i = next.fetch_add(1)) < inputs.size();) analyseOne(i); });
        for (auto& t : pool) t.join();
        done = true;
    }
};

// ============================ Registry ============================

Registry::~Registry() { stopTimer(); }

void Registry::addMember(AlignMember* m)
{
    const juce::ScopedLock sl(lock);
    m->alignMemberId = nextId++;
    members.push_back(m);
}

void Registry::removeMember(AlignMember* m)
{
    const juce::ScopedLock sl(lock);
    members.erase(std::remove(members.begin(), members.end(), m), members.end());
}

std::vector<AlignMember*> Registry::membersOf(int group) const
{
    std::vector<AlignMember*> out;
    for (auto* m : members)
        if (m->getAlignGroup() == group) out.push_back(m);
    return out;
}

std::vector<MemberReport> Registry::listMembers(int group) const
{
    const juce::ScopedLock sl(lock);
    std::vector<MemberReport> out;
    for (auto* m : membersOf(group))
    {
        MemberReport r;
        r.memberId = m->alignMemberId;
        r.name = m->getAlignName();
        r.isReference = m->isAlignReference();
        // carry over the last analysis for this instance, if any
        for (auto& prev : groups[group].status.reports)
            if (prev.memberId == r.memberId)
            {
                const bool ref = r.isReference;
                r = prev;
                r.name = m->getAlignName();
                r.isReference = prev.isReference || ref;
            }
        out.push_back(r);
    }
    return out;
}

Registry::GroupStatus Registry::getStatus(int group) const
{
    const juce::ScopedLock sl(lock);
    return groups[juce::jlimit(0, numGroups - 1, group)].status;
}

void Registry::startAnalysis(int group, bool usePhase)
{
    group = juce::jlimit(0, numGroups - 1, group);
    const juce::ScopedLock sl(lock);
    auto& g = groups[group];
    if (g.status.phase == Phase::Analysing) return;

    const auto ms = membersOf(group);
    if (ms.size() < 2)
    {
        g.status.phase = Phase::Error;
        g.status.message = "Put GHS Drum Gate on at least two drum tracks (same Align group) first";
        return;
    }

    for (auto* m : ms)
        m->getAlignCapture().arm(m->getAlignSampleRate(), captureSeconds);

    g.usePhase = usePhase;
    g.status.phase = Phase::Capturing;
    g.status.progress = 0.0f;
    g.status.message = "Press play - recording " + juce::String(int(ms.size())) + " tracks";
    g.startedMs = g.lastProgressMs = juce::Time::getMillisecondCounter();
    g.lastWritten = -1;
    startTimerHz(10);
}

void Registry::cancel(int group)
{
    const juce::ScopedLock sl(lock);
    auto& g = groups[juce::jlimit(0, numGroups - 1, group)];
    for (auto* m : membersOf(group))
    {
        m->getAlignCapture().freeze();
        m->getAlignCapture().state = CaptureBuffer::Idle;
    }
    if (g.status.phase == Phase::Capturing)
    {
        g.status.phase = Phase::Idle;
        g.status.message = "Cancelled";
    }
}

void Registry::resetGroup(int group)
{
    cancel(group);
    const juce::ScopedLock sl(lock);
    auto& g = groups[juce::jlimit(0, numGroups - 1, group)];
    for (auto* m : membersOf(group))
        m->applyAlignment(MemberAlignment {});
    g.status = GroupStatus {};
    g.status.message = "Alignment cleared";
}

void Registry::timerCallback()
{
    bool anyActive = false;
    for (int group = 0; group < numGroups; ++group)
    {
        Phase phase;
        {
            const juce::ScopedLock sl(lock);
            phase = groups[group].status.phase;
        }

        if (phase == Phase::Analysing)
        {
            anyActive = true;
            finishJob(group);
            continue;
        }
        if (phase != Phase::Capturing) continue;
        anyActive = true;

        const juce::ScopedLock sl(lock);
        auto& g = groups[group];
        const auto ms = membersOf(group);
        const auto now = juce::Time::getMillisecondCounter();

        int minWritten = INT_MAX, recording = 0, full = 0, cap = 1;
        double sr = 44100.0;
        for (auto* m : ms)
        {
            auto& c = m->getAlignCapture();
            const int st = c.state.load();
            if (st == CaptureBuffer::Recording) ++recording;
            if (st == CaptureBuffer::Full) ++full;
            minWritten = std::min(minWritten, c.written.load());
            cap = std::max(cap, int(c.data.size()));
            sr = c.sampleRate;
        }
        if (ms.size() < 2) { g.status.phase = Phase::Error; g.status.message = "Fewer than two tracks left in the group"; continue; }

        g.status.progress = float(minWritten) / float(cap);
        if (minWritten != g.lastWritten) { g.lastWritten = minWritten; g.lastProgressMs = now; }

        const bool allFull = full == int(ms.size());
        const bool stalledButEnough = minWritten >= int(minCaptureSeconds * sr) && now - g.lastProgressMs > 1200;
        if (allFull || stalledButEnough)
        {
            beginAnalysis(group);
            continue;
        }

        if (recording + full == 0)
            g.status.message = "Press play - recording starts with the transport";
        else
            g.status.message = "Recording " + juce::String(int(ms.size())) + " tracks... "
                               + juce::String(minWritten / sr, 1) + " / " + juce::String(cap / sr, 0) + " s";

        if (now - g.startedMs > 90000)
        {
            for (auto* m : ms) { m->getAlignCapture().freeze(); m->getAlignCapture().state = CaptureBuffer::Idle; }
            g.status.phase = Phase::Error;
            g.status.message = "Didn't hear every track play - play a section where the whole kit is playing";
        }
    }
    if (! anyActive) stopTimer();
}

void Registry::beginAnalysis(int group)
{
    // lock held by caller
    auto& g = groups[group];
    const auto ms = membersOf(group);
    for (auto* m : ms) m->getAlignCapture().freeze();

    // the stretch of timeline every track recorded
    juce::int64 start = std::numeric_limits<juce::int64>::min(), end = std::numeric_limits<juce::int64>::max();
    double sr = 0.0;
    for (auto* m : ms)
    {
        auto& c = m->getAlignCapture();
        start = std::max(start, c.startTime.load());
        end = std::min(end, c.startTime.load() + c.written.load());
        sr = c.sampleRate;
    }

    bool rateMismatch = false;
    for (auto* m : ms) rateMismatch |= std::abs(m->getAlignCapture().sampleRate - sr) > 0.5;

    if (end - start < juce::int64(minCaptureSeconds * sr) || rateMismatch)
    {
        for (auto* m : ms) m->getAlignCapture().state = CaptureBuffer::Idle;
        g.status.phase = Phase::Error;
        g.status.message = rateMismatch ? "Tracks are running at different sample rates"
                                        : "The tracks didn't record the same part of the song - play through without stopping";
        return;
    }

    auto job = std::make_shared<Job>();
    job->sampleRate = sr;
    job->usePhase = g.usePhase;
    for (auto* m : ms)
    {
        auto& c = m->getAlignCapture();
        const auto offset = size_t(start - c.startTime.load());
        Job::Input in { m->alignMemberId, m->getAlignName(), m->isAlignReference(),
                        std::vector<float>(c.data.begin() + long(offset), c.data.begin() + long(offset + size_t(end - start))) };
        job->inputs.push_back(std::move(in));
        c.state = CaptureBuffer::Idle;
    }

    g.job = job;
    g.status.phase = Phase::Analysing;
    g.status.progress = 1.0f;
    g.status.message = "Analysing " + juce::String(int(ms.size())) + " tracks...";
    std::thread([job] { job->run(); }).detach();
}

void Registry::finishJob(int group)
{
    const juce::ScopedLock sl(lock);
    auto& g = groups[group];
    if (! g.job || ! g.job->done.load()) return;

    const auto job = g.job;
    g.job.reset();

    for (auto* m : members)
        for (auto& [id, al] : job->alignments)
            if (m->alignMemberId == id)
                m->applyAlignment(al);

    g.status.reports = job->reports;
    g.status.phase = Phase::Done;
    int aligned = 0, out = 0;
    for (auto& r : job->reports) { if (r.outOfRange) ++out; else if (! r.isReference) ++aligned; }
    g.status.message = "Aligned " + juce::String(aligned) + " track" + (aligned == 1 ? "" : "s") + " to "
                       + [&] { for (auto& r : job->reports) if (r.isReference) return r.name; return juce::String("the reference"); }()
                       + (out > 0 ? "  (" + juce::String(out) + " out of range, left as is)" : juce::String());
}

} // namespace GHSAlign
