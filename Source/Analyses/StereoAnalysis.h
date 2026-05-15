/*
  ==============================================================================

    StereoAnalysis.h
    Per-frequency stereo divergence analysis.

    Divergence at a bin is the level difference between the right and left
    channels of a signal: R_dB - L_dB. Zero means the channels match
    (mono / fully correlated in level at that frequency); positive means R
    is louder, negative means L is louder.

    Three views are produced from each spectrum frame:

      - Pre:    pre_R  - pre_L   - the input signal's own stereo image.
      - Post:   post_R - post_L  - the output signal's stereo image.
      - Diff:   (post_R - post_L) - (pre_R - pre_L) - the divergence the
                DEVICE introduced. A stereo-transparent device reads 0
                here regardless of how stereo the input already was.

    Diff is the canonical view (it isolates the device). Pre and Post are
    raw sanity checks. All three are EMA-smoothed per bin so the trace is
    stable on complex / noisy signals. Bins with no signal carry the
    kNoMeasurementDb sentinel so the display can rest the trace on its
    zero-divergence centre line there rather than draw noise.

    Consumes the existing pre/post spectrum FFT dB arrays - no new DSP
    stream, same derived-measurement pattern as FrequencyResponse.

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

    enum class View { Diff, Pre, Post };

    void prepare (int numBins);
    void reset();

    // Computes the pre / post / device divergence arrays from the four
    // spectrum dB arrays. Real-time-safe.
    void update (const float* preDbL,  const float* preDbR,
                 const float* postDbL, const float* postDbR);

    const std::vector<float>& getPreDivergence()    const noexcept { return preDiv; }
    const std::vector<float>& getPostDivergence()   const noexcept { return postDiv; }
    const std::vector<float>& getDeviceDivergence() const noexcept { return deviceDiv; }

    // View-selected accessor for the display.
    const std::vector<float>& getDivergence (View v) const noexcept
    {
        switch (v)
        {
            case View::Pre:  return preDiv;
            case View::Post: return postDiv;
            case View::Diff: return deviceDiv;
        }
        return deviceDiv;
    }

    int getNumBins() const noexcept { return (int) preDiv.size(); }

private:
    std::vector<float> preDiv;       // pre_R - pre_L,  EMA-smoothed
    std::vector<float> postDiv;      // post_R - post_L, EMA-smoothed
    std::vector<float> deviceDiv;    // device-added,    EMA-smoothed

    // Per-bin hysteresis state for the pre and post signals.
    std::vector<char>  preHasSignal;
    std::vector<char>  postHasSignal;
};
