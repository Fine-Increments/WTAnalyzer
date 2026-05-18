/*
  ==============================================================================

    MlsIR.h
    Impulse-response extraction from a maximum-length-sequence (MLS) stimulus
    by cross-correlation.

    Algorithm:
      1. User sets the MLS order - it must match the order WTGenerator's MLS
         generator is running. The sequence period is L = 2^order - 1.
      2. Audio thread waits for the pre signal to cross a level threshold,
         then captures pre and post together for two full periods (2L
         samples each).
      3. The first period is the device settling into steady state under the
         periodic MLS; the second period is the steady-state response.
      4. Message thread cross-correlates the second post period against the
         second pre period (FFT: post * conj(pre), inverse transform). An MLS
         has a near-flat spectrum, so its autocorrelation is essentially a
         delta - the correlation is therefore the device's impulse response,
         scaled by ~L. The linear IR is peak-located near zero lag (which
         absorbs device latency) and the leading `tail` seconds are kept.
      5. Display reads the resulting time-domain IR.

    Using the recorded pre as the correlation reference - rather than a
    regenerated sequence - keeps pre and post sample-aligned and folds device
    latency into the IR position automatically. The analyzer only needs the
    MLS period (from the order), not its tap polynomial.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

class MlsIR
{
public:
    // MLS order range - matches WTGenerator's MLS generator (order 2..20).
    static constexpr int kMinOrder     = 2;
    static constexpr int kMaxOrder     = 20;
    static constexpr int kDefaultOrder = 16;

    // Displayed IR window (the recovered IR is a full period long; only the
    // leading portion carries the response, the rest is the noise floor).
    static constexpr float kMinTailSec     = 0.05f;
    static constexpr float kMaxTailSec     = 10.0f;
    static constexpr float kDefaultTailSec = 1.0f;

    // Pre threshold that arms the capture - the MLS plays at full level, so
    // this only confirms the stimulus is present before grabbing samples.
    static constexpr float kTriggerThreshold = 0.05f;

    // Half-width of the window the IR peak is searched for around zero lag,
    // so moderate device latency is absorbed without a manual offset.
    static constexpr float kIRSearchSec = 0.05f;

    enum class State
    {
        Idle,
        Armed,
        Capturing,
        ReadyToProcess,
        IRReady
    };

    enum class Channel { L, R };

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // Sets the MLS order (sequence period) and displayed IR window. Safe to
    // call any time from the message thread; takes effect on the next capture.
    void setParams (int order, float tailSec);

    void requestCapture();   // Arms both channels.

    // Audio-thread entry - per sample, current pre/post for each channel.
    void processSample (float preL, float postL, float preR, float postR);

    // Message-thread entry. Call from a Timer; runs the correlation for any
    // channel that has finished capturing since the last call.
    void tryProcessCapture();

    State getState() const noexcept;

    int getCaptureProgress (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).captureProgress.load (std::memory_order_relaxed);
    }
    int getCaptureLength (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).captureLength.load (std::memory_order_relaxed);
    }
    int getIRLength (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).irLength.load (std::memory_order_relaxed);
    }
    const std::vector<float>& getIR (Channel ch = Channel::L) const noexcept
    {
        return ch == Channel::L ? chL.ir : chR.ir;
    }

    float getSampleRate() const noexcept { return sampleRate; }
    int   getOrder()      const noexcept { return order; }
    float getTailSec()    const noexcept { return tailSec; }

    // Bumped whenever a correlation completes or the state is reset, so a
    // downstream view (e.g. CSD) can tell when the IR has changed.
    int getIRGeneration() const noexcept
    {
        return irGeneration.load (std::memory_order_relaxed);
    }

private:
    struct ChannelState
    {
        std::vector<float> preCapture;
        std::vector<float> postCapture;
        std::vector<float> ir;
        std::atomic<State> state           { State::Idle };
        std::atomic<int>   captureProgress { 0 };
        std::atomic<int>   captureLength   { 0 };
        std::atomic<int>   irLength        { 0 };
    };

    void runCorrelation (ChannelState& ch);
    void resetChannel   (ChannelState& ch);
    void processChannel (ChannelState& ch, float preSample, float postSample);
    void ensureCapacity (int captureSamples);

    static int periodFor (int ord) noexcept { return (1 << ord) - 1; }

    float sampleRate = 48000.0f;

    int   fftOrder = 0;
    int   fftSize  = 0;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> scratchA;   // post spectrum / correlation result
    std::vector<float> scratchB;   // pre spectrum

    ChannelState chL;
    ChannelState chR;

    int   order   = kDefaultOrder;
    float tailSec = kDefaultTailSec;

    std::atomic<int> irGeneration { 0 };
};
