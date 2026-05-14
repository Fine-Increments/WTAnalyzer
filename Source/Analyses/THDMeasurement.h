/*
  ==============================================================================

    THDMeasurement.h
    Total Harmonic Distortion analysis.

    Three concurrent views are produced from each spectrum frame:

      - Pre (classical):  Pre's own harmonics, in dB relative to pre's
                          fundamental. Useful for verifying that the test
                          signal really is a clean sine.
      - Post (classical): Post's own harmonics, in dB relative to post's
                          fundamental. The classical "what does the device
                          produce" view.
      - Diff:             Added harmonic energy, per harmonic, in dB relative
                          to pre's fundamental. Computed from
                              added_amp_n = sqrt(max(0, post_n^2 - pre_n^2))
                          so it isolates energy the device added on top of
                          whatever was already present in the input.

    The top-level THD% / THD dB readout is always computed from the Diff
    view: total added harmonic energy across h2..hN divided by pre's
    fundamental. The classical views exist only as visualisations of each
    signal's harmonic content - they do not produce a THD percentage.

    Caller drives this from the spectrum-FFT path - no new DSP stream, just a
    derived measurement on the pre/post dB arrays we already produce.

  ==============================================================================
*/

#pragma once

#include <array>

class THDMeasurement
{
public:
    static constexpr int kMaxHarmonics = 16;

    // Pre's fundamental bin must read at least this loud for the measurement
    // to be considered valid. Below this we declare "no valid input" rather
    // than report garbage. Set well above the FFT noise floor (~-110 dB FS)
    // so the panel cleanly transitions to "no signal" when the test tone
    // ends - otherwise harmonic ratios approach 0 dB as their absolute
    // levels and the fundamental converge on the noise floor.
    static constexpr float kFundamentalMinDb = -30.0f;

    // Sentinel for harmonic dB slots where no valid measurement exists
    // (harmonic above Nyquist, or analysis not yet primed).
    static constexpr float kNoMeasurementDb = -200.0f;

    // Selects which set of bars to expose via the getHarmonic* accessors.
    enum class Source { Diff, Pre, Post };

    void prepare (int numSpectrumBins, float binFrequencyScale);
    void reset();

    // Operates on the same pre/post dB arrays the spectrum FFT produces.
    // Each array must hold at least numSpectrumBins values.
    void update (const float* preDb, const float* postDb);

    // Was the most recent update able to find a valid fundamental?
    bool  isValid()              const noexcept { return valid; }

    // Fundamental frequency (Hz) and FFT bin index (located in pre). Only
    // meaningful when isValid().
    float getFundamentalHz()     const noexcept { return fundamentalHz; }
    int   getFundamentalBin()    const noexcept { return fundamentalBin; }

    // Total THD across harmonics 2..N. Always the differential measurement:
    // added harmonic energy in post relative to pre's fundamental.
    float getTotalThdPercent()   const noexcept { return thdPercent; }
    float getTotalThdDb()        const noexcept { return thdDb; }

    // Number of harmonics that fit below Nyquist for the current fundamental.
    int   getNumValidHarmonics() const noexcept { return numValidHarmonics; }

    // harmonic = 1 is the fundamental, 2 is the 2nd harmonic, etc.
    // Returns kNoMeasurementDb for harmonics out of range or above Nyquist.
    //
    // getHarmonicDb is only meaningful for Pre / Post (absolute spectrum
    // magnitudes). For Diff it returns the same value as getHarmonicRatioDb
    // since the differential bar is a ratio by construction.
    float getHarmonicDb      (Source source, int harmonic) const noexcept;
    float getHarmonicRatioDb (Source source, int harmonic) const noexcept;

    // Fundamental level (h1) of the pre and post signals in dB, for the
    // subline readout. Pre's value drives the canonical "Fundamental at X dB"
    // text since pre is the test signal reference.
    float getPreFundamentalDb()  const noexcept { return preFundamentalDb;  }
    float getPostFundamentalDb() const noexcept { return postFundamentalDb; }

private:
    struct SourceData
    {
        std::array<float, kMaxHarmonics> harmonicDb      {};
        std::array<float, kMaxHarmonics> harmonicRatioDb {};
    };

    SourceData preData;
    SourceData postData;
    SourceData diffData;   // ratio dB relative to pre fundamental (added energy)

    int   numBins        = 0;
    float binFreqScale   = 0.0f;

    bool  valid              = false;
    int   fundamentalBin     = 0;
    float fundamentalHz      = 0.0f;
    float thdPercent         = 0.0f;
    float thdDb              = kNoMeasurementDb;
    float preFundamentalDb   = kNoMeasurementDb;
    float postFundamentalDb  = kNoMeasurementDb;
    int   numValidHarmonics  = 0;

    static void resetSourceData (SourceData& d);
};
