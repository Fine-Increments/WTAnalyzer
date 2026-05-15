/*
  ==============================================================================

    FrequencyResponse.cpp

  ==============================================================================
*/

#include "FrequencyResponse.h"

#include <algorithm>

void FrequencyResponse::prepare (int numBins)
{
    responseDb     .assign ((size_t) numBins, kNoMeasurementDb);
    responseDb_R   .assign ((size_t) numBins, kNoMeasurementDb);
    responseDb_Diff.assign ((size_t) numBins, kNoMeasurementDb);
}

void FrequencyResponse::reset()
{
    std::fill (responseDb     .begin(), responseDb     .end(), kNoMeasurementDb);
    std::fill (responseDb_R   .begin(), responseDb_R   .end(), kNoMeasurementDb);
    std::fill (responseDb_Diff.begin(), responseDb_Diff.end(), kNoMeasurementDb);
}

void FrequencyResponse::update (const float* preDbL, const float* postDbL,
                                const float* preDbR, const float* postDbR)
{
    const int n = (int) responseDb.size();

    for (int i = 0; i < n; ++i)
    {
        const bool lValid = preDbL[i] > kMinValidPreDb;
        const bool rValid = preDbR[i] > kMinValidPreDb;

        responseDb  [i] = lValid ? (postDbL[i] - preDbL[i]) : kNoMeasurementDb;
        responseDb_R[i] = rValid ? (postDbR[i] - preDbR[i]) : kNoMeasurementDb;

        // Diff is only meaningful where both channels measure.
        responseDb_Diff[i] = (lValid && rValid)
                                ? (responseDb_R[i] - responseDb[i])
                                : kNoMeasurementDb;
    }
}
