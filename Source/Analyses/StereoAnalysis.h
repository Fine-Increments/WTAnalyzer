/*
  ==============================================================================

    StereoAnalysis.h
    Per-frequency device-added stereo divergence.

    Divergence at a bin measures how much the device under test made the
    left and right channels differ. For each channel the device's effect
    is FR_x = post_x - pre_x. The reported value is:

      magnitude = |FR_R - FR_L|   - how much the device decorrelated the
                                    two channels at that frequency.
      sign      = + if |FR_R| >= |FR_L|, else -   - the channel the device
                                    acted on MORE. Boost-vs-cut polarity is
                                    deliberately NOT encoded: a cut to R and
                                    a boost to R both read in the R (+)
                                    direction.

    A stereo-transparent device reads 0 regardless of how stereo the input
    already was - the FR-difference cancels the input's own stereo content.

    Per-channel responses are EMA-smoothed first; the signed divergence is
    derived from the smoothed pair so its sign stays stable near a
    symmetric tie. Bins with no signal carry the kNoMeasurementDb sentinel
    so the display rests the trace on its zero centre line there.

    Consumes the existing pre/post spectrum FFT dB arrays - no new DSP
    stream. Drives the Stereo Image mode's Divergence sub-view.

  ==============================================================================
*/

#pragma once

#include <vector>

class StereoAnalysis
{
public:
    // A bin "has signal" once the louder of its two channels rises above
    // kSignalEnterDb, and keeps that status until it falls below
    // kSignalLeaveDb. The 20 dB hysteresis gap stops bin-by-bin flicker
    // when a channel sits right at the threshold.
    static constexpr float kSignalEnterDb  = -80.0f;
    static constexpr float kSignalLeaveDb  = -100.0f;

    // Sentinel for bins with no valid divergence measurement.
    static constexpr float kNoMeasurementDb = -200.0f;

    // Per-bin EMA factor. ~260 ms time constant at the order-13 spectrum
    // hop rate - smooth enough for a steady-state divergence meter.
    static constexpr float kSmoothingAlpha = 0.15f;

    void prepare (int numBins);
    void reset();

    // Computes the device-added stereo divergence from the four spectrum
    // dB arrays. Real-time-safe.
    void update (const float* preDbL,  const float* preDbR,
                 const float* postDbL, const float* postDbR);

    const std::vector<float>& getDivergence() const noexcept { return divergence; }

    int getNumBins() const noexcept { return (int) divergence.size(); }

private:
    std::vector<float> divergence;   // signed |FR_R - FR_L|, per bin

    // Per-channel device response (post - pre), EMA-smoothed. divergence
    // is derived from this smoothed pair each frame so its sign is stable.
    std::vector<float> frLSmoothed;
    std::vector<float> frRSmoothed;

    // Per-bin hysteresis state for the pre and post signals.
    std::vector<char>  preHasSignal;
    std::vector<char>  postHasSignal;
};
