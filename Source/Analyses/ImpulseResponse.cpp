/*
  ==============================================================================

    ImpulseResponse.cpp

  ==============================================================================
*/

#include "ImpulseResponse.h"

#include <algorithm>
#include <cmath>
#include <limits>

void ImpulseResponse::prepare (double sr, int /*samplesPerBlock*/)
{
    sampleRate       = (float) sr;
    maxWindowSamples = (int) std::ceil (kMaxWindowMs * 0.001 * sr);

    chL.capture .assign ((size_t) maxWindowSamples, 0.0f);
    chL.averaged.assign ((size_t) maxWindowSamples, 0.0f);
    chR.capture .assign ((size_t) maxWindowSamples, 0.0f);
    chR.averaged.assign ((size_t) maxWindowSamples, 0.0f);

    const int defaultSamples = (int) std::ceil (kDefaultWindowMs * 0.001 * sr);
    windowSamples.store (std::min (defaultSamples, maxWindowSamples), std::memory_order_relaxed);

    reset();
}

void ImpulseResponse::resetChannel (ChannelState& ch)
{
    ch.state               = State::Idle;
    ch.captureIdx          = 0;
    // Large initial value so the very first trigger after reset can fire
    // immediately; the holdoff only meaningfully applies AFTER a completed
    // capture (kept smaller than INT_MAX so the increment can't overflow).
    ch.sinceLastCompletion = std::numeric_limits<int>::max() / 2;

    ch.completedCaptures  .store (0, std::memory_order_relaxed);
    ch.displayLengthAtomic.store (0, std::memory_order_relaxed);

    std::fill (ch.capture .begin(), ch.capture .end(), 0.0f);
    std::fill (ch.averaged.begin(), ch.averaged.end(), 0.0f);
}

void ImpulseResponse::invalidateChannel (ChannelState& ch)
{
    ch.state               = State::Idle;
    ch.captureIdx          = 0;
    ch.sinceLastCompletion = std::numeric_limits<int>::max() / 2;
    ch.completedCaptures  .store (0, std::memory_order_relaxed);
    ch.displayLengthAtomic.store (0, std::memory_order_relaxed);
    std::fill (ch.averaged.begin(), ch.averaged.end(), 0.0f);
}

void ImpulseResponse::reset()
{
    resetChannel (chL);
    resetChannel (chR);
}

void ImpulseResponse::setWindowMs (int ms)
{
    const int clamped = std::clamp (ms, kMinWindowMs, kMaxWindowMs);
    const int samples = (int) std::ceil (clamped * 0.001 * sampleRate);
    const int newWin  = std::min (samples, maxWindowSamples);

    // CRITICAL: skip the reset path entirely when nothing changed. This
    // function gets polled every processBlock so an unconditional reset
    // would re-zero both per-channel averaged buffers (~23 MB each at
    // 48 kHz / 120 s) hundreds of times per second and burn the audio
    // thread.
    if (newWin == windowSamples.load (std::memory_order_relaxed))
        return;

    windowSamples.store (newWin, std::memory_order_relaxed);

    // Window actually changed - invalidate the in-progress average for both
    // channels since its length no longer matches. Buffers stay allocated;
    // only the contents are wiped.
    invalidateChannel (chL);
    invalidateChannel (chR);
}

void ImpulseResponse::setAverageGoal (int count)
{
    averageGoal.store (std::clamp (count, kMinAverages, kMaxAverages),
                       std::memory_order_relaxed);
}

void ImpulseResponse::processSample (float preL, float postL, float preR, float postR)
{
    const int winSamps = windowSamples.load (std::memory_order_relaxed);
    if (winSamps <= 0) return;

    processChannel (chL, preL, postL, winSamps);
    processChannel (chR, preR, postR, winSamps);
}

void ImpulseResponse::processChannel (ChannelState& ch,
                                      float preSample, float postSample,
                                      int winSamps)
{
    if (ch.sinceLastCompletion < winSamps)
        ++ch.sinceLastCompletion;

    if (ch.state == State::Idle)
    {
        if (ch.completedCaptures.load (std::memory_order_relaxed)
            >= averageGoal.load (std::memory_order_relaxed))
            return;

        if (std::abs (preSample) > kTriggerThresholdLinear
            && ch.sinceLastCompletion >= winSamps)
        {
            ch.state         = State::Capturing;
            ch.capture[0]    = postSample;
            ch.captureIdx    = 1;
        }
        return;
    }

    // State::Capturing
    ch.capture[(size_t) ch.captureIdx] = postSample;
    ++ch.captureIdx;

    if (ch.captureIdx >= winSamps)
    {
        // Capture complete. Fold into running average using the stable
        // incremental mean: avg' = avg + (capture - avg) / next.
        const int already = ch.completedCaptures.load (std::memory_order_relaxed);
        const int next    = already + 1;
        const float invNext = 1.0f / (float) next;
        for (int i = 0; i < winSamps; ++i)
            ch.averaged[(size_t) i] += (ch.capture[(size_t) i] - ch.averaged[(size_t) i]) * invNext;

        ch.completedCaptures  .store (next,     std::memory_order_release);
        ch.displayLengthAtomic.store (winSamps, std::memory_order_release);

        ch.state               = State::Idle;
        ch.captureIdx          = 0;
        ch.sinceLastCompletion = 0;
    }
}
