/*
  ==============================================================================

    FarinaIR.cpp

  ==============================================================================
*/

#include "FarinaIR.h"

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

void FarinaIR::prepare (double sr, int /*samplesPerBlock*/)
{
    sampleRate = (float) sr;

    // Free anything from a previous sample-rate's run; defer all
    // re-allocation to requestCapture() (see header comment for why).
    fft.reset();
    fftOrder = 0;
    fftSize  = 0;
    inverseSweep.clear();
    postScratch .clear();
    invScratch  .clear();
    chL.postCapture.clear();
    chL.ir         .clear();
    chR.postCapture.clear();
    chR.ir         .clear();

    reset();
}

void FarinaIR::ensureCapacity (int captureSamples, int filterSamples)
{
    const int needed       = captureSamples + filterSamples;
    const int neededOrder  = nextPowerOf2Order (needed);

    const bool fftOk      = fft != nullptr && fftOrder >= neededOrder;
    const bool postLOk    = (int) chL.postCapture.size() >= captureSamples;
    const bool postROk    = (int) chR.postCapture.size() >= captureSamples;
    const bool filterOk   = (int) inverseSweep.size() >= filterSamples;

    if (fftOk && postLOk && postROk && filterOk)
        return;

    fftOrder = neededOrder;
    fftSize  = 1 << fftOrder;
    fft      = std::make_unique<juce::dsp::FFT> (fftOrder);

    chL.postCapture.assign ((size_t) captureSamples, 0.0f);
    chL.ir         .assign ((size_t) filterSamples, 0.0f);
    chR.postCapture.assign ((size_t) captureSamples, 0.0f);
    chR.ir         .assign ((size_t) filterSamples, 0.0f);

    inverseSweep.assign ((size_t) filterSamples, 0.0f);
    postScratch .assign ((size_t) (2 * fftSize), 0.0f);
    invScratch  .assign ((size_t) (2 * fftSize), 0.0f);
}

void FarinaIR::resetChannel (ChannelState& ch)
{
    ch.state          .store (State::Idle, std::memory_order_release);
    ch.captureProgress.store (0, std::memory_order_relaxed);
    ch.captureLength  .store (0, std::memory_order_relaxed);
    ch.irLength       .store (0, std::memory_order_relaxed);
    std::fill (ch.postCapture.begin(), ch.postCapture.end(), 0.0f);
    std::fill (ch.ir         .begin(), ch.ir         .end(), 0.0f);
}

void FarinaIR::reset()
{
    resetChannel (chL);
    resetChannel (chR);
}

void FarinaIR::setSweepParams (float f0, float f1, float durationSec, float tail)
{
    f0Hz             = juce::jlimit (kMinF0Hz, kMaxF0Hz, f0);
    f1Hz             = juce::jlimit (kMinF1Hz, kMaxF1Hz, f1);
    sweepDurationSec = juce::jlimit (kMinSweepSec, kMaxSweepSec, durationSec);
    tailSec          = juce::jlimit (kMinTailSec, kMaxTailSec, tail);

    if (f1Hz <= f0Hz * 1.1f)
        f1Hz = f0Hz * 1.1f;
}

void FarinaIR::requestCapture()
{
    if ((int) sampleRate <= 0) return;

    const int sweepSamples = (int) std::ceil (sweepDurationSec * sampleRate);
    const int tailSamples  = (int) std::ceil (tailSec          * sampleRate);
    const int total        = sweepSamples + tailSamples;

    // Lazy allocation: first capture (or capture after params grow
    // beyond current capacity) pays the FFT setup + buffer cost here,
    // on the message thread, well outside the audio path.
    ensureCapacity (total, sweepSamples);

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

void FarinaIR::processSample (float preL, float postL, float preR, float postR)
{
    processChannel (chL, preL, postL);
    processChannel (chR, preR, postR);
}

void FarinaIR::processChannel (ChannelState& ch, float preSample, float postSample)
{
    const State s = ch.state.load (std::memory_order_acquire);

    if (s == State::Armed)
    {
        if (std::abs (preSample) > kTriggerThreshold)
        {
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
        ch.postCapture[(size_t) prog] = postSample;
        const int next = prog + 1;
        ch.captureProgress.store (next, std::memory_order_relaxed);

        if (next >= len)
            ch.state.store (State::ReadyToProcess, std::memory_order_release);
    }
}

void FarinaIR::tryProcessCapture()
{
    auto maybeProcess = [this] (ChannelState& ch)
    {
        if (ch.state.load (std::memory_order_acquire) != State::ReadyToProcess)
            return;
        runDeconvolution (ch);
        ch.state.store (State::IRReady, std::memory_order_release);
    };
    maybeProcess (chL);
    maybeProcess (chR);
}

FarinaIR::State FarinaIR::getState() const noexcept
{
    // Return the channel-state that's "earliest" in the lifecycle so the
    // UI sees the slowest-moving channel's progress.
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

void FarinaIR::generateInverseSweep (std::vector<float>& out,
                                     float f0, float f1, float durationSec)
{
    const int N = (int) std::ceil (durationSec * sampleRate);
    if (N <= 0 || N > (int) inverseSweep.size()) return;

    const float L = std::log (f1 / f0);
    const float K = juce::MathConstants<float>::twoPi * f0 * durationSec / L;

    for (int i = 0; i < N; ++i)
    {
        const float t        = (float) i / sampleRate;
        const float tFwd     = durationSec - t;
        const float phaseArg = std::exp (tFwd * L / durationSec) - 1.0f;
        const float sFwd     = std::sin (K * phaseArg);
        const float envelope = std::exp (-t * L / durationSec);
        out[(size_t) i] = sFwd * envelope;
    }
}

void FarinaIR::runDeconvolution (ChannelState& ch)
{
    const int captureSamples = ch.captureLength.load (std::memory_order_relaxed);
    const int sweepSamples   = (int) std::ceil (sweepDurationSec * sampleRate);
    const int tailSamples    = (int) std::ceil (tailSec          * sampleRate);

    if (captureSamples <= 0 || sweepSamples <= 0 || tailSamples <= 0)
        return;

    generateInverseSweep (inverseSweep, f0Hz, f1Hz, sweepDurationSec);

    std::fill (postScratch.begin(), postScratch.end(), 0.0f);
    for (int i = 0; i < captureSamples && i < fftSize; ++i)
        postScratch[(size_t) i] = ch.postCapture[(size_t) i];
    fft->performRealOnlyForwardTransform (postScratch.data(), false);

    std::fill (invScratch.begin(), invScratch.end(), 0.0f);
    for (int i = 0; i < sweepSamples && i < fftSize; ++i)
        invScratch[(size_t) i] = inverseSweep[(size_t) i];
    fft->performRealOnlyForwardTransform (invScratch.data(), false);

    for (int k = 0; k < fftSize; ++k)
    {
        const int idx = 2 * k;
        const float ar = postScratch[(size_t) idx];
        const float ai = postScratch[(size_t) idx + 1];
        const float br = invScratch [(size_t) idx];
        const float bi = invScratch [(size_t) idx + 1];
        postScratch[(size_t) idx]     = ar * br - ai * bi;
        postScratch[(size_t) idx + 1] = ar * bi + ai * br;
    }

    fft->performRealOnlyInverseTransform (postScratch.data());

    const int irStart = juce::jmax (0, sweepSamples - 1);

    ch.ir.assign ((size_t) tailSamples, 0.0f);
    for (int i = 0; i < tailSamples; ++i)
    {
        const int srcIdx = irStart + i;
        if (srcIdx < fftSize)
            ch.ir[(size_t) i] = postScratch[(size_t) srcIdx];
    }

    ch.irLength.store (tailSamples, std::memory_order_release);
}
