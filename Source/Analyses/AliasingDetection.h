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

    The class also peak-holds pre and post spectra independently so the
    display can offer a "hold" view for those channels when running a sweep
    test. Peaks are not auto-reset; the display calls clearPeaks() in
    response to the user's Clear button between test runs.

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

    void update (const float* preDb, const float* postDb);

    bool  isValid()           const noexcept { return valid; }
    float getFundamentalHz()  const noexcept { return fundamentalHz; }
    int   getFundamentalBin() const noexcept { return fundamentalBin; }

    // Per-frame off-grid differential (post^2 - pre^2 in dB). Bins that
    // are on the grid OR contribute no added energy hold kNoMeasurementDb.
    const std::vector<float>& getLiveDifferentialDb() const noexcept { return liveDifferentialDb; }

    // Peak-hold of the above across frames since the last clear / reset.
    const std::vector<float>& getPeakDifferentialDb() const noexcept { return peakDifferentialDb; }

    // Peak-hold of the raw pre and post spectra. Used by the display
    // when the user toggles Hold while viewing the Pre or Post trace.
    const std::vector<float>& getPeakPreDb()  const noexcept { return peakPreDb;  }
    const std::vector<float>& getPeakPostDb() const noexcept { return peakPostDb; }

    int   getNumBins() const noexcept { return (int) peakDifferentialDb.size(); }

    // Loudest peak-held differential and its frequency, for the HUD.
    float getPeakResidueDb() const noexcept { return peakResidueDb; }
    float getPeakResidueHz() const noexcept { return peakResidueHz; }

private:
    int   numBins        = 0;
    float binFreqScale   = 0.0f;

    bool  valid          = false;
    int   fundamentalBin = 0;
    float fundamentalHz  = 0.0f;

    float peakResidueDb  = kNoMeasurementDb;
    float peakResidueHz  = 0.0f;

    std::vector<float> liveDifferentialDb;
    std::vector<float> peakDifferentialDb;
    std::vector<float> peakPreDb;
    std::vector<float> peakPostDb;
};
