/*
  ==============================================================================

    CSD.cpp

  ==============================================================================
*/

#include "CSD.h"

#include <algorithm>
#include <cmath>

void CSD::clear()
{
    gridL.clear();
    gridR.clear();
    ready  = false;
    spanMs = 0.0f;
}

void CSD::compute (const float* irL, int nL,
                   const float* irR, int nR,
                   double sampleRateHz)
{
    sampleRate = (float) sampleRateHz;

    const int nMax = juce::jmax (nL, nR);
    if (nMax <= 0 || irL == nullptr || irR == nullptr)
    {
        clear();
        return;
    }

    gridL.assign ((size_t) (kNumSlices * kNumBins), kFloorDb);
    gridR.assign ((size_t) (kNumSlices * kNumBins), kFloorDb);

    computeChannel (gridL, irL, nL, nMax);
    computeChannel (gridR, irR, nR, nMax);

    // Time span: the last slice sits at fraction 1.0 of the in-bounds
    // start range of the longest channel.
    const int maxStart = juce::jmax (0, nMax - kSliceFftSize);
    spanMs = (float) maxStart / sampleRate * 1000.0f;

    ready = true;
}

void CSD::computeChannel (std::vector<float>& grid, const float* ir, int n, int nMax)
{
    if (n <= 0) return;   // grid already floored

    juce::dsp::FFT fft (kSliceFftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) kSliceFftSize,
                                                juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> scratch ((size_t) (2 * kSliceFftSize), 0.0f);

    // Slide both channels over the SHARED (longest-channel) start range, so L
    // and R genuinely share one time axis and spanMs (derived from nMax) labels
    // both correctly. Using the per-channel range instead stretched a shorter
    // channel's decay across the longer channel's axis. A channel shorter than
    // nMax simply reads zeros past its end (guarded below).
    const int maxStartThis = juce::jmax (0, nMax - kSliceFftSize);

    float peakDb = -1.0e9f;

    for (int s = 0; s < kNumSlices; ++s)
    {
        const float frac  = kNumSlices > 1 ? (float) s / (float) (kNumSlices - 1) : 0.0f;
        const int   start = juce::roundToInt (frac * (float) maxStartThis);

        std::fill (scratch.begin(), scratch.end(), 0.0f);
        for (int i = 0; i < kSliceFftSize; ++i)
        {
            const int idx = start + i;
            scratch[(size_t) i] = (idx < n) ? ir[idx] : 0.0f;
        }

        window.multiplyWithWindowingTable (scratch.data(), (size_t) kSliceFftSize);
        fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

        float* row = grid.data() + (size_t) s * (size_t) kNumBins;
        for (int bin = 0; bin < kNumBins; ++bin)
        {
            const float db = juce::Decibels::gainToDecibels (scratch[(size_t) bin], -200.0f);
            row[bin] = db;
            peakDb   = juce::jmax (peakDb, db);
        }
    }

    // Normalise to the channel peak (0 dB) and floor the decay.
    if (peakDb > -1.0e8f)
    {
        for (auto& v : grid)
            v = juce::jlimit (kFloorDb, 0.0f, v - peakDb);
    }
}

float CSD::getValue (int channel, int slice, int bin) const noexcept
{
    if (! ready) return kFloorDb;
    if (slice < 0 || slice >= kNumSlices || bin < 0 || bin >= kNumBins) return kFloorDb;

    const auto& grid = (channel == 0) ? gridL : gridR;
    if (grid.empty()) return kFloorDb;
    return grid[(size_t) slice * (size_t) kNumBins + (size_t) bin];
}

float CSD::getSliceTimeMs (int slice) const noexcept
{
    if (kNumSlices <= 1) return 0.0f;
    const float frac = (float) juce::jlimit (0, kNumSlices - 1, slice)
                     / (float) (kNumSlices - 1);
    return frac * spanMs;
}

float CSD::getBinHz (int bin) const noexcept
{
    return (float) bin * (sampleRate / (float) kSliceFftSize);
}
