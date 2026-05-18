/*
  ==============================================================================

    MlsIR.cpp
    See MlsIR.h.

  ==============================================================================
*/

#include "MlsIR.h"

#include <algorithm>
#include <cmath>

namespace
{
    int nextPowerOf2Order (int n)
    {
        int order = 0;
        int size  = 1;
        while (size < n) { size <<= 1; ++order; }
        return order;
    }
}

void MlsIR::prepare (double sr, int /*samplesPerBlock*/)
{
    sampleRate = (float) sr;

    // Free anything from a previous run; re-allocation is deferred to
    // requestCapture(), off the audio path.
    fft.reset();
    fftOrder = 0;
    fftSize  = 0;
    scratchA.clear();
    scratchB.clear();
    chL.preCapture .clear();
    chL.postCapture.clear();
    chL.ir         .clear();
    chR.preCapture .clear();
    chR.postCapture.clear();
    chR.ir         .clear();

    reset();
}

void MlsIR::ensureCapacity (int captureSamples)
{
    // The correlation runs on one period (captureSamples / 2) of each of pre
    // and post; a linear correlation of two L-length signals needs an FFT of
    // at least 2L = captureSamples.
    const int neededOrder = nextPowerOf2Order (captureSamples);

    const bool fftOk   = fft != nullptr && fftOrder >= neededOrder;
    const bool capLOk  = (int) chL.postCapture.size() >= captureSamples;
    const bool capROk  = (int) chR.postCapture.size() >= captureSamples;

    if (fftOk && capLOk && capROk)
        return;

    fftOrder = neededOrder;
    fftSize  = 1 << fftOrder;
    fft      = std::make_unique<juce::dsp::FFT> (fftOrder);

    chL.preCapture .assign ((size_t) captureSamples, 0.0f);
    chL.postCapture.assign ((size_t) captureSamples, 0.0f);
    chR.preCapture .assign ((size_t) captureSamples, 0.0f);
    chR.postCapture.assign ((size_t) captureSamples, 0.0f);

    scratchA.assign ((size_t) (2 * fftSize), 0.0f);
    scratchB.assign ((size_t) (2 * fftSize), 0.0f);
}

void MlsIR::resetChannel (ChannelState& ch)
{
    ch.state          .store (State::Idle, std::memory_order_release);
    ch.captureProgress.store (0, std::memory_order_relaxed);
    ch.captureLength  .store (0, std::memory_order_relaxed);
    ch.irLength       .store (0, std::memory_order_relaxed);
    std::fill (ch.preCapture .begin(), ch.preCapture .end(), 0.0f);
    std::fill (ch.postCapture.begin(), ch.postCapture.end(), 0.0f);
    std::fill (ch.ir         .begin(), ch.ir         .end(), 0.0f);
}

void MlsIR::reset()
{
    resetChannel (chL);
    resetChannel (chR);
    irGeneration.fetch_add (1, std::memory_order_relaxed);
}

void MlsIR::setParams (int newOrder, float newTailSec)
{
    order   = juce::jlimit (kMinOrder, kMaxOrder, newOrder);
    tailSec = juce::jlimit (kMinTailSec, kMaxTailSec, newTailSec);
}

void MlsIR::requestCapture()
{
    if ((int) sampleRate <= 0)
        return;

    // Two full MLS periods: the first lets the device settle into steady
    // state under the periodic stimulus, the second is the measured period.
    const int total = 2 * periodFor (order);

    ensureCapacity (total);

    auto armChannel = [&] (ChannelState& ch)
    {
        ch.captureLength  .store (total, std::memory_order_relaxed);
        ch.captureProgress.store (0,     std::memory_order_relaxed);
        ch.irLength       .store (0,     std::memory_order_relaxed);
        ch.state          .store (State::Armed, std::memory_order_release);
    };
    armChannel (chL);
    armChannel (chR);
}

void MlsIR::processSample (float preL, float postL, float preR, float postR)
{
    processChannel (chL, preL, postL);
    processChannel (chR, preR, postR);
}

void MlsIR::processChannel (ChannelState& ch, float preSample, float postSample)
{
    const State s = ch.state.load (std::memory_order_acquire);

    if (s == State::Armed)
    {
        // The MLS plays at full level - the threshold just confirms the
        // stimulus is present before the (steady-state) capture begins.
        if (std::abs (preSample) > kTriggerThreshold)
        {
            ch.preCapture [0] = preSample;
            ch.postCapture[0] = postSample;
            ch.captureProgress.store (1, std::memory_order_relaxed);
            ch.state.store (State::Capturing, std::memory_order_release);
        }
        return;
    }

    if (s == State::Capturing)
    {
        const int prog = ch.captureProgress.load (std::memory_order_relaxed);
        const int len  = ch.captureLength  .load (std::memory_order_relaxed);
        ch.preCapture [(size_t) prog] = preSample;
        ch.postCapture[(size_t) prog] = postSample;
        const int next = prog + 1;
        ch.captureProgress.store (next, std::memory_order_relaxed);

        if (next >= len)
            ch.state.store (State::ReadyToProcess, std::memory_order_release);
    }
}

void MlsIR::tryProcessCapture()
{
    auto maybeProcess = [this] (ChannelState& ch)
    {
        if (ch.state.load (std::memory_order_acquire) != State::ReadyToProcess)
            return;
        runCorrelation (ch);
        ch.state.store (State::IRReady, std::memory_order_release);
    };
    maybeProcess (chL);
    maybeProcess (chR);
}

MlsIR::State MlsIR::getState() const noexcept
{
    auto rank = [] (State s) -> int
    {
        switch (s)
        {
            case State::Idle:           return 0;
            case State::IRReady:        return 1;
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

void MlsIR::runCorrelation (ChannelState& ch)
{
    const int captureSamples = ch.captureLength.load (std::memory_order_relaxed);
    const int period         = captureSamples / 2;        // L = 2^order - 1

    if (period <= 0 || fftSize < 2 * period)
        return;

    const int tailSamples = juce::jlimit (1, period,
                                          (int) std::ceil ((double) tailSec * sampleRate));

    // Forward-transform the SECOND period of post and pre. The first period
    // is discarded - it is the device settling under the periodic MLS.
    std::fill (scratchA.begin(), scratchA.end(), 0.0f);
    std::fill (scratchB.begin(), scratchB.end(), 0.0f);
    for (int i = 0; i < period; ++i)
    {
        scratchA[(size_t) i] = ch.postCapture[(size_t) (period + i)];
        scratchB[(size_t) i] = ch.preCapture [(size_t) (period + i)];
    }
    fft->performRealOnlyForwardTransform (scratchA.data(), false);
    fft->performRealOnlyForwardTransform (scratchB.data(), false);

    // Cross-correlation: post * conj(pre). For an MLS the pre autocorrelation
    // is ~ a delta, so the result is the device IR scaled by ~period.
    for (int k = 0; k < fftSize; ++k)
    {
        const int idx = 2 * k;
        const float ar = scratchA[(size_t) idx];
        const float ai = scratchA[(size_t) idx + 1];
        const float br = scratchB[(size_t) idx];
        const float bi = scratchB[(size_t) idx + 1];
        scratchA[(size_t) idx]     = ar * br + ai * bi;
        scratchA[(size_t) idx + 1] = ai * br - ar * bi;
    }

    fft->performRealOnlyInverseTransform (scratchA.data());

    // The IR sits at lag = device latency, a small non-negative offset.
    // Search a bounded window from zero lag for the largest-magnitude sample.
    const int searchW = juce::jmax (1, (int) std::ceil ((double) kIRSearchSec * sampleRate));
    const int hi      = juce::jmin (fftSize, searchW + 1);

    int   irStart = 0;
    float peakAbs = -1.0f;
    for (int i = 0; i < hi; ++i)
    {
        const float a = std::abs (scratchA[(size_t) i]);
        if (a > peakAbs)
        {
            peakAbs = a;
            irStart = i;
        }
    }

    const double norm = 1.0 / (double) period;
    ch.ir.assign ((size_t) tailSamples, 0.0f);
    for (int i = 0; i < tailSamples; ++i)
    {
        const int srcIdx = irStart + i;
        if (srcIdx < fftSize)
            ch.ir[(size_t) i] = (float) ((double) scratchA[(size_t) srcIdx] * norm);
    }

    ch.irLength.store (tailSamples, std::memory_order_release);
    irGeneration.fetch_add (1, std::memory_order_relaxed);
}
