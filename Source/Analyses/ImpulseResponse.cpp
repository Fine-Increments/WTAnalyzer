/*
  ==============================================================================

    ImpulseResponse.cpp

  ==============================================================================
*/

#include "ImpulseResponse.h"

#include <algorithm>
#include <cmath>

void ImpulseResponse::prepare (double sr, int /*samplesPerBlock*/)
{
    sampleRate       = (float) sr;
    maxWindowSamples = (int) std::ceil (kMaxWindowMs * 0.001 * sr);

    capture .assign ((size_t) maxWindowSamples, 0.0f);
    averaged.assign ((size_t) maxWindowSamples, 0.0f);

    // Default window: kDefaultWindowMs, converted to samples at the
    // current rate. Stored atomically so setWindowMs() can update it
    // later without re-allocating the buffers.
    const int defaultSamples = (int) std::ceil (kDefaultWindowMs * 0.001 * sr);
    windowSamples      .store (std::min (defaultSamples, maxWindowSamples), std::memory_order_relaxed);
    displayLengthAtomic.store (0, std::memory_order_relaxed);
    completedCaptures  .store (0, std::memory_order_relaxed);

    reset();
}

void ImpulseResponse::reset()
{
    state                = State::Idle;
    captureIdx           = 0;
    sinceLastCompletion  = 0;

    completedCaptures  .store (0, std::memory_order_relaxed);
    displayLengthAtomic.store (0, std::memory_order_relaxed);

    std::fill (capture .begin(), capture .end(), 0.0f);
    std::fill (averaged.begin(), averaged.end(), 0.0f);
}

void ImpulseResponse::setWindowMs (int ms)
{
    const int clamped = std::clamp (ms, kMinWindowMs, kMaxWindowMs);
    const int samples = (int) std::ceil (clamped * 0.001 * sampleRate);
    windowSamples.store (std::min (samples, maxWindowSamples), std::memory_order_relaxed);

    // Changing the window invalidates the in-progress average - lengths
    // wouldn't match. Reset the running average without touching the
    // already-allocated buffers.
    state                = State::Idle;
    captureIdx           = 0;
    sinceLastCompletion  = 0;
    completedCaptures  .store (0, std::memory_order_relaxed);
    displayLengthAtomic.store (0, std::memory_order_relaxed);
    std::fill (averaged.begin(), averaged.end(), 0.0f);
}

void ImpulseResponse::setAverageGoal (int count)
{
    averageGoal.store (std::clamp (count, kMinAverages, kMaxAverages),
                       std::memory_order_relaxed);
}

void ImpulseResponse::processSample (float preSample, float postSample)
{
    const int winSamps = windowSamples.load (std::memory_order_relaxed);
    if (winSamps <= 0) return;

    if (sinceLastCompletion < winSamps)
        ++sinceLastCompletion;

    if (state == State::Idle)
    {
        // Stop accepting triggers once we've reached the user's averaging
        // goal. The display keeps showing the held average until reset()
        // is called.
        if (completedCaptures.load (std::memory_order_relaxed)
            >= averageGoal.load (std::memory_order_relaxed))
            return;

        // Edge trigger: pre crosses above threshold while idle. We also
        // require sinceLastCompletion to have caught up to at least the
        // window length, which prevents back-to-back triggers on a tone
        // that happens to be above threshold continuously.
        if (std::abs (preSample) > kTriggerThresholdLinear
            && sinceLastCompletion >= winSamps)
        {
            state      = State::Capturing;
            capture[0] = postSample;
            captureIdx = 1;
        }
        return;
    }

    // State::Capturing
    capture[(size_t) captureIdx] = postSample;
    ++captureIdx;

    if (captureIdx >= winSamps)
    {
        // Capture complete. Fold into running average.
        const int already = completedCaptures.load (std::memory_order_relaxed);
        const int next    = already + 1;

        // Incremental mean: avg' = avg + (capture - avg) / next.
        // Numerically stable and bounded for any number of captures.
        const float invNext = 1.0f / (float) next;
        for (int i = 0; i < winSamps; ++i)
            averaged[(size_t) i] += (capture[(size_t) i] - averaged[(size_t) i]) * invNext;

        completedCaptures  .store (next, std::memory_order_release);
        displayLengthAtomic.store (winSamps, std::memory_order_release);

        state               = State::Idle;
        captureIdx          = 0;
        sinceLastCompletion = 0;
        // The Idle-branch checks averageGoal vs completedCaptures, so
        // hitting the goal naturally stops further triggers from
        // arming. The user calls reset() (Clear button) to start over.
    }
}
