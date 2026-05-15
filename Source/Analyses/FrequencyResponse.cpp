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
    binValidL      .assign ((size_t) numBins, 0);
    binValidR      .assign ((size_t) numBins, 0);
}

void FrequencyResponse::reset()
{
    std::fill (responseDb     .begin(), responseDb     .end(), kNoMeasurementDb);
    std::fill (responseDb_R   .begin(), responseDb_R   .end(), kNoMeasurementDb);
    std::fill (responseDb_Diff.begin(), responseDb_Diff.end(), kNoMeasurementDb);
    std::fill (binValidL      .begin(), binValidL      .end(), 0);
    std::fill (binValidR      .begin(), binValidR      .end(), 0);
}

void FrequencyResponse::update (const float* preDbL, const float* postDbL,
                                const float* preDbR, const float* postDbR)
{
    const int n = (int) responseDb.size();
    constexpr float kSentinelFloor = kNoMeasurementDb + 0.5f;

    // Per-bin EMA smoother. If the new raw value is no-measurement, the
    // smoothed value resets to sentinel (don't blend into noise). If the
    // previous smoothed value was sentinel and the new is valid, re-seed
    // with the raw value (no warm-up artifact).
    auto smooth = [] (float prev, float raw) -> float
    {
        if (raw <= kSentinelFloor) return kNoMeasurementDb;
        if (prev <= kSentinelFloor) return raw;
        return kSmoothingAlpha * raw + (1.0f - kSmoothingAlpha) * prev;
    };

    for (int i = 0; i < n; ++i)
    {
        // Hysteresis: bin enters the valid pool when pre rises above
        // kValidEnterDb; leaves only when pre falls below kValidLeaveDb.
        // This eliminates the "flicker at the threshold edge" pattern
        // that was fragmenting the trace on complex signals.
        if (binValidL[(size_t) i])
        {
            if (preDbL[i] < kValidLeaveDb) binValidL[(size_t) i] = 0;
        }
        else
        {
            if (preDbL[i] > kValidEnterDb) binValidL[(size_t) i] = 1;
        }

        if (binValidR[(size_t) i])
        {
            if (preDbR[i] < kValidLeaveDb) binValidR[(size_t) i] = 0;
        }
        else
        {
            if (preDbR[i] > kValidEnterDb) binValidR[(size_t) i] = 1;
        }

        const bool lValid = binValidL[(size_t) i] != 0;
        const bool rValid = binValidR[(size_t) i] != 0;

        const float lRaw = lValid ? (postDbL[i] - preDbL[i]) : kNoMeasurementDb;
        const float rRaw = rValid ? (postDbR[i] - preDbR[i]) : kNoMeasurementDb;
        const float diffRaw = (lValid && rValid) ? (rRaw - lRaw) : kNoMeasurementDb;

        responseDb     [i] = smooth (responseDb     [i], lRaw);
        responseDb_R   [i] = smooth (responseDb_R   [i], rRaw);
        responseDb_Diff[i] = smooth (responseDb_Diff[i], diffRaw);
    }
}
