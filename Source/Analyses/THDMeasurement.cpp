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

void THDMeasurement::resetChannel (ChannelState& ch)
{
    ch.valid             = false;
    ch.fundamentalBin    = 0;
    ch.fundamentalHz     = 0.0f;
    ch.thdPercent        = 0.0f;
    ch.thdDb             = kNoMeasurementDb;
    ch.preFundamentalDb  = kNoMeasurementDb;
    ch.postFundamentalDb = kNoMeasurementDb;
    ch.numValidHarmonics = 0;

    resetSourceData (ch.preData);
    resetSourceData (ch.postData);
    resetSourceData (ch.diffData);
}

void THDMeasurement::prepare (int numSpectrumBins, float binFrequencyScale)
{
    numBins      = numSpectrumBins;
    binFreqScale = binFrequencyScale;
    reset();
}

void THDMeasurement::reset()
{
    resetChannel (chL);
    resetChannel (chR);
}

void THDMeasurement::update (const float* preDbL, const float* postDbL,
                             const float* preDbR, const float* postDbR)
{
    updateChannel (chL, preDbL, postDbL);
    updateChannel (chR, preDbR, postDbR);
}

void THDMeasurement::updateChannel (ChannelState& ch,
                                    const float* preDb, const float* postDb)
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
        // No valid fundamental in this channel's pre signal.
        ch.valid             = false;
        ch.thdPercent        = 0.0f;
        ch.thdDb             = kNoMeasurementDb;
        ch.numValidHarmonics = 0;
        return;
    }

    ch.fundamentalBin = peakBin;
    ch.fundamentalHz  = (float) ch.fundamentalBin * binFreqScale;

    // Reset all three sources before filling. resetSourceData seeds every
    // slot with kNoMeasurementDb so out-of-range harmonics naturally read
    // as "no data" downstream.
    resetSourceData (ch.preData);
    resetSourceData (ch.postData);
    resetSourceData (ch.diffData);

    // Sum energy over a small bin cluster around each target rather than
    // reading the single exact bin. A fundamental that does not land on an
    // integer bin (e.g. 1 kHz at 48 kHz / 8192 = bin 170.6) spreads its energy
    // across the Hann main lobe, so a single-bin read under-reports the tone -
    // and every harmonic - by up to ~1.4 dB, deflating THD%. The cluster half
    // width is capped at half the harmonic spacing so clusters never overlap
    // for low fundamentals. Applied identically to fundamental and harmonics,
    // so the harmonic/fundamental ratio stays exact.
    const int clusterW = std::max (0, std::min (2, (ch.fundamentalBin - 1) / 2));

    auto clusterDb = [this, clusterW] (const float* db, int centerBin)
    {
        const int a = std::max (0, centerBin - clusterW);
        const int b = std::min (numBins - 1, centerBin + clusterW);
        double power = 0.0;
        for (int k = a; k <= b; ++k)
        {
            const double amp = std::pow (10.0, (double) db[k] / 20.0);
            power += amp * amp;
        }
        return (float) (10.0 * std::log10 (power + 1.0e-30));
    };

    ch.preData .harmonicDb[0] = clusterDb (preDb,  ch.fundamentalBin);
    ch.postData.harmonicDb[0] = clusterDb (postDb, ch.fundamentalBin);
    ch.preFundamentalDb  = ch.preData .harmonicDb[0];
    ch.postFundamentalDb = ch.postData.harmonicDb[0];
    ch.numValidHarmonics = 1;

    for (int n = 2; n <= kMaxHarmonics; ++n)
    {
        const int harmonicBin = ch.fundamentalBin * n;
        if (harmonicBin >= numBins)
            break;   // above Nyquist; we're done

        ch.preData .harmonicDb[n - 1] = clusterDb (preDb,  harmonicBin);
        ch.postData.harmonicDb[n - 1] = clusterDb (postDb, harmonicBin);
        ch.numValidHarmonics = n;
    }

    // Classical ratios: each source's harmonics relative to its own
    // fundamental (h1 is always 0 dB by construction).
    for (int i = 0; i < ch.numValidHarmonics; ++i)
    {
        ch.preData .harmonicRatioDb[i] = ch.preData .harmonicDb[i] - ch.preData .harmonicDb[0];
        ch.postData.harmonicRatioDb[i] = ch.postData.harmonicDb[i] - ch.postData.harmonicDb[0];
    }

    const float preFundAmp = std::pow (10.0f, ch.preData.harmonicDb[0] / 20.0f);
    ch.diffData.harmonicRatioDb[0] = 0.0f;
    ch.diffData.harmonicDb     [0] = 0.0f;

    float harmonicPower = 0.0f;

    for (int i = 1; i < ch.numValidHarmonics; ++i)   // start at h2 (index 1)
    {
        const float preAmp  = std::pow (10.0f, ch.preData .harmonicDb[i] / 20.0f);
        const float postAmp = std::pow (10.0f, ch.postData.harmonicDb[i] / 20.0f);

        const float addedPower = std::max (0.0f, postAmp * postAmp - preAmp * preAmp);
        const float addedAmp   = std::sqrt (addedPower);

        harmonicPower += addedPower;

        if (preFundAmp > 1.0e-10f && addedAmp > 1.0e-12f)
        {
            const float ratio = addedAmp / preFundAmp;
            ch.diffData.harmonicRatioDb[i] = 20.0f * std::log10 (ratio);
            ch.diffData.harmonicDb     [i] = ch.diffData.harmonicRatioDb[i];
        }
        else
        {
            ch.diffData.harmonicRatioDb[i] = kNoMeasurementDb;
            ch.diffData.harmonicDb     [i] = kNoMeasurementDb;
        }
    }

    if (preFundAmp > 1.0e-10f)
    {
        const float thdRatio = std::sqrt (harmonicPower) / preFundAmp;
        ch.thdPercent = thdRatio * 100.0f;
        ch.thdDb      = 20.0f * std::log10 (thdRatio + 1.0e-10f);
    }
    else
    {
        ch.thdPercent = 0.0f;
        ch.thdDb      = kNoMeasurementDb;
    }

    ch.valid = true;
}

float THDMeasurement::getHarmonicDb (Source source, int harmonic, Channel chSel) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;

    const auto& ch = get (chSel);
    switch (source)
    {
        case Source::Pre:  return ch.preData .harmonicDb[(size_t) idx];
        case Source::Post: return ch.postData.harmonicDb[(size_t) idx];
        case Source::Diff: return ch.diffData.harmonicDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}

float THDMeasurement::getHarmonicRatioDb (Source source, int harmonic, Channel chSel) const noexcept
{
    const int idx = harmonic - 1;
    if (idx < 0 || idx >= kMaxHarmonics) return kNoMeasurementDb;

    const auto& ch = get (chSel);
    switch (source)
    {
        case Source::Pre:  return ch.preData .harmonicRatioDb[(size_t) idx];
        case Source::Post: return ch.postData.harmonicRatioDb[(size_t) idx];
        case Source::Diff: return ch.diffData.harmonicRatioDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}
