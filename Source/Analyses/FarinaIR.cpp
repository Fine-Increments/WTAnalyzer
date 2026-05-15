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
    postCapture .clear();
    inverseSweep.clear();
    ir          .clear();
    postScratch .clear();
    invScratch  .clear();

    reset();
}

void FarinaIR::ensureCapacity (int captureSamples, int filterSamples)
{
    const int needed       = captureSamples + filterSamples;
    const int neededOrder  = nextPowerOf2Order (needed);

    const bool fftOk      = fft != nullptr && fftOrder >= neededOrder;
    const bool postOk     = (int) postCapture .size() >= captureSamples;
    const bool filterOk   = (int) inverseSweep.size() >= filterSamples;

    if (fftOk && postOk && filterOk)
        return;

    fftOrder = neededOrder;
    fftSize  = 1 << fftOrder;
    fft      = std::make_unique<juce::dsp::FFT> (fftOrder);

    postCapture .assign ((size_t) captureSamples, 0.0f);
    inverseSweep.assign ((size_t) filterSamples, 0.0f);
    ir          .assign ((size_t) filterSamples, 0.0f);    // upper-bound on tail
    postScratch .assign ((size_t) (2 * fftSize), 0.0f);
    invScratch  .assign ((size_t) (2 * fftSize), 0.0f);
}

void FarinaIR::reset()
{
    state.store (State::Idle, std::memory_order_release);
    captureProgress.store (0, std::memory_order_relaxed);
    captureLength  .store (0, std::memory_order_relaxed);
    irLength       .store (0, std::memory_order_relaxed);

    std::fill (postCapture.begin(), postCapture.end(), 0.0f);
    std::fill (ir         .begin(), ir         .end(), 0.0f);
}

void FarinaIR::setSweepParams (float f0, float f1, float durationSec, float tail)
{
    f0Hz             = juce::jlimit (kMinF0Hz, kMaxF0Hz, f0);
    f1Hz             = juce::jlimit (kMinF1Hz, kMaxF1Hz, f1);
    sweepDurationSec = juce::jlimit (kMinSweepSec, kMaxSweepSec, durationSec);
    tailSec          = juce::jlimit (kMinTailSec, kMaxTailSec, tail);

    // f1 must exceed f0 by a meaningful margin for the log sweep to
    // make sense. Nudge if the user typed nonsense.
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

    captureLength  .store (total, std::memory_order_relaxed);
    captureProgress.store (0, std::memory_order_relaxed);
    irLength       .store (0, std::memory_order_relaxed);
    state          .store (State::Armed, std::memory_order_release);
}

void FarinaIR::processSample (float preSample, float postSample)
{
    const State s = state.load (std::memory_order_acquire);

    if (s == State::Armed)
    {
        if (std::abs (preSample) > kTriggerThreshold)
        {
            postCapture[0] = postSample;
            captureProgress.store (1, std::memory_order_relaxed);
            state.store (State::Capturing, std::memory_order_release);
        }
        return;
    }

    if (s == State::Capturing)
    {
        const int prog = captureProgress.load (std::memory_order_relaxed);
        const int len  = captureLength  .load (std::memory_order_relaxed);
        postCapture[(size_t) prog] = postSample;
        const int next = prog + 1;
        captureProgress.store (next, std::memory_order_relaxed);

        if (next >= len)
            state.store (State::ReadyToProcess, std::memory_order_release);
    }
}

void FarinaIR::tryProcessCapture()
{
    if (state.load (std::memory_order_acquire) != State::ReadyToProcess)
        return;

    runDeconvolution();
    state.store (State::IRReady, std::memory_order_release);
}

void FarinaIR::generateInverseSweep (std::vector<float>& out,
                                     float f0, float f1, float durationSec)
{
    const int N = (int) std::ceil (durationSec * sampleRate);
    if (N <= 0 || N > (int) inverseSweep.size()) return;

    const float L = std::log (f1 / f0);
    const float K = juce::MathConstants<float>::twoPi * f0 * durationSec / L;

    // Inverse filter: f(t) = sin(K * (e^((T-t)*L/T) - 1)) * e^(-t*L/T)
    // i.e. time-reversed forward sweep multiplied by exponential
    // amplitude shaping that compensates for the log sweep's
    // longer dwell at low frequencies.
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

void FarinaIR::runDeconvolution()
{
    const int captureSamples = captureLength.load (std::memory_order_relaxed);
    const int sweepSamples   = (int) std::ceil (sweepDurationSec * sampleRate);
    const int tailSamples    = (int) std::ceil (tailSec          * sampleRate);

    if (captureSamples <= 0 || sweepSamples <= 0 || tailSamples <= 0)
        return;

    generateInverseSweep (inverseSweep, f0Hz, f1Hz, sweepDurationSec);

    // FFT of post (zero-padded to fftSize). performRealOnlyForwardTransform
    // with ignoreNegativeFreqs=false writes the full complex spectrum in
    // interleaved (Re, Im) pairs.
    std::fill (postScratch.begin(), postScratch.end(), 0.0f);
    for (int i = 0; i < captureSamples && i < fftSize; ++i)
        postScratch[(size_t) i] = postCapture[(size_t) i];
    fft->performRealOnlyForwardTransform (postScratch.data(), false);

    // FFT of inverse sweep (zero-padded).
    std::fill (invScratch.begin(), invScratch.end(), 0.0f);
    for (int i = 0; i < sweepSamples && i < fftSize; ++i)
        invScratch[(size_t) i] = inverseSweep[(size_t) i];
    fft->performRealOnlyForwardTransform (invScratch.data(), false);

    // Element-wise complex multiply: result = post * inverse (in frequency).
    // Layout: scratches hold 2 * fftSize floats, organised as fftSize complex
    // pairs (re, im).
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

    // The linear IR is located starting at sample sweepSamples - 1 in the
    // convolution result. Extract tailSamples worth.
    const int irStart = juce::jmax (0, sweepSamples - 1);

    ir.assign ((size_t) tailSamples, 0.0f);
    for (int i = 0; i < tailSamples; ++i)
    {
        const int srcIdx = irStart + i;
        if (srcIdx < fftSize)
            ir[(size_t) i] = postScratch[(size_t) srcIdx];
    }

    irLength.store (tailSamples, std::memory_order_release);
}
