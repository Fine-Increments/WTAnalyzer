/*
  ==============================================================================

    THDMeasurement.cpp

  ==============================================================================
*/

#include "THDMeasurement.h"

#include <algorithm>
#include <cmath>

void THDMeasurement::prepare (int numSpectrumBins, float binFrequencyScale)
{
    numBins      = numSpectrumBins;
    binFreqScale = binFrequencyScale;
    reset();
}

void THDMeasurement::reset()
{
    valid             = false;
    fundamentalBin    = 0;
    fundamentalHz     = 0.0f;
    thdPercent        = 0.0f;
    thdDb             = kNoMeasurementDb;
    numValidHarmonics = 0;
    std::fill (harmonicDb     .begin(), harmonicDb     .end(), kNoMeasurementDb);
    std::fill (harmonicRatioDb.begin(), harmonicRatioDb.end(), kNoMeasurementDb);
}

void THDMeasurement::update (const float* preDb, const float* postDb)
{
    // Restrict the search range so we have room for the harmonic series to
    // sit below Nyquist. A fundamental at 18 kHz would only leave one
    // meaningful harmonic before aliasing, which isn't a useful THD readout.
    constexpr float kMinUsefulFreq = 20.0f;
    constexpr float kMaxUsefulFreq = 12000.0f;

    int   peakBin = -1;
    float peakDb  = -1000.0f;

    for (int bin = 1; bin < numBins; ++bin)   // skip DC
    {
        const float freq = (float) bin * binFreqScale;
        if (freq < kMinUsefulFreq) continue;
        if (freq > kMaxUsefulFreq) break;

        if (preDb[bin] > peakDb)
        {
            peakDb  = preDb[bin];
            peakBin = bin;
        }
    }

    if (peakBin < 0 || peakDb < kFundamentalMinDb)
    {
        // No valid fundamental in pre signal.
        valid             = false;
        thdPercent        = 0.0f;
        thdDb             = kNoMeasurementDb;
        numValidHarmonics = 0;
        return;
    }

    fundamentalBin = peakBin;
    fundamentalHz  = (float) fundamentalBin * binFreqScale;

    // Read post at fundamental and each harmonic bin.
    std::fill (harmonicDb.begin(), harmonicDb.end(), kNoMeasurementDb);
    harmonicDb[0] = postDb[fundamentalBin];
    numValidHarmonics = 1;

    for (int n = 2; n <= kMaxHarmonics; ++n)
    {
        const int harmonicBin = fundamentalBin * n;
        if (harmonicBin >= numBins)
            break;   // above Nyquist; we're done

        harmonicDb[n - 1] = postDb[harmonicBin];
        numValidHarmonics = n;
    }

    // Ratios relative to the fundamental (harmonic 1).
    for (int i = 0; i < numValidHarmonics; ++i)
        harmonicRatioDb[i] = harmonicDb[i] - harmonicDb[0];

    // Total THD: sqrt(sum of harmonic_n^2 for n = 2..N) / fundamental.
    // Convert each dB level back to linear amplitude, sum the powers
    // (amplitude^2) of the harmonics, take sqrt, divide by fundamental amp.
    const float fundamentalAmp = std::pow (10.0f, harmonicDb[0] / 20.0f);
    float       harmonicPower  = 0.0f;

    for (int i = 1; i < numValidHarmonics; ++i)   // start at h2 (index 1)
    {
        if (harmonicDb[i] <= kNoMeasurementDb + 1.0f)
            continue;
        const float amp = std::pow (10.0f, harmonicDb[i] / 20.0f);
        harmonicPower += amp * amp;
    }

    if (fundamentalAmp > 1.0e-10f)
    {
        const float thdRatio = std::sqrt (harmonicPower) / fundamentalAmp;
        thdPercent = thdRatio * 100.0f;
        thdDb      = 20.0f * std::log10 (thdRatio + 1.0e-10f);
    }
    else
    {
        thdPercent = 0.0f;
        thdDb      = kNoMeasurementDb;
    }

    valid = true;
}

float THDMeasurement::getHarmonicDb (int harmonic) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;
    return harmonicDb[(size_t) idx];
}

float THDMeasurement::getHarmonicRatioDb (int harmonic) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;
    return harmonicRatioDb[(size_t) idx];
}
