#pragma once

#include "ChainAxes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

/**
 * Captures live audio from this plugin's own input (so it works on whatever
 * track it's inserted on in Logic - no separate audio-routing setup) and
 * estimates where that audio sits in the same 7-axis space the web app's
 * text-prompt matcher uses, so GHSDrumChainVariants::buildDrumChain() can
 * suggest a chain for it exactly the same way it would for a typed prompt.
 *
 * This characterizes the *raw* tone that's already coming in (bright/dark,
 * driven/clean, wide/narrow, etc.) - it does not detect "what plugins are
 * already on this track." Treat every axis value as a best-effort DSP
 * estimate to audition and adjust by ear, not a measurement - there is no
 * ground truth to check it against, the same honest framing the web app
 * uses for its own audio analyzer.
 */
namespace GHSToneAnalyzer
{
    /**
     * Bounded capture buffer, safe to feed from the audio thread. Not a true
     * wraparound ring - once full, recording stops automatically so
     * getCapturedCopy() always returns a clean, bounded take rather than
     * silently overwriting the start of a long one.
     */
    class Recorder
    {
    public:
        /** Message-thread only. Reallocates only if sampleRate/numChannels actually changed. */
        void prepare(double newSampleRate, int newNumChannels, double maxSeconds = 20.0);

        /** Message-thread only. Clears any previous capture and starts filling from sample 0. */
        void startRecording();

        /** Message-thread only. Safe to call even if already stopped (e.g. auto-stopped on full). */
        void stopRecording();

        bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }

        /** Audio-thread. A no-op (single atomic load, no allocation) when not recording. */
        void pushBlock(const juce::AudioBuffer<float>& buffer) noexcept;

        double getSampleRate() const noexcept { return sampleRate; }

        /** Message-thread only. Samples captured per channel so far (capped at capacity). */
        int getNumCaptured() const noexcept { return writePos.load(std::memory_order_acquire); }

        double getCapturedSeconds() const noexcept { return sampleRate > 0.0 ? (double) getNumCaptured() / sampleRate : 0.0; }

        /** Message-thread only. Copies out exactly the captured portion. */
        juce::AudioBuffer<float> getCapturedCopy() const;

    private:
        juce::AudioBuffer<float> capture;
        std::atomic<bool> recording { false };
        std::atomic<int> writePos { 0 };
        double sampleRate = 44100.0;
    };

    /**
     * Runs FFT-based band-energy, dynamics, decay, modulation, and stereo-width
     * analysis over a captured buffer and maps the result onto the 7 axes.
     * NOT real-time safe (allocates, runs FFTs) - always call from a background
     * thread, never from processBlock.
     */
    GHSChainAxes::AxisScores analyze(const juce::AudioBuffer<float>& audio, double sampleRate);
}
