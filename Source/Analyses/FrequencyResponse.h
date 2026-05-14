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
    // Bin magnitude below this (in dBFS) is considered too quiet to derive a
    // meaningful response. Above this, the response is computed normally.
    static constexpr float kMinValidPreDb  = -80.0f;

    // Sentinel value written to bins where no valid measurement exists.
    // SpectrumDisplay treats this as a "break the path" signal.
    static constexpr float kNoMeasurementDb = -200.0f;

    // Called once after sample rate / spectrum size is known. Sizes the
    // internal buffer; allocates. Not real-time-safe.
    void prepare (int numBins);

    // Resets the response buffer to "no measurement" everywhere. Cheap and
    // real-time-safe. Called on mode switches so stale data isn't displayed.
    void reset();

    // Computes post_dB - pre_dB per bin. Real-time-safe. Input arrays must
    // each be at least getNumBins() floats long.
    void update (const float* preDb, const float* postDb);

    const std::vector<float>& getResponseDb() const noexcept { return responseDb; }
    int  getNumBins()                         const noexcept { return (int) responseDb.size(); }

private:
    std::vector<float> responseDb;
};
