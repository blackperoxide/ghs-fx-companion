#include "ToneAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

using GHSChainAxes::Axis;
using GHSChainAxes::AxisScores;
using GHSChainAxes::clampAxis;

// ---------------------------------------------------------------------------
// Recorder
// ---------------------------------------------------------------------------

void GHSToneAnalyzer::Recorder::prepare(double newSampleRate, int newNumChannels, double maxSeconds)
{
    const int capacity = juce::jmax(1, (int) (newSampleRate * maxSeconds));

    if ((int) sampleRate != (int) newSampleRate
        || capture.getNumChannels() != newNumChannels
        || capture.getNumSamples() != capacity)
    {
        sampleRate = newSampleRate;
        capture.setSize(newNumChannels, capacity, false, true, true);
        capture.clear();
        writePos.store(0, std::memory_order_release);
        recording.store(false, std::memory_order_release);
    }
}

void GHSToneAnalyzer::Recorder::startRecording()
{
    recording.store(false, std::memory_order_release);
    capture.clear();
    writePos.store(0, std::memory_order_release);
    recording.store(true, std::memory_order_release);
}

void GHSToneAnalyzer::Recorder::stopRecording()
{
    recording.store(false, std::memory_order_release);
}

void GHSToneAnalyzer::Recorder::pushBlock(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (!recording.load(std::memory_order_acquire))
        return;

    const int pos = writePos.load(std::memory_order_relaxed);
    const int capacity = capture.getNumSamples();
    const int numToCopy = juce::jmin(buffer.getNumSamples(), capacity - pos);

    if (numToCopy <= 0)
    {
        recording.store(false, std::memory_order_release);
        return;
    }

    const int channelsToCopy = juce::jmin(buffer.getNumChannels(), capture.getNumChannels());
    for (int ch = 0; ch < channelsToCopy; ++ch)
        capture.copyFrom(ch, pos, buffer, ch, 0, numToCopy);

    const int newPos = pos + numToCopy;
    writePos.store(newPos, std::memory_order_release);

    if (newPos >= capacity)
        recording.store(false, std::memory_order_release);
}

juce::AudioBuffer<float> GHSToneAnalyzer::Recorder::getCapturedCopy() const
{
    const int n = getNumCaptured();
    juce::AudioBuffer<float> result(juce::jmax(1, capture.getNumChannels()), n);
    for (int ch = 0; ch < capture.getNumChannels(); ++ch)
        result.copyFrom(ch, 0, capture, ch, 0, n);
    return result;
}

// ---------------------------------------------------------------------------
// analyze() - FFT band energy + dynamics + decay + modulation + width
// ---------------------------------------------------------------------------

namespace
{
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder; // 4096
    constexpr int hopSize = fftSize / 2;   // 2048, 50% overlap
    constexpr int maxFramesToAnalyze = 300; // bounds CPU on a long capture

    struct Bands { float sub = 0, low = 0, lowMid = 0, mid = 0, highMid = 0, air = 0, total = 0; };

    float binToHz(int bin, double sampleRate) { return (float) (bin * sampleRate / fftSize); }

    void accumulateBand(Bands& b, float hz, float magnitude)
    {
        b.total += magnitude;
        if (hz < 20.0f) return;
        if (hz < 60.0f) b.sub += magnitude;
        else if (hz < 160.0f) b.low += magnitude;
        else if (hz < 500.0f) b.lowMid += magnitude;
        else if (hz < 2000.0f) b.mid += magnitude;
        else if (hz < 6000.0f) b.highMid += magnitude;
        else b.air += magnitude;
    }

    /** value at x=x0 maps to 0, x=x1 maps to 10, clamped outside that range. */
    float scaleRange(float x, float x0, float x1)
    {
        if (x1 <= x0) return 0.0f;
        return clampAxis((x - x0) / (x1 - x0) * 10.0f);
    }
}

GHSChainAxes::AxisScores GHSToneAnalyzer::analyze(const juce::AudioBuffer<float>& audio, double sampleRate)
{
    AxisScores axes;
    // Neutral default (matches the web app's non-gating axis default) if there's
    // nothing to analyze - keeps a zero-length capture from reading as "all 0",
    // which would misleadingly read as "extremely dark/dry/narrow/clean".
    for (int i = 0; i < GHSChainAxes::numAxes; ++i)
        axes[(Axis) i] = 5.0f;

    const int numSamples = audio.getNumSamples();
    const int numChannels = audio.getNumChannels();
    if (numSamples < fftSize || numChannels < 1 || sampleRate <= 0.0)
        return axes;

    // Mono downmix for spectral/dynamics/decay analysis - width is measured
    // separately, straight off the original channels, below.
    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int ch = 0; ch < numChannels; ++ch)
        mono.addFrom(0, 0, audio, ch, 0, numSamples, 1.0f / (float) numChannels);

    const float globalPeak = mono.getMagnitude(0, 0, numSamples);
    const float globalRms = mono.getRMSLevel(0, 0, numSamples);
    const float crestDb = globalRms > 1.0e-8f ? juce::Decibels::gainToDecibels(globalPeak / globalRms) : 3.0f;

    // --- FFT pass: band energies + a short-time RMS envelope for movement/decay ---
    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    const int numFrames = juce::jmin(maxFramesToAnalyze, juce::jmax(1, 1 + (numSamples - fftSize) / hopSize));

    Bands totalBands;
    std::vector<float> envelope; // one short-time RMS value per frame
    envelope.reserve((size_t) numFrames);

    std::vector<float> fftData((size_t) fftSize * 2, 0.0f);
    const auto* monoData = mono.getReadPointer(0);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const int start = frame * hopSize;
        const int available = juce::jmin(fftSize, numSamples - start);
        if (available <= 0)
            break;

        std::fill(fftData.begin(), fftData.end(), 0.0f);
        std::copy(monoData + start, monoData + start + available, fftData.begin());
        window.multiplyWithWindowingTable(fftData.data(), fftSize);

        // Short-time RMS straight off the windowed frame, before the FFT overwrites it in place.
        float sumSq = 0.0f;
        for (int i = 0; i < available; ++i)
            sumSq += fftData[(size_t) i] * fftData[(size_t) i];
        envelope.push_back(std::sqrt(sumSq / (float) juce::jmax(1, available)));

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        for (int bin = 0; bin <= fftSize / 2; ++bin)
            accumulateBand(totalBands, binToHz(bin, sampleRate), fftData[(size_t) bin]);
    }

    if (totalBands.total > 1.0e-8f)
    {
        const float brightnessRatio = (totalBands.highMid + totalBands.air) / totalBands.total;
        axes[Axis::Brightness] = scaleRange(brightnessRatio, 0.04f, 0.35f);
    }

    // Punch: crest factor in dB - lots of headroom between peaks and average level
    // reads as punchy/dynamic; a squashed, already-compressed signal reads low.
    axes[Axis::Punch] = scaleRange(crestDb, 3.0f, 20.0f);

    // Drive: the inverse of punch (less headroom = pushed harder), blended with
    // how much energy lives above the fundamental/body range - a rough, honest
    // proxy for harmonic saturation, not a real distortion detector.
    const float highFreqDensity = totalBands.total > 1.0e-8f
                                       ? (totalBands.mid + totalBands.highMid + totalBands.air) / totalBands.total
                                       : 0.5f;
    axes[Axis::Drive] = clampAxis((10.0f - axes[Axis::Punch]) * 0.5f + scaleRange(highFreqDensity, 0.3f, 0.85f) * 0.5f);

    // Lofi: the weakest-grounded of these seven - restricted top-end plus a
    // little pull from Drive (tape/bitcrush character tends to ride along with
    // saturation). Audition this one by ear more than the others.
    axes[Axis::Lofi] = clampAxis((10.0f - axes[Axis::Brightness]) * 0.5f + axes[Axis::Drive] * 0.3f);

    // Movement: coefficient of variation of the short-time RMS envelope - a
    // tremolo/chorus/filter-sweep makes the envelope breathe; a static signal
    // doesn't. Imperfect on sparse transient material (a few isolated hits also
    // raise envelope variance), but reasonable on sustained/continuous audio.
    if (envelope.size() >= 4)
    {
        double mean = 0.0;
        for (float v : envelope) mean += v;
        mean /= (double) envelope.size();

        double variance = 0.0;
        for (float v : envelope) variance += (v - mean) * (v - mean);
        variance /= (double) envelope.size();

        const double stddev = std::sqrt(variance);
        const float coeffOfVariation = mean > 1.0e-8 ? (float) (stddev / mean) : 0.0f;
        axes[Axis::Movement] = scaleRange(coeffOfVariation, 0.15f, 0.65f);
    }

    // Space: how long the envelope takes to decay after its peak, relative to
    // that peak's own level - a rough decay-tail estimate, not a reverb detector.
    // A dry, immediately-cut signal reads low; a long sustained tail reads high.
    if (envelope.size() >= 2)
    {
        size_t peakFrame = 0;
        float peakVal = 0.0f;
        for (size_t i = 0; i < envelope.size(); ++i)
        {
            if (envelope[i] > peakVal) { peakVal = envelope[i]; peakFrame = i; }
        }

        const float floorLevel = peakVal * 0.01f; // -40dB relative to the peak frame
        size_t decayFrame = envelope.size() - 1;
        bool foundDecay = false;
        for (size_t i = peakFrame; i < envelope.size(); ++i)
        {
            if (envelope[i] <= floorLevel) { decayFrame = i; foundDecay = true; break; }
        }

        if (foundDecay)
        {
            const double decayMs = (double) (decayFrame - peakFrame) * hopSize / sampleRate * 1000.0;
            axes[Axis::Space] = scaleRange((float) decayMs, 100.0f, 2000.0f);
        }
        else
        {
            // The signal never actually dropped 40dB below its peak before the
            // capture ended - it's still sustaining (a held note, a continuous
            // pad), not necessarily "decaying slowly in a long reverb tail".
            // Reading that as near-max Space would confuse "hasn't stopped yet"
            // with "wet" - fall back to a low, honest "can't tell yet" value
            // instead of extrapolating a tail we never actually observed.
            axes[Axis::Space] = 2.0f;
        }
    }

    // Width: L/R correlation on the original (non-downmixed) channels. Mono
    // input can't be wide by definition.
    if (numChannels >= 2)
    {
        const auto* left = audio.getReadPointer(0);
        const auto* right = audio.getReadPointer(1);

        double sumL = 0, sumR = 0, sumLR = 0, sumL2 = 0, sumR2 = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            sumL += left[i]; sumR += right[i];
            sumLR += (double) left[i] * right[i];
            sumL2 += (double) left[i] * left[i];
            sumR2 += (double) right[i] * right[i];
        }

        const double n = (double) numSamples;
        const double covariance = sumLR / n - (sumL / n) * (sumR / n);
        const double varL = sumL2 / n - (sumL / n) * (sumL / n);
        const double varR = sumR2 / n - (sumR / n) * (sumR / n);
        const double denom = std::sqrt(juce::jmax(1.0e-12, varL * varR));
        const double correlation = juce::jlimit(-1.0, 1.0, denom > 0.0 ? covariance / denom : 1.0);

        axes[Axis::Width] = clampAxis((float) ((1.0 - correlation) * 5.0));
    }
    else
    {
        axes[Axis::Width] = 1.0f;
    }

    for (int i = 0; i < GHSChainAxes::numAxes; ++i)
        axes[(Axis) i] = clampAxis(axes[(Axis) i]);

    return axes;
}
