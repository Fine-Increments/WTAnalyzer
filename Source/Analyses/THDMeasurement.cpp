/*
  ==============================================================================

    THDMeasurement.cpp

  ==============================================================================
*/

#include "THDMeasurement.h"

#include <algorithm>
#include <cmath>

void THDMeasurement::resetSourceData (SourceData& d)
{
    std::fill (d.harmonicDb     .begin(), d.harmonicDb     .end(), kNoMeasurementDb);
    std::fill (d.harmonicRatioDb.begin(), d.harmonicRatioDb.end(), kNoMeasurementDb);
}

void THDMeasurement::prepare (int numSpectrumBins, float binFrequencyScale)
{
    numBins      = numSpectrumBins;
    binFreqScale = binFrequencyScale;
    reset();
}

void THDMeasurement::reset()
{
    valid              = false;
    fundamentalBin     = 0;
    fundamentalHz      = 0.0f;
    thdPercent         = 0.0f;
    thdDb              = kNoMeasurementDb;
    preFundamentalDb   = kNoMeasurementDb;
    postFundamentalDb  = kNoMeasurementDb;
    numValidHarmonics  = 0;

    resetSourceData (preData);
    resetSourceData (postData);
    resetSourceData (diffData);
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

    // Reset all three sources before filling. resetSourceData seeds every
    // slot with kNoMeasurementDb so out-of-range harmonics naturally read
    // as "no data" downstream.
    resetSourceData (preData);
    resetSourceData (postData);
    resetSourceData (diffData);

    preData .harmonicDb[0] = preDb [fundamentalBin];
    postData.harmonicDb[0] = postDb[fundamentalBin];
    preFundamentalDb  = preData .harmonicDb[0];
    postFundamentalDb = postData.harmonicDb[0];
    numValidHarmonics = 1;

    for (int n = 2; n <= kMaxHarmonics; ++n)
    {
        const int harmonicBin = fundamentalBin * n;
        if (harmonicBin >= numBins)
            break;   // above Nyquist; we're done

        preData .harmonicDb[n - 1] = preDb [harmonicBin];
        postData.harmonicDb[n - 1] = postDb[harmonicBin];
        numValidHarmonics = n;
    }

    // Classical ratios: each source's harmonics relative to its own
    // fundamental (h1 is always 0 dB by construction).
    for (int i = 0; i < numValidHarmonics; ++i)
    {
        preData .harmonicRatioDb[i] = preData .harmonicDb[i] - preData .harmonicDb[0];
        postData.harmonicRatioDb[i] = postData.harmonicDb[i] - postData.harmonicDb[0];
    }

    // ---- Differential view + total THD% --------------------------------------
    // For each harmonic compute the added energy as
    //     added_amp_n = sqrt(max(0, post_amp_n^2 - pre_amp_n^2))
    // and express the bar value in dB relative to pre's fundamental
    // amplitude. The fundamental's bar (h1) is anchored at 0 dB - it is the
    // reference, not a distortion product.
    const float preFundAmp = std::pow (10.0f, preData.harmonicDb[0] / 20.0f);
    diffData.harmonicRatioDb[0] = 0.0f;
    diffData.harmonicDb     [0] = 0.0f;

    float harmonicPower = 0.0f;

    for (int i = 1; i < numValidHarmonics; ++i)   // start at h2 (index 1)
    {
        const float preAmp  = std::pow (10.0f, preData .harmonicDb[i] / 20.0f);
        const float postAmp = std::pow (10.0f, postData.harmonicDb[i] / 20.0f);

        const float addedPower = std::max (0.0f, postAmp * postAmp - preAmp * preAmp);
        const float addedAmp   = std::sqrt (addedPower);

        harmonicPower += addedPower;

        if (preFundAmp > 1.0e-10f && addedAmp > 1.0e-12f)
        {
            const float ratio = addedAmp / preFundAmp;
            diffData.harmonicRatioDb[i] = 20.0f * std::log10 (ratio);
            diffData.harmonicDb     [i] = diffData.harmonicRatioDb[i];
        }
        else
        {
            diffData.harmonicRatioDb[i] = kNoMeasurementDb;
            diffData.harmonicDb     [i] = kNoMeasurementDb;
        }
    }

    if (preFundAmp > 1.0e-10f)
    {
        const float thdRatio = std::sqrt (harmonicPower) / preFundAmp;
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

float THDMeasurement::getHarmonicDb (Source source, int harmonic) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;

    switch (source)
    {
        case Source::Pre:  return preData .harmonicDb[(size_t) idx];
        case Source::Post: return postData.harmonicDb[(size_t) idx];
        case Source::Diff: return diffData.harmonicDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}

float THDMeasurement::getHarmonicRatioDb (Source source, int harmonic) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;

    switch (source)
    {
        case Source::Pre:  return preData .harmonicRatioDb[(size_t) idx];
        case Source::Post: return postData.harmonicRatioDb[(size_t) idx];
        case Source::Diff: return diffData.harmonicRatioDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}
