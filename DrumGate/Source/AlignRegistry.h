#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Cross-instance alignment: every GHS Drum Gate instance in the session
 * registers here. Pressing Analyse in any one of them records the same
 * stretch of the song (by host timeline position) on every instance in its
 * group, analyses each mic against the reference on a background thread,
 * and hands every instance its own time offset and polarity / phase.
 *
 * DAWs run AU v2 / VST3 plugins of a session in one process, so a process-
 * wide registry sees them all. (A host that sandboxes each plugin in its own
 * process would show each instance alone in its group.)
 */
namespace GHSAlign
{

static constexpr int numGroups = 4;
static constexpr double maxAlignMs = 10.0;   // +-10 ms ~ +-3.4 m of mic distance
static constexpr double captureSeconds = 6.0;
static constexpr double minCaptureSeconds = 1.5;

/** One instance's recording of its own input, filled by its audio thread. */
struct CaptureBuffer
{
    enum State { Idle = 0, Armed = 1, Recording = 2, Full = 3, Frozen = 4 };

    std::vector<float> data;          // sized on the message thread when armed
    double sampleRate = 44100.0;
    std::atomic<int> state { Idle };
    std::atomic<juce::int64> startTime { 0 }; // host timeline sample of data[0]
    std::atomic<int> written { 0 };
    std::atomic<bool> busy { false }; // audio thread is inside write()

    /** Message thread. */
    void arm(double sr, double seconds);

    /** Audio thread: appends this block (mono sum) if armed/recording and the transport is rolling. */
    void write(const float* const* channels, int numChannels, int numSamples,
               bool playing, std::optional<juce::int64> timeInSamples);

    /** Message thread: stops the audio thread writing (waits for an in-flight block to finish). */
    void freeze();
};

/** What an instance needs to apply its alignment. */
struct MemberAlignment
{
    bool valid = false;
    double offsetSeconds = 0.0; // this mic's arrival relative to the reference (positive = later)
    double phaseRad = 0.0;      // rotation (pi = polarity flip)
    bool usePhase = false;      // rotation via allpass pair, vs. polarity only
};

/** One row of the Align panel's table. */
struct MemberReport
{
    int memberId = 0;
    juce::String name;
    bool isReference = false;
    bool analysed = false;
    double lagMs = 0.0;
    double phaseDeg = 0.0;
    double corrBefore = 0.0, corrAfter = 0.0;
    double confidence = 0.0;
    bool outOfRange = false;
};

/** Implemented by the plugin processor. All calls on the message thread. */
class AlignMember
{
public:
    virtual ~AlignMember() = default;
    virtual int getAlignGroup() const = 0;
    virtual bool isAlignReference() const = 0;
    virtual juce::String getAlignName() const = 0;
    virtual CaptureBuffer& getAlignCapture() = 0;
    virtual double getAlignSampleRate() const = 0;
    virtual void applyAlignment(const MemberAlignment&) = 0;
    int alignMemberId = 0; // assigned by the registry
};

class Registry : private juce::Timer
{
public:
    enum class Phase { Idle, Capturing, Analysing, Done, Error };

    struct GroupStatus
    {
        Phase phase = Phase::Idle;
        juce::String message;
        float progress = 0.0f;           // capture progress 0..1
        std::vector<MemberReport> reports; // last analysis
    };

    Registry() = default;
    ~Registry() override;

    void addMember(AlignMember*);
    void removeMember(AlignMember*);

    /** Arms capture on every instance in the group. The result arrives asynchronously. */
    void startAnalysis(int group, bool usePhase);
    void cancel(int group);
    /** Clears every instance's alignment in the group. */
    void resetGroup(int group);

    GroupStatus getStatus(int group) const;
    /** Name / reference flag of every instance currently in the group. */
    std::vector<MemberReport> listMembers(int group) const;

private:
    struct Job;
    void timerCallback() override;
    void beginAnalysis(int group);
    void finishJob(int group);
    std::vector<AlignMember*> membersOf(int group) const;

    mutable juce::CriticalSection lock;
    std::vector<AlignMember*> members;
    int nextId = 1;

    struct GroupState
    {
        GroupStatus status;
        bool usePhase = false;
        juce::uint32 startedMs = 0, lastProgressMs = 0;
        int lastWritten = -1;
        std::shared_ptr<Job> job;
    };
    GroupState groups[numGroups];
};

} // namespace GHSAlign
