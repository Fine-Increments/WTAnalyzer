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

    liveDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);
    peakDifferentialDb.assign ((size_t) numBins, kNoMeasurementDb);

    reset();
}

void AliasingDetection::reset()
{
    valid          = false;
    fundamentalBin = 0;
    fundamentalHz  = 0.0f;
    peakResidueDb  = kNoMeasurementDb;
    peakResidueHz  = 0.0f;

    std::fill (liveDifferentialDb.begin(), liveDifferentialDb.end(), kNoMeasurementDb);
    std::fill (peakDifferentialDb.begin(), peakDifferentialDb.end(), kNoMeasurementDb);
}

void AliasingDetection::clearPeaks()
{
    peakResidueDb  = kNoMeasurementDb;
    peakResidueHz  = 0.0f;

    std::fill (peakDifferentialDb.begin(), peakDifferentialDb.end(), kNoMeasurementDb);
    // liveDifferentialDb is regenerated every frame; no need to wipe it
    // and doing so causes a one-frame flicker.
}

void AliasingDetection::update (const float* preDb, const float* postDb)
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
        valid = false;
        std::fill (liveDifferentialDb.begin(), liveDifferentialDb.end(), kNoMeasurementDb);
        return;
    }

    valid          = true;
    fundamentalBin = peakBin;
    fundamentalHz  = (float) peakBin * binFreqScale;

    // Compute the differential per bin. On-grid bins and bins where no
    // energy was added carry kNoMeasurementDb so traces break the path
    // there.
    int   currentPeakBin = -1;
    float currentPeakDb  = peakResidueDb;

    for (int b = 1; b < numBins; ++b)
    {
        const int harmonicIdx = (b + fundamentalBin / 2) / fundamentalBin;
        const int gridBin     = harmonicIdx * fundamentalBin;
        if (std::abs (b - gridBin) <= kGridToleranceBins)
        {
            liveDifferentialDb[(size_t) b] = kNoMeasurementDb;
            continue;
        }

        const float preAmp  = std::pow (10.0f, preDb [b] / 20.0f);
        const float postAmp = std::pow (10.0f, postDb[b] / 20.0f);
        const float addedPower = std::max (0.0f, postAmp * postAmp - preAmp * preAmp);
        if (addedPower < 1.0e-20f)
        {
            liveDifferentialDb[(size_t) b] = kNoMeasurementDb;
            continue;
        }

        const float addedDb = 10.0f * std::log10 (addedPower);
        liveDifferentialDb[(size_t) b] = addedDb;

        if (peakDifferentialDb[(size_t) b] < addedDb)
        {
            peakDifferentialDb[(size_t) b] = addedDb;
            if (addedDb > currentPeakDb)
            {
                currentPeakDb  = addedDb;
                currentPeakBin = b;
            }
        }
    }

    if (currentPeakBin > 0)
    {
        peakResidueDb = currentPeakDb;
        peakResidueHz = (float) currentPeakBin * binFreqScale;
    }
}
