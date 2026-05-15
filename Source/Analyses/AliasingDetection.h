/*
  ==============================================================================

    AliasingDetection.h
    Detects aliasing residue ADDED by the effect under test: post-effect
    spectral energy that sits OFF the harmonic grid of the pre-effect
    fundamental AND exceeds whatever was already present in pre.

    Algorithm per frame:
      1. Locate pre's loudest peak (the test signal fundamental).
      2. Build the harmonic grid (f0, 2f0, 3f0, ...).
      3. For each post bin NOT on the grid, compute added energy:
             added_amp = sqrt(max(0, post_amp^2 - pre_amp^2))
         (in dB after the conversion). Pre's own aliasing is therefore
         subtracted out - the green trace is strictly what the device
         introduced, not what was already in the input.
      4. Store the per-frame value (live) and update a peak-hold array.

    Peak hold is maintained only on the differential, never on the raw
    pre / post spectra: the user-relevant question is "what aliasing did
    the device produce" and peak-hold of the channels just clutters the
    display with sweep trails of the input signal. The display calls
    clearPeaks() in response to the user's Clear button between sweeps.

    Consumes the existing spectrum FFT output; no new DSP path.

  ==============================================================================
*/

#pragma once

#include <vector>

class AliasingDetection
{
public:
    // Pre's fundamental bin must read at least this loud for the frame to
    // contribute. Below this we treat the input as silent and skip the
    // frame entirely (peak-held data persists).
    static constexpr float kFundamentalMinDb = -30.0f;

    // Sentinel for "no measurement yet at this bin." Trace renderers treat
    // it as a path break.
    static constexpr float kNoMeasurementDb = -200.0f;

    // Bins within +/- this many indices of an integer harmonic of the
    // fundamental are considered "on the grid" and skipped by the
    // differential. Two bins of slop accommodates DFT leakage from the
    // Hann window and small drift between adjacent sweep frames.
    static constexpr int   kGridToleranceBins = 2;

    void prepare (int numBins, float binFrequencyScale);

    // Clear EVERY accumulated state. Called on mode entry so a stale
    // picture from a previous mode doesn't leak in.
    void reset();

    // Clear only the peak-hold arrays (differential, pre, post). Used by
    // the display's Clear button between test runs without disrupting
    // the in-progress live measurement.
    void clearPeaks();

    // Stereo update: each channel's algorithm runs independently, and a
    // Diff array (R - L per bin) is derived from the live + peak results
    // for the stereo overlay. Mono callers pass identical L and R arrays.
    void update (const float* preDbL, const float* postDbL,
                 const float* preDbR, const float* postDbR);

    // L (master) accessors - unsuffixed names match the pre-stereo API
    // so existing callers continue to work.
    bool  isValid()           const noexcept { return chL.valid; }
    float getFundamentalHz()  const noexcept { return chL.fundamentalHz; }
    int   getFundamentalBin() const noexcept { return chL.fundamentalBin; }
    const std::vector<float>& getLiveDifferentialDb() const noexcept { return chL.liveDifferentialDb; }
    const std::vector<float>& getPeakDifferentialDb() const noexcept { return chL.peakDifferentialDb; }
    float getPeakResidueDb()  const noexcept { return chL.peakResidueDb; }
    float getPeakResidueHz()  const noexcept { return chL.peakResidueHz; }

    // R-channel accessors.
    bool  isValid_R()           const noexcept { return chR.valid; }
    float getFundamentalHz_R()  const noexcept { return chR.fundamentalHz; }
    int   getFundamentalBin_R() const noexcept { return chR.fundamentalBin; }
    const std::vector<float>& getLiveDifferentialDb_R() const noexcept { return chR.liveDifferentialDb; }
    const std::vector<float>& getPeakDifferentialDb_R() const noexcept { return chR.peakDifferentialDb; }
    float getPeakResidueDb_R()  const noexcept { return chR.peakResidueDb; }
    float getPeakResidueHz_R()  const noexcept { return chR.peakResidueHz; }

    // Diff: R - L per bin. Available for both the live and peak-hold
    // arrays. Bins where either channel has no measurement carry
    // kNoMeasurementDb so the renderer breaks the path there.
    const std::vector<float>& getLiveDifferentialDb_Diff() const noexcept { return liveDifferentialDb_Diff; }
    const std::vector<float>& getPeakDifferentialDb_Diff() const noexcept { return peakDifferentialDb_Diff; }

    int   getNumBins() const noexcept { return (int) chL.peakDifferentialDb.size(); }

private:
    // Per-channel state. Each channel locates its own fundamental and
    // accumulates its own peak-held residue across frames.
    struct ChannelState
    {
        bool  valid          = false;
        int   fundamentalBin = 0;
        float fundamentalHz  = 0.0f;
        float peakResidueDb  = kNoMeasurementDb;
        float peakResidueHz  = 0.0f;
        std::vector<float> liveDifferentialDb;
        std::vector<float> peakDifferentialDb;
    };

    int   numBins      = 0;
    float binFreqScale = 0.0f;

    ChannelState chL;
    ChannelState chR;

    std::vector<float> liveDifferentialDb_Diff;
    std::vector<float> peakDifferentialDb_Diff;

    // Runs the per-frame algorithm for one channel's data into `ch`.
    void updateChannel (ChannelState& ch, const float* preDb, const float* postDb);
};
