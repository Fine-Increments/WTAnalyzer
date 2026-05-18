/*
  ==============================================================================

    FarinaIR.h
    Impulse-response extraction from a log sine sweep via Farina
    deconvolution.

    Algorithm:
      1. User configures sweep parameters (f0, f1, duration, tail length)
         and clicks Capture.
      2. Audio thread continuously ring-buffers the post signal while it
         watches pre for a level-threshold crossing. The crossing only has
         to land somewhere on the sweep's rising onset - it is a coarse
         "the sweep is playing now" arm, not a precise time origin. A
         measurement sweep fades in, so the threshold trips a few ms late;
         the pre-roll ring buffer preserves the samples before the trigger
         so the captured sweep is never truncated.
      3. On the trigger the pre-roll is spliced in front of the capture and
         recording continues for (preroll + sweep + tail) seconds.
      4. Message thread generates the inverse-sweep filter mathematically
         from the same parameters, FFT-convolves it with the captured post,
         and locates the linear IR by the deconvolution peak (see below).
      5. Display reads the resulting time-domain IR.

    The inverse filter for a Farina log sweep
        s(t) = sin( K * (e^(t*L/T) - 1) )
    with K = 2*pi*f0*T/L and L = ln(f1/f0) is
        f(t) = s(T - t) * e^(-t*L/T)
    so that s * f equals a delta at t = T. The linear-IR component of the
    deconvolution therefore sits at (sweep_onset + sweep_duration_samples)
    within the convolution result. The onset is not known to the sample -
    it lies somewhere within the pre-roll region - so the linear IR is
    found by searching for the largest-magnitude sample in a bounded window
    around that expected position. The harmonic-distortion IRs sit seconds
    earlier and far lower in level, so the peak in that window is
    unambiguously the linear IR's direct arrival.

    One-shot capture: a single sweep distributes test energy across the
    entire spectrum and across time, so SNR is already enormous; we don't
    need multi-capture averaging like the Direct Impulse IR mode does.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

class FarinaIR
{
public:
    static constexpr float kMinF0Hz       = 5.0f;
    static constexpr float kMaxF0Hz       = 20000.0f;
    static constexpr float kDefaultF0Hz   = 20.0f;
    static constexpr float kMinF1Hz       = 10.0f;
    static constexpr float kMaxF1Hz       = 24000.0f;
    static constexpr float kDefaultF1Hz   = 20000.0f;

    // Maximum supported sweep + tail in seconds. Drives the FFT order
    // allocated at prepareToPlay; chosen to balance "covers most
    // reverbs" against worst-case memory at 192 kHz sessions.
    static constexpr float kMaxSweepSec    = 30.0f;
    static constexpr float kMinSweepSec    = 0.1f;
    static constexpr float kDefaultSweepSec = 10.0f;

    static constexpr float kMaxTailSec     = 10.0f;
    static constexpr float kMinTailSec     = 0.05f;
    static constexpr float kDefaultTailSec = 1.0f;

    // Pre-signal threshold that arms the capture once the user has clicked
    // Capture. It only needs to fire somewhere on the sweep's rising onset:
    // the pre-roll ring buffer recovers the samples before it and the peak
    // search locates the exact IR position, so this is deliberately coarse.
    static constexpr float kTriggerThreshold = 0.05f;

    // Length of post signal ring-buffered ahead of the trigger, so a sweep
    // that fades in is never truncated; and the half-width of the window
    // the linear IR is searched for around its expected position.
    static constexpr float kPreRollSec  = 0.1f;
    static constexpr float kIRSearchSec = 0.05f;

    enum class State
    {
        Idle,             // No capture in progress; ir buffer may hold a previous result.
        Armed,            // User clicked Capture; waiting for pre to cross the threshold.
        Capturing,        // Recording post into postCapture.
        ReadyToProcess,   // Audio thread finished recording; UI thread will deconvolve next tick.
        IRReady           // Deconvolution complete; ir buffer is valid.
    };

    enum class Channel { L, R };

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // Sets the sweep parameters to use for the next capture. Safe to call
    // any time from the message thread; takes effect on the next capture.
    void setSweepParams (float f0Hz, float f1Hz, float durationSec, float tailSec);

    void requestCapture();   // Arms both channels' triggers.

    // Audio-thread entry. Called per sample with the current pre and post
    // values for each channel. Each channel runs its own trigger
    // detection and capture; both share the deconvolution FFT scratch on
    // the message thread.
    void processSample (float preL, float postL, float preR, float postR);

    // Message-thread entry. Call from a Timer; performs the deconvolution
    // for whichever channel(s) have completed capture since the last call.
    void tryProcessCapture();

    // Aggregate state for "what should the display show as a status?".
    // Returns the worst-progress channel state - i.e. Capturing if either
    // channel is still capturing; Armed if either is armed; etc.
    State getState() const noexcept;

    int   getCaptureProgress (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).captureProgress.load (std::memory_order_relaxed);
    }
    int   getCaptureLength   (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).captureLength.load (std::memory_order_relaxed);
    }
    int   getIRLength        (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).irLength.load (std::memory_order_relaxed);
    }

    const std::vector<float>& getIR (Channel ch = Channel::L) const noexcept
    {
        return ch == Channel::L ? chL.ir : chR.ir;
    }
    float getSampleRate() const noexcept { return sampleRate; }

    // Bumped whenever a deconvolution completes or the state is reset, so
    // the CSD view can tell when the IR has changed and recompute.
    int getIRGeneration() const noexcept
    {
        return irGeneration.load (std::memory_order_relaxed);
    }

    // For the display - read back the currently-set sweep parameters.
    float getF0Hz()        const noexcept { return f0Hz; }
    float getF1Hz()        const noexcept { return f1Hz; }
    float getSweepSec()    const noexcept { return sweepDurationSec; }
    float getTailSec()     const noexcept { return tailSec; }

private:
    struct ChannelState
    {
        std::vector<float> preRoll;            // ring buffer of pre-trigger post samples
        std::vector<float> postCapture;
        std::vector<float> ir;
        int                preRollWrite = 0;   // ring-buffer write cursor
        std::atomic<State> state           { State::Idle };
        std::atomic<int>   captureProgress { 0 };
        std::atomic<int>   captureLength   { 0 };
        std::atomic<int>   irLength        { 0 };
    };

    void generateInverseSweep (std::vector<float>& out,
                               float f0, float f1, float durationSec);
    void runDeconvolution (ChannelState& ch);
    void resetChannel     (ChannelState& ch);
    void processChannel   (ChannelState& ch, float preSample, float postSample);

    // Allocates / re-allocates buffers and the FFT object to handle the
    // requested capture and filter sizes. Idempotent and cheap when the
    // current capacity already covers the request. Called from the
    // message thread inside requestCapture().
    void ensureCapacity (int captureSamples, int filterSamples, int preRollSamples);

    float sampleRate = 48000.0f;

    int   fftOrder = 0;
    int   fftSize  = 0;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> inverseSweep;
    std::vector<float> postScratch;
    std::vector<float> invScratch;

    ChannelState chL;
    ChannelState chR;

    float f0Hz             = kDefaultF0Hz;
    float f1Hz             = kDefaultF1Hz;
    float sweepDurationSec = kDefaultSweepSec;
    float tailSec          = kDefaultTailSec;

    std::atomic<int> irGeneration { 0 };
};
