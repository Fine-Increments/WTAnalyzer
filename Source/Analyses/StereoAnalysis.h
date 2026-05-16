/*
  ==============================================================================

    StereoAnalysis.h
    Per-frequency stereo analysis for the Stereo Image mode. Drives two
    sub-views from the spectrum FFT output:

      Divergence  - device-added |FR_R - FR_L|, see below.
      Correlation - per-frequency phase correlation of the POST signal's
                    L and R channels, from their cross-spectrum. +1 = the
                    two channels are in phase at that frequency, 0 =
                    decorrelated, -1 = anti-phase (mono-fold cancellation).
                    Cross / auto spectra are EMA-averaged over a longer
                    window than divergence so the coherence estimate is
                    stable. Bins where the post signal has no energy carry
                    the kNoMeasurementDb sentinel.

    --- Divergence ---------------------------------------------------------

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

#include <atomic>
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

    // Slower EMA for the correlation cross / auto spectra. Coherence is a
    // statistical average; a longer window (~490 ms) settles the estimate.
    static constexpr float kCorrelationAlpha = 0.08f;

    void prepare (int numBins);
    void reset();

    // Computes the device-added stereo divergence and the post-signal L/R
    // phase correlation. preDb/postDb are the magnitude-dB arrays;
    // postComplexL/R are interleaved [re, im] pairs (2 floats per bin).
    // Real-time-safe.
    void update (const float* preDbL,  const float* preDbR,
                 const float* postDbL, const float* postDbR,
                 const float* postComplexL, const float* postComplexR);

    const std::vector<float>& getDivergence()  const noexcept { return divergence; }
    const std::vector<float>& getCorrelation() const noexcept { return correlation; }

    // Single-number broadband correlation: the per-bin cross / auto
    // spectra summed across the spectrum, then normalised. Energy-
    // weighted (loud frequencies dominate), so it tracks but is not
    // bit-identical to a time-domain phase meter. kNoMeasurementDb when
    // no bin carries signal.
    float getBroadbandCorrelation() const noexcept
    {
        return broadbandCorrelation.load (std::memory_order_relaxed);
    }

    int getNumBins() const noexcept { return (int) divergence.size(); }

private:
    std::vector<float> divergence;   // signed |FR_R - FR_L|, per bin
    std::vector<float> correlation;  // post L/R phase correlation [-1,+1], per bin

    // EMA accumulators for the L/R cross-spectrum and the two auto-spectra.
    // correlation = avg(crossRe) / sqrt(avg(autoLL) * avg(autoRR)).
    std::vector<float> crossReAccum;
    std::vector<float> autoLLAccum;
    std::vector<float> autoRRAccum;
    std::vector<char>  correlationSeeded;

    // Per-channel device response (post - pre), EMA-smoothed. divergence
    // is derived from this smoothed pair each frame so its sign is stable.
    std::vector<float> frLSmoothed;
    std::vector<float> frRSmoothed;

    // Per-bin hysteresis state for the pre and post signals.
    std::vector<char>  preHasSignal;
    std::vector<char>  postHasSignal;

    // Broadband aggregate of the cross / auto spectra. Written by the
    // audio thread in update(), read by the display.
    std::atomic<float> broadbandCorrelation { kNoMeasurementDb };
};
