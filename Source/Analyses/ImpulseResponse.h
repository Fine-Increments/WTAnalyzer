/*
  ==============================================================================

    ImpulseResponse.h
    Time-domain impulse-response capture.

    Algorithm: a level threshold on the pre signal detects each impulse.
    On trigger, we record kWindowSamples of the post signal into a capture
    buffer, then add it into a running average. Re-trigger is gated until
    the current capture completes - if impulses arrive faster than the
    window length, the in-between ones are ignored (the capture finishes
    first, then the next impulse re-arms the trigger). Averaging continues
    indefinitely until the display calls reset().

    Window length is settable in milliseconds at runtime, but the
    underlying buffers are allocated once in prepare() at kMaxWindowMs
    (long enough for the most extreme reverb tails) so the audio thread
    never allocates.

    Consumes raw pre/post audio samples (not the spectrum FFT output),
    so this is the first time-domain analysis in the suite.

  ==============================================================================
*/

#pragma once

#include <atomic>
#include <vector>

class ImpulseResponse
{
public:
    // Maximum capture window. 2 minutes at 192 kHz worst case = 23M samples
    // per buffer; at 48 kHz it's 5.76M (~23 MB at float). Chosen to cover
    // even pad-style supermassive reverbs that ring out for a minute-plus
    // before going inaudible.
    static constexpr int kMaxWindowMs = 120000;

    static constexpr int kMinWindowMs = 50;
    static constexpr int kDefaultWindowMs = 250;

    static constexpr int kMinAverages     = 1;
    static constexpr int kMaxAverages     = 64;
    static constexpr int kDefaultAverages = 4;

    // Pre signal must cross this linear level (absolute value) to trigger
    // a capture. Equivalent to ~-40 dB FS - kept loose so an impulse set
    // well below full scale still triggers reliably, while staying clear
    // of typical noise floors. False triggers on tone are gated by the
    // per-capture
    // window holdoff, not the threshold itself.
    static constexpr float kTriggerThresholdLinear = 0.01f;

    enum class Channel { L, R };

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // User-facing settings. Safe to call from the UI thread; the audio
    // thread reads atomics on its next sample.
    void setWindowMs    (int ms);
    void setAverageGoal (int count);

    // UI-thread Clear. Defers the actual reset to the audio thread (consumed at
    // the top of processSample) so it never races processChannel's non-atomic
    // state/index members. Do not call reset() directly from the message thread.
    void requestClear() noexcept { clearRequested.store (true, std::memory_order_relaxed); }

    // Audio-thread entry. Called per-sample with the pre and post values
    // at this sample index for each channel. Pre is used for trigger
    // detection; post is what gets captured.
    void processSample (float preL, float postL, float preR, float postR);

    // UI-thread accessors. Each channel exposes its own averaged buffer
    // and capture stats. The unsuffixed names return the L channel for
    // backwards compatibility with pre-stereo callers.
    const std::vector<float>& getAveragedBuffer (Channel ch = Channel::L) const noexcept
    {
        return ch == Channel::L ? chL.averaged : chR.averaged;
    }
    int   getDisplayLength     (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).displayLengthAtomic.load (std::memory_order_relaxed);
    }
    int   getCompletedCaptures (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).completedCaptures.load (std::memory_order_relaxed);
    }
    int   getAverageGoal()         const noexcept { return averageGoal.load (std::memory_order_relaxed); }
    float getSampleRate()          const noexcept { return sampleRate; }

    // True iff at least one capture on the indicated channel has
    // completed since the last reset.
    bool  hasAnyCapture (Channel ch = Channel::L) const noexcept
    {
        return getCompletedCaptures (ch) > 0;
    }

private:
    enum class State { Idle, Capturing };

    struct ChannelState
    {
        std::vector<float> capture;
        std::vector<float> averaged;
        State state = State::Idle;
        int   captureIdx = 0;
        int   sinceLastCompletion = 0;
        std::atomic<int> displayLengthAtomic { 0 };
        std::atomic<int> completedCaptures   { 0 };
    };

    float sampleRate = 48000.0f;
    int   maxWindowSamples = 0;

    ChannelState chL;
    ChannelState chR;

    std::atomic<int>  windowSamples  { 0 };
    std::atomic<int>  averageGoal    { kDefaultAverages };
    std::atomic<bool> clearRequested { false };

    void resetChannel  (ChannelState& ch);
    void invalidateChannel (ChannelState& ch);
    void processChannel (ChannelState& ch, float preSample, float postSample, int winSamps);
};
