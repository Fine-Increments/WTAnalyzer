/*
  ==============================================================================

    AliasingDetection.cpp

  ==============================================================================
*/

#include "AliasingDetection.h"

#include <algorithm>
#include <cmath>

void AliasingDetection::prepare (int numBinsIn, float binFrequencyScale)
{
    numBins      = numBinsIn;
    binFreqScale = binFrequencyScale;

    chL.liveDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);
    chL.peakDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);
    chR.liveDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);
    chR.peakDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);

    liveDifferentialDb_Diff.assign ((size_t) numBins, kNoMeasurementDb);
    peakDifferentialDb_Diff.assign ((size_t) numBins, kNoMeasurementDb);

    reset();
}

void AliasingDetection::reset()
{
    auto resetCh = [] (ChannelState& c)
    {
        c.valid          = false;
        c.fundamentalBin = 0;
        c.fundamentalHz  = 0.0f;
        c.peakResidueDb  = kNoMeasurementDb;
        c.peakResidueHz  = 0.0f;
        std::fill (c.liveDifferentialDb.begin(), c.liveDifferentialDb.end(), kNoMeasurementDb);
        std::fill (c.peakDifferentialDb.begin(), c.peakDifferentialDb.end(), kNoMeasurementDb);
    };
    resetCh (chL);
    resetCh (chR);

    std::fill (liveDifferentialDb_Diff.begin(), liveDifferentialDb_Diff.end(), kNoMeasurementDb);
    std::fill (peakDifferentialDb_Diff.begin(), peakDifferentialDb_Diff.end(), kNoMeasurementDb);
}

void AliasingDetection::clearPeaks()
{
    chL.peakResidueDb = kNoMeasurementDb;
    chL.peakResidueHz = 0.0f;
    chR.peakResidueDb = kNoMeasurementDb;
    chR.peakResidueHz = 0.0f;

    std::fill (chL.peakDifferentialDb.begin(), chL.peakDifferentialDb.end(), kNoMeasurementDb);
    std::fill (chR.peakDifferentialDb.begin(), chR.peakDifferentialDb.end(), kNoMeasurementDb);
    std::fill (peakDifferentialDb_Diff.begin(), peakDifferentialDb_Diff.end(), kNoMeasurementDb);
    // live arrays regenerate every frame; wiping them causes a one-frame flicker.
}

void AliasingDetection::update (const float* preDbL, const float* postDbL,
                                const float* preDbR, const float* postDbR)
{
    updateChannel (chL, preDbL, postDbL);
    updateChannel (chR, preDbR, postDbR);

    // Diff = R - L per bin where both sides have a measurement.
    for (int b = 0; b < numBins; ++b)
    {
        const float lLive = chL.liveDifferentialDb[(size_t) b];
        const float rLive = chR.liveDifferentialDb[(size_t) b];
        const float lPeak = chL.peakDifferentialDb[(size_t) b];
        const float rPeak = chR.peakDifferentialDb[(size_t) b];

        liveDifferentialDb_Diff[(size_t) b] =
            (lLive > kNoMeasurementDb + 0.5f && rLive > kNoMeasurementDb + 0.5f)
                ? rLive - lLive : kNoMeasurementDb;
        peakDifferentialDb_Diff[(size_t) b] =
            (lPeak > kNoMeasurementDb + 0.5f && rPeak > kNoMeasurementDb + 0.5f)
                ? rPeak - lPeak : kNoMeasurementDb;
    }
}

void AliasingDetection::updateChannel (ChannelState& ch,
                                       const float* preDb, const float* postDb)
{
    constexpr float kMinUsefulFreq = 20.0f;
    constexpr float kMaxUsefulFreq = 20000.0f;

    int   peakBin = -1;
    float peakDb  = -1000.0f;

    for (int b = 1; b < numBins; ++b)
    {
        const float f = (float) b * binFreqScale;
        if (f < kMinUsefulFreq) continue;
        if (f > kMaxUsefulFreq) break;

        if (preDb[b] > peakDb)
        {
            peakDb  = preDb[b];
            peakBin = b;
        }
    }

    if (peakBin < 0 || peakDb < kFundamentalMinDb)
    {
        // No valid fundamental. Wipe the live differential so the trace
        // collapses; peak-held data persists for the user to inspect.
        ch.valid = false;
        std::fill (ch.liveDifferentialDb.begin(), ch.liveDifferentialDb.end(), kNoMeasurementDb);
        return;
    }

    ch.valid          = true;
    ch.fundamentalBin = peakBin;
    ch.fundamentalHz  = (float) peakBin * binFreqScale;

    int   currentPeakBin = -1;
    float currentPeakDb  = ch.peakResidueDb;

    for (int b = 1; b < numBins; ++b)
    {
        const int harmonicIdx = (b + ch.fundamentalBin / 2) / ch.fundamentalBin;
        const int gridBin     = harmonicIdx * ch.fundamentalBin;
        if (std::abs (b - gridBin) <= kGridToleranceBins)
        {
            ch.liveDifferentialDb[(size_t) b] = kNoMeasurementDb;
            continue;
        }

        const float preAmp  = std::pow (10.0f, preDb [b] / 20.0f);
        const float postAmp = std::pow (10.0f, postDb[b] / 20.0f);
        const float addedPower = std::max (0.0f, postAmp * postAmp - preAmp * preAmp);
        if (addedPower < 1.0e-20f)
        {
            ch.liveDifferentialDb[(size_t) b] = kNoMeasurementDb;
            continue;
        }

        const float addedDb = 10.0f * std::log10 (addedPower);
        ch.liveDifferentialDb[(size_t) b] = addedDb;

        if (ch.peakDifferentialDb[(size_t) b] < addedDb)
        {
            ch.peakDifferentialDb[(size_t) b] = addedDb;
            if (addedDb > currentPeakDb)
            {
                currentPeakDb  = addedDb;
                currentPeakBin = b;
            }
        }
    }

    if (currentPeakBin > 0)
    {
        ch.peakResidueDb = currentPeakDb;
        ch.peakResidueHz = (float) currentPeakBin * binFreqScale;
    }
}
