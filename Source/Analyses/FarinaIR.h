/*
  ==============================================================================

    FarinaIR.h
    Impulse-response extraction from a log sine sweep via Farina
    deconvolution.

    Algorithm:
      1. User configures sweep parameters (f0, f1, duration, tail length)
         and clicks Capture.
      2. Audio thread watches pre for a level threshold crossing - the
         sweep onset - and on detection records the post signal for
         (sweep_duration + tail) seconds.
      3. Message thread generates the inverse-sweep filter
         mathematically from the same parameters, FFT-convolves it with
         the captured post, and extracts the linear IR from the result.
      4. Display reads the resulting time-domain IR.

    The inverse filter for a Farina log sweep
        s(t) = sin( K * (e^(t*L/T) - 1) )
    with K = 2*pi*f0*T/L and L = ln(f1/f0) is
        f(t) = s(T - t) * e^(-t*L/T)
    so that s * f equals a delta at t = T (i.e. the deconvolution
    output's linear-IR component is positioned starting at sample
    sweep_duration_samples within the convolution result).

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

    // Pre-signal threshold for sweep-onset auto-detection after the user
    // arms a capture. Loose; the sweep starts at full amplitude so this
    // catches the first sample.
    static constexpr float kTriggerThreshold = 0.05f;

    enum class State
    {
        Idle,             // No capture in progress; ir buffer may hold a previous result.
        Armed,            // User clicked Capture; waiting for pre to cross the threshold.
        Capturing,        // Recording post into postCapture.
        ReadyToProcess,   // Audio thread finished recording; UI thread will deconvolve next tick.
        IRReady           // Deconvolution complete; ir buffer is valid.
    };

    // prepare() is intentionally lightweight: it only stores the sample
    // rate and clears state. Heavy buffer / FFT allocation happens
    // lazily inside requestCapture() so plugin instantiation stays fast
    // at any sample rate (the worst-case FFT here is large and JUCE's
    // precomputed twiddle tables take measurable time to build).
    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // Sets the sweep parameters to use for the next capture. Safe to call
    // any time from the message thread; takes effect on the next capture.
    void setSweepParams (float f0Hz, float f1Hz, float durationSec, float tailSec);

    void requestCapture();   // Arms the trigger; audio thread starts watching pre.

    // Audio-thread entry. Called per sample with the current pre and post
    // values. Cheap when state == Idle / IRReady / ReadyToProcess.
    void processSample (float preSample, float postSample);

    // Message-thread entry. Call from a Timer; performs the deconvolution
    // if a capture has completed and a result isn't already ready.
    void tryProcessCapture();

    State getState() const noexcept { return state.load (std::memory_order_acquire); }

    int   getCaptureProgress() const noexcept { return captureProgress.load (std::memory_order_relaxed); }
    int   getCaptureLength()   const noexcept { return captureLength  .load (std::memory_order_relaxed); }
    int   getIRLength()        const noexcept { return irLength       .load (std::memory_order_relaxed); }

    const std::vector<float>& getIR() const noexcept { return ir; }
    float getSampleRate() const noexcept { return sampleRate; }

    // For the display - read back the currently-set sweep parameters.
    float getF0Hz()        const noexcept { return f0Hz; }
    float getF1Hz()        const noexcept { return f1Hz; }
    float getSweepSec()    const noexcept { return sweepDurationSec; }
    float getTailSec()     const noexcept { return tailSec; }

private:
    void generateInverseSweep (std::vector<float>& out,
                               float f0, float f1, float durationSec);
    void runDeconvolution();

    // Allocates / re-allocates buffers and the FFT object to handle the
    // requested capture and filter sizes. Idempotent and cheap when the
    // current capacity already covers the request. Called from the
    // message thread inside requestCapture().
    void ensureCapacity (int captureSamples, int filterSamples);

    float sampleRate     = 48000.0f;

    // FFT capacity sized once in prepare() for the worst-case
    // (maxSweep + maxTail) at the current sample rate. The runtime
    // capture / inverse filter are zero-padded into this fixed size.
    int   fftOrder       = 0;
    int   fftSize        = 0;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> postCapture;   // raw post samples while capturing
    std::vector<float> inverseSweep;  // generated mathematically per capture
    std::vector<float> ir;            // extracted IR after deconvolution
    std::vector<float> postScratch;   // 2 * fftSize for FFT work
    std::vector<float> invScratch;    // 2 * fftSize for FFT work

    std::atomic<State> state           { State::Idle };
    std::atomic<int>   captureProgress { 0 };
    std::atomic<int>   captureLength   { 0 };
    std::atomic<int>   irLength        { 0 };

    // Current sweep params (set by UI). Audio thread reads only the
    // capture length derived from these; the values themselves are read
    // by the message thread for deconvolution.
    float f0Hz             = kDefaultF0Hz;
    float f1Hz             = kDefaultF1Hz;
    float sweepDurationSec = kDefaultSweepSec;
    float tailSec          = kDefaultTailSec;
};
