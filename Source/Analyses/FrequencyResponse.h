/*
  ==============================================================================

    FrequencyResponse.h
    Bin-wise magnitude transfer function: post_dB - pre_dB per FFT bin, which
    in the linear domain is post/pre (the plugin's gain at each frequency).

    Operates on the pre-effect and post-effect dB spectrum arrays already
    computed by the processor's spectrum FFT. No new DSP path - this analysis
    is a derived output from data the universal spectrum overlay already
    produces.

    Bins where the pre-effect magnitude falls below kMinValidPreDb are marked
    with kNoMeasurementDb so the display can skip drawing through them rather
    than show meaningless "infinite gain" values.

  ==============================================================================
*/

#pragma once

#include <vector>

class FrequencyResponse
{
public:
    // Per-bin validity uses hysteresis: a bin becomes valid when pre rises
    // above kValidEnterDb and stays valid until pre falls below
    // kValidLeaveDb. The 20 dB gap prevents the bin-by-bin flicker that
    // happens when pre sits right at the threshold and noise pushes it
    // back and forth.
    static constexpr float kValidEnterDb   = -80.0f;
    static constexpr float kValidLeaveDb   = -100.0f;

    // Backwards compatibility alias.
    static constexpr float kMinValidPreDb  = kValidEnterDb;

    // Sentinel value written to bins where no valid measurement exists.
    static constexpr float kNoMeasurementDb = -200.0f;

    // Temporal smoothing factor for the per-bin EMA (responseDb_t =
    // alpha * raw + (1 - alpha) * prev). At the spectrum hop rate
    // (~47 Hz at 48 kHz) this gives a ~130 ms time constant - enough
    // to suppress FFT bin-to-bin variance without making the trace
    // sluggish on parameter changes.
    static constexpr float kSmoothingAlpha = 0.15f;

    // Called once after sample rate / spectrum size is known. Sizes the
    // internal buffers; allocates. Not real-time-safe.
    void prepare (int numBins);

    // Resets all response buffers to "no measurement" everywhere. Cheap
    // and real-time-safe. Called on mode switches so stale data isn't
    // displayed.
    void reset();

    // Computes post_dB - pre_dB per bin, per channel, and the L vs R
    // difference. Real-time-safe. Input arrays must each be at least
    // getNumBins() floats long. The diff trace is R minus L in dB (so a
    // perfectly symmetric plugin reads 0 dB across the spectrum).
    void update (const float* preDbL,  const float* postDbL,
                 const float* preDbR,  const float* postDbR);

    const std::vector<float>& getResponseDb()     const noexcept { return responseDb;      }
    const std::vector<float>& getResponseDb_R()   const noexcept { return responseDb_R;    }
    const std::vector<float>& getResponseDb_Diff() const noexcept { return responseDb_Diff; }
    int  getNumBins()                              const noexcept { return (int) responseDb.size(); }

private:
    // All three arrays hold temporally-smoothed values (EMA per bin).
    std::vector<float> responseDb;       // L channel  (post_L - pre_L)
    std::vector<float> responseDb_R;     // R channel  (post_R - pre_R)
    std::vector<float> responseDb_Diff;  // R - L      (channel asymmetry, dB)

    // Per-bin hysteresis state - tracks whether each channel's pre level
    // is currently above the valid threshold band.
    std::vector<char> binValidL;
    std::vector<char> binValidR;
};
