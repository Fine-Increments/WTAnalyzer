/*
  ==============================================================================

    FrequencyResponse.cpp

  ==============================================================================
*/

#include "FrequencyResponse.h"

#include <algorithm>

void FrequencyResponse::prepare (int numBins)
{
    responseDb.assign ((size_t) numBins, kNoMeasurementDb);
}

void FrequencyResponse::reset()
{
    std::fill (responseDb.begin(), responseDb.end(), kNoMeasurementDb);
}

void FrequencyResponse::update (const float* preDb, const float* postDb)
{
    const int n = (int) responseDb.size();

    for (int i = 0; i < n; ++i)
    {
        if (preDb[i] > kMinValidPreDb)
            responseDb[i] = postDb[i] - preDb[i];   // dB subtraction = linear-domain division
        else
            responseDb[i] = kNoMeasurementDb;
    }
}
