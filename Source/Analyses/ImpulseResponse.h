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
    // a capture. Equivalent to ~-20 dB FS. Most impulse-train scripts
    // peak around -3 to -6 dB FS so this is conservative.
    static constexpr float kTriggerThresholdLinear = 0.1f;

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // User-facing settings. Safe to call from the UI thread; the audio
    // thread reads atomics on its next sample.
    void setWindowMs    (int ms);
    void setAverageGoal (int count);

    // Audio-thread entry. Called per-sample with the pre and post values
    // at this sample index. preSample is used for trigger detection;
    // postSample is what we capture.
    void processSample (float preSample, float postSample);

    // UI-thread accessors. The buffer is the running average of all
    // completed captures since the last reset(); displayLength is how
    // many samples of it are valid for the current window setting.
    const std::vector<float>& getAveragedBuffer() const noexcept { return averaged; }
    int   getDisplayLength()       const noexcept { return displayLengthAtomic.load (std::memory_order_relaxed); }
    int   getCompletedCaptures()   const noexcept { return completedCaptures.load (std::memory_order_relaxed); }
    int   getAverageGoal()         const noexcept { return averageGoal.load (std::memory_order_relaxed); }
    float getSampleRate()          const noexcept { return sampleRate; }

    // True iff at least one capture has completed since the last reset.
    bool  hasAnyCapture() const noexcept { return getCompletedCaptures() > 0; }

private:
    enum class State { Idle, Capturing };

    float sampleRate = 48000.0f;
    int   maxWindowSamples = 0;

    // Triple of buffers:
    //   capture - the current in-flight capture (written sample by sample)
    //   averaged - running sum / completedCaptures of all completed captures
    //              (this is what the UI reads)
    //   scratch  - holds the just-completed capture while it's being added
    //              into 'averaged'; lets the audio thread keep capturing
    //              the next impulse without stomping on the running average
    //              read by the UI. (Not strictly needed since the merge is
    //              a tight loop, but it makes the lifetimes obvious.)
    std::vector<float> capture;
    std::vector<float> averaged;

    State state = State::Idle;
    int   captureIdx = 0;

    std::atomic<int> windowSamples       { 0 };
    std::atomic<int> displayLengthAtomic { 0 };
    std::atomic<int> averageGoal         { kDefaultAverages };
    std::atomic<int> completedCaptures   { 0 };

    // Wall-clock since the last completed capture, in samples. Used as a
    // soft re-trigger holdoff so that a long resonant tail in pre doesn't
    // immediately re-arm before the capture really finishes.
    int sinceLastCompletion = 0;
};
