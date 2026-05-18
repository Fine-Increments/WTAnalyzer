/*
  ==============================================================================

    StepResponse.h
    Time-domain step-response capture for a step stimulus.

    Algorithm:
      1. Audio thread continuously ring-buffers the post signal while it
         watches pre for a rising-edge threshold crossing.
      2. A step can rise slowly (WTGenerator's Step has a settable rise time),
         so the threshold trips part-way up the edge. The pre-roll ring buffer
         preserves the samples before the trigger, so the foot of the step -
         and the pre-step baseline - are never lost.
      3. On the trigger the pre-roll is spliced in front of the capture and
         the post signal is recorded for the window length.
      4. Message thread derives the headline metrics from the captured
         waveform: baseline, settled level, 10-90% rise time and overshoot.
      5. Display reads the captured response waveform and the metrics.

    Sample index `preRollSamples` in the response buffer is t = 0 (the step
    edge); everything before it is the pre-step baseline.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

class StepResponse
{
public:
    // Capture window after the step edge.
    static constexpr float kMinWindowMs     = 2.0f;
    static constexpr float kMaxWindowMs     = 2000.0f;
    static constexpr float kDefaultWindowMs = 50.0f;

    // Baseline shown before the step edge, captured via the pre-roll buffer.
    static constexpr float kPreRollMs = 5.0f;

    // Rising-edge threshold on the pre signal that arms the capture. Coarse:
    // the pre-roll recovers the foot of the step regardless of where it trips.
    static constexpr float kTriggerThreshold = 0.05f;

    enum class State
    {
        Idle,
        Armed,
        Capturing,
        ReadyToProcess,
        Ready
    };

    enum class Channel { L, R };

    void prepare (double sampleRate, int samplesPerBlock);
    void reset();

    // Sets the post-step capture window. Message thread; next capture.
    void setParams (float windowMs);

    void requestCapture();   // Arms both channels.

    void processSample (float preL, float postL, float preR, float postR);

    // Message-thread entry. Call from a Timer; derives the metrics for any
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
    int getResponseLength (Channel ch = Channel::L) const noexcept
    {
        return (ch == Channel::L ? chL : chR).responseLength.load (std::memory_order_relaxed);
    }
    const std::vector<float>& getResponse (Channel ch = Channel::L) const noexcept
    {
        return ch == Channel::L ? chL.response : chR.response;
    }

    // Headline metrics (valid once getState() reports Ready).
    float getBaseline        (Channel ch = Channel::L) const noexcept { return pick (ch).baseline; }
    float getSettledLevel    (Channel ch = Channel::L) const noexcept { return pick (ch).settledLevel; }
    float getRiseTimeMs      (Channel ch = Channel::L) const noexcept { return pick (ch).riseTimeMs; }
    float getOvershootPct    (Channel ch = Channel::L) const noexcept { return pick (ch).overshootPct; }

    float getSampleRate()      const noexcept { return sampleRate; }
    int   getPreRollSamples()  const noexcept { return preRollSamples; }
    float getWindowMs()        const noexcept { return windowMs; }

    int getResponseGeneration() const noexcept
    {
        return responseGeneration.load (std::memory_order_relaxed);
    }

private:
    struct ChannelState
    {
        std::vector<float> preRoll;            // ring buffer of pre-trigger post samples
        std::vector<float> response;           // [pre-roll baseline][step + settling]
        int                preRollWrite = 0;
        std::atomic<State> state           { State::Idle };
        std::atomic<int>   captureProgress { 0 };
        std::atomic<int>   captureLength   { 0 };
        std::atomic<int>   responseLength  { 0 };

        // Metrics, published by the state store after a capture.
        float baseline     = 0.0f;
        float settledLevel = 0.0f;
        float riseTimeMs   = 0.0f;
        float overshootPct = 0.0f;
    };

    const ChannelState& pick (Channel ch) const noexcept
    {
        return ch == Channel::L ? chL : chR;
    }

    void runMetrics     (ChannelState& ch);
    void resetChannel   (ChannelState& ch);
    void processChannel (ChannelState& ch, float preSample, float postSample);
    void ensureCapacity (int captureSamples, int preRollLen);

    float sampleRate     = 48000.0f;
    int   preRollSamples = 0;

    ChannelState chL;
    ChannelState chR;

    float windowMs = kDefaultWindowMs;

    std::atomic<int> responseGeneration { 0 };
};
