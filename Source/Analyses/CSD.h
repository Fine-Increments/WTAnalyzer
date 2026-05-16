/*
  ==============================================================================

    CSD.h
    Cumulative Spectral Decay - a time x frequency grid showing how a
    captured impulse response decays.

    Both IR modes (Direct Impulse IR, Farina IR) capture the device's
    impulse response. The IR is already the device's differential
    behaviour, so the CSD is a visualisation of that captured data, not a
    new measurement: a short Hann window is slid along the IR, FFT'd at
    each position, and the magnitude-dB slices are stacked into a grid.

    A resonance shows up as a ridge that lingers in the time direction
    while everything around it has decayed. The grid is normalised so the
    channel's loudest cell reads 0 dB and the decay runs downward.

    Computed once per capture on the message thread (allocations fine);
    the display reads the finished grid. Drives the CSD Heatmap and
    CSD 3D sub-views shared by both IR modes.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

class CSD
{
public:
    // Per-slice analysis FFT. 1024 points = ~21 ms window at 48 kHz -
    // fine enough time resolution for decay structure, 512 usable bins.
    static constexpr int kSliceFftOrder = 10;
    static constexpr int kSliceFftSize  = 1 << kSliceFftOrder;   // 1024
    static constexpr int kNumBins       = kSliceFftSize / 2;     // 512

    // Time slices spanning the captured IR. 96 keeps the grid small and
    // the waterfall legible while resolving early decay well.
    static constexpr int kNumSlices = 96;

    // Display / empty-cell floor, in dB below the channel peak.
    static constexpr float kFloorDb = -90.0f;

    void compute (const float* irL, int nL,
                  const float* irR, int nR,
                  double sampleRateHz);
    void clear();

    bool  isReady()       const noexcept { return ready; }
    int   getNumSlices()  const noexcept { return kNumSlices; }
    int   getNumBins()    const noexcept { return kNumBins; }
    float getSampleRate() const noexcept { return sampleRate; }

    // dB at (slice, bin) for channel 0 (L) or 1 (R). 0 dB = channel peak,
    // decay runs negative; kFloorDb when not ready.
    float getValue (int channel, int slice, int bin) const noexcept;

    float getSliceTimeMs (int slice) const noexcept;
    float getBinHz       (int bin)   const noexcept;
    float getMaxHz()     const noexcept { return sampleRate * 0.5f; }
    float getSpanMs()    const noexcept { return spanMs; }

private:
    void computeChannel (std::vector<float>& grid, const float* ir, int n, int nMax);

    std::vector<float> gridL;   // kNumSlices * kNumBins, row-major by slice
    std::vector<float> gridR;

    float sampleRate = 48000.0f;
    float spanMs     = 0.0f;    // time span of the slice set
    bool  ready      = false;
};
