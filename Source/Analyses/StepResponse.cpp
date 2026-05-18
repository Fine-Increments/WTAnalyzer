/*
  ==============================================================================

    StepResponse.cpp
    See StepResponse.h.

  ==============================================================================
*/

#include "StepResponse.h"

#include <algorithm>
#include <cmath>

void StepResponse::prepare (double sr, int /*samplesPerBlock*/)
{
    sampleRate     = (float) sr;
    preRollSamples = juce::jmax (1, (int) std::ceil ((double) kPreRollMs * sr / 1000.0));

    chL.preRoll  .clear();
    chL.response .clear();
    chR.preRoll  .clear();
    chR.response .clear();

    reset();
}

void StepResponse::ensureCapacity (int captureSamples, int preRollLen)
{
    const bool capLOk = (int) chL.response.size() >= captureSamples
                     && (int) chL.preRoll .size() == preRollLen;
    const bool capROk = (int) chR.response.size() >= captureSamples
                     && (int) chR.preRoll .size() == preRollLen;

    if (capLOk && capROk)
        return;

    chL.preRoll .assign ((size_t) preRollLen, 0.0f);
    chL.response.assign ((size_t) captureSamples, 0.0f);
    chR.preRoll .assign ((size_t) preRollLen, 0.0f);
    chR.response.assign ((size_t) captureSamples, 0.0f);
}

void StepResponse::resetChannel (ChannelState& ch)
{
    ch.state          .store (State::Idle, std::memory_order_release);
    ch.captureProgress.store (0, std::memory_order_relaxed);
    ch.captureLength  .store (0, std::memory_order_relaxed);
    ch.responseLength .store (0, std::memory_order_relaxed);
    ch.preRollWrite = 0;
    ch.baseline = ch.settledLevel = ch.riseTimeMs = ch.overshootPct = 0.0f;
    std::fill (ch.preRoll .begin(), ch.preRoll .end(), 0.0f);
    std::fill (ch.response.begin(), ch.response.end(), 0.0f);
}

void StepResponse::reset()
{
    resetChannel (chL);
    resetChannel (chR);
    responseGeneration.fetch_add (1, std::memory_order_relaxed);
}

void StepResponse::setParams (float newWindowMs)
{
    windowMs = juce::jlimit (kMinWindowMs, kMaxWindowMs, newWindowMs);
}

void StepResponse::requestCapture()
{
    if ((int) sampleRate <= 0)
        return;

    const int windowSamples = juce::jmax (1, (int) std::ceil (
                                  (double) windowMs * sampleRate / 1000.0));
    const int total = preRollSamples + windowSamples;

    ensureCapacity (total, preRollSamples);

    auto armChannel = [&] (ChannelState& ch)
    {
        ch.captureLength  .store (total, std::memory_order_relaxed);
        ch.captureProgress.store (0,     std::memory_order_relaxed);
        ch.responseLength .store (0,     std::memory_order_relaxed);
        ch.preRollWrite = 0;
        std::fill (ch.preRoll.begin(), ch.preRoll.end(), 0.0f);
        ch.state.store (State::Armed, std::memory_order_release);
    };
    armChannel (chL);
    armChannel (chR);
}

void StepResponse::processSample (float preL, float postL, float preR, float postR)
{
    processChannel (chL, preL, postL);
    processChannel (chR, preR, postR);
}

void StepResponse::processChannel (ChannelState& ch, float preSample, float postSample)
{
    const State s = ch.state.load (std::memory_order_acquire);

    if (s == State::Armed)
    {
        // Ring-buffer the post signal while waiting, so the pre-step baseline
        // and the foot of a slow-rising step are never lost.
        const int P = (int) ch.preRoll.size();
        if (P > 0)
        {
            ch.preRoll[(size_t) ch.preRollWrite] = postSample;
            ch.preRollWrite = (ch.preRollWrite + 1) % P;
        }

        // Rising edge: the step climbs from ~0 toward its level.
        if (preSample > kTriggerThreshold)
        {
            // Splice the pre-roll in front of the capture, oldest sample
            // first - preRollWrite now points at the oldest entry.
            for (int i = 0; i < P; ++i)
                ch.response[(size_t) i] =
                    ch.preRoll[(size_t) ((ch.preRollWrite + i) % P)];

            ch.captureProgress.store (P, std::memory_order_relaxed);
            ch.state.store (State::Capturing, std::memory_order_release);
        }
        return;
    }

    if (s == State::Capturing)
    {
        const int prog = ch.captureProgress.load (std::memory_order_relaxed);
        const int len  = ch.captureLength  .load (std::memory_order_relaxed);
        ch.response[(size_t) prog] = postSample;
        const int next = prog + 1;
        ch.captureProgress.store (next, std::memory_order_relaxed);

        if (next >= len)
            ch.state.store (State::ReadyToProcess, std::memory_order_release);
    }
}

void StepResponse::tryProcessCapture()
{
    auto maybeProcess = [this] (ChannelState& ch)
    {
        if (ch.state.load (std::memory_order_acquire) != State::ReadyToProcess)
            return;
        runMetrics (ch);
        ch.state.store (State::Ready, std::memory_order_release);
    };
    maybeProcess (chL);
    maybeProcess (chR);
}

StepResponse::State StepResponse::getState() const noexcept
{
    auto rank = [] (State s) -> int
    {
        switch (s)
        {
            case State::Idle:           return 0;
            case State::Ready:          return 1;
            case State::ReadyToProcess: return 2;
            case State::Armed:          return 3;
            case State::Capturing:      return 4;
        }
        return 0;
    };
    const State sL = chL.state.load (std::memory_order_acquire);
    const State sR = chR.state.load (std::memory_order_acquire);
    return rank (sL) >= rank (sR) ? sL : sR;
}

void StepResponse::runMetrics (ChannelState& ch)
{
    const int total = ch.captureLength.load (std::memory_order_relaxed);
    const int P     = preRollSamples;

    if (total <= P)
        return;

    // Baseline: the pre-step level, averaged over the pre-roll region.
    double baseSum = 0.0;
    for (int i = 0; i < P; ++i)
        baseSum += (double) ch.response[(size_t) i];
    const double baseline = (P > 0) ? baseSum / (double) P : 0.0;

    // Settled level: averaged over the last tenth of the post-step window.
    const int windowLen = total - P;
    const int tailLen   = juce::jmax (1, windowLen / 10);
    double settledSum = 0.0;
    for (int i = total - tailLen; i < total; ++i)
        settledSum += (double) ch.response[(size_t) i];
    const double settled   = settledSum / (double) tailLen;
    const double amplitude = settled - baseline;

    double riseMs   = 0.0;
    double overshot = 0.0;

    if (std::abs (amplitude) > 1.0e-6)
    {
        const double dir = (amplitude >= 0.0) ? 1.0 : -1.0;
        const double lo  = baseline + 0.1 * amplitude;
        const double hi  = baseline + 0.9 * amplitude;

        // 10% and 90% crossings, searched forward from the step edge.
        int t10 = -1, t90 = -1;
        for (int i = P; i < total; ++i)
        {
            const double v = (double) ch.response[(size_t) i];
            if (t10 < 0 && dir * (v - lo) >= 0.0) t10 = i;
            if (t10 >= 0 && dir * (v - hi) >= 0.0) { t90 = i; break; }
        }
        if (t10 >= 0 && t90 >= t10)
            riseMs = (double) (t90 - t10) / (double) sampleRate * 1000.0;

        // Overshoot: the post-step extreme in the step's direction, measured
        // past the settled level as a percentage of the step amplitude.
        double extreme = (double) ch.response[(size_t) P];
        for (int i = P; i < total; ++i)
        {
            const double v = (double) ch.response[(size_t) i];
            if (dir * (v - extreme) > 0.0)
                extreme = v;
        }
        overshot = juce::jmax (0.0, (extreme - settled) / amplitude * 100.0);
    }

    ch.baseline     = (float) baseline;
    ch.settledLevel = (float) settled;
    ch.riseTimeMs   = (float) riseMs;
    ch.overshootPct = (float) overshot;

    ch.responseLength.store (total, std::memory_order_release);
    responseGeneration.fetch_add (1, std::memory_order_relaxed);
}
