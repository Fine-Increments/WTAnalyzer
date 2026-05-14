/*
  ==============================================================================

    THDMeasurement.h
    Total Harmonic Distortion analysis.

    Algorithm: find the loudest bin in the pre-effect spectrum (the input's
    fundamental frequency, assumed to be a sine), then read the post-effect
    spectrum at integer multiples of that bin to recover the per-harmonic
    distortion. Total THD is the RMS of harmonics 2..N divided by the
    fundamental.

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
    // to be considered valid. Below this, we declare "no valid input" rather
    // than report garbage.
    static constexpr float kFundamentalMinDb = -60.0f;

    // Sentinel for harmonic dB slots where no valid measurement exists
    // (harmonic above Nyquist, or analysis not yet primed).
    static constexpr float kNoMeasurementDb = -200.0f;

    void prepare (int numSpectrumBins, float binFrequencyScale);
    void reset();

    // Operates on the same pre/post dB arrays the spectrum FFT produces.
    // Each array must hold at least numSpectrumBins values.
    void update (const float* preDb, const float* postDb);

    // Was the most recent update able to find a valid fundamental?
    bool  isValid()              const noexcept { return valid; }

    // Fundamental frequency (Hz) and FFT bin index. Only meaningful when isValid().
    float getFundamentalHz()     const noexcept { return fundamentalHz; }
    int   getFundamentalBin()    const noexcept { return fundamentalBin; }

    // Total THD across harmonics 2..N.
    float getTotalThdPercent()   const noexcept { return thdPercent; }
    float getTotalThdDb()        const noexcept { return thdDb; }

    // Number of harmonics that fit below Nyquist for the current fundamental.
    int   getNumValidHarmonics() const noexcept { return numValidHarmonics; }

    // harmonic = 1 is the fundamental, 2 is the 2nd harmonic, etc.
    // Returns kNoMeasurementDb for harmonics out of range or above Nyquist.
    float getHarmonicDb      (int harmonic) const noexcept;   // post mag in dB
    float getHarmonicRatioDb (int harmonic) const noexcept;   // relative to fundamental

private:
    int   numBins        = 0;
    float binFreqScale   = 0.0f;

    bool  valid             = false;
    int   fundamentalBin    = 0;
    float fundamentalHz     = 0.0f;
    float thdPercent        = 0.0f;
    float thdDb             = kNoMeasurementDb;
    int   numValidHarmonics = 0;

    std::array<float, kMaxHarmonics> harmonicDb      {};
    std::array<float, kMaxHarmonics> harmonicRatioDb {};
};
