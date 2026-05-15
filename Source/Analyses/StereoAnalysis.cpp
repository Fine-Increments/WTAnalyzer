/*
  ==============================================================================

    StereoAnalysis.cpp

  ==============================================================================
*/

#include "StereoAnalysis.h"

#include <algorithm>

void StereoAnalysis::prepare (int numBins)
{
    preDiv       .assign ((size_t) numBins, kNoMeasurementDb);
    postDiv      .assign ((size_t) numBins, kNoMeasurementDb);
    deviceDiv    .assign ((size_t) numBins, kNoMeasurementDb);
    preHasSignal .assign ((size_t) numBins, 0);
    postHasSignal.assign ((size_t) numBins, 0);
}

void StereoAnalysis::reset()
{
    std::fill (preDiv   .begin(), preDiv   .end(), kNoMeasurementDb);
    std::fill (postDiv  .begin(), postDiv  .end(), kNoMeasurementDb);
    std::fill (deviceDiv.begin(), deviceDiv.end(), kNoMeasurementDb);
    std::fill (preHasSignal .begin(), preHasSignal .end(), 0);
    std::fill (postHasSignal.begin(), postHasSignal.end(), 0);
}

void StereoAnalysis::update (const float* preDbL,  const float* preDbR,
                             const float* postDbL, const float* postDbR)
{
    const int n = (int) preDiv.size();
    constexpr float kSentinelFloor = kNoMeasurementDb + 0.5f;

    // Per-bin EMA. New no-measurement value resets the smoothed value to
    // sentinel (don't blend into noise); a sentinel previous re-seeds from
    // the raw value (no warm-up artefact).
    auto smooth = [] (float prev, float raw) -> float
    {
        if (raw <= kSentinelFloor)  return kNoMeasurementDb;
        if (prev <= kSentinelFloor) return raw;
        return kSmoothingAlpha * raw + (1.0f - kSmoothingAlpha) * prev;
    };

    for (int i = 0; i < n; ++i)
    {
        // Hysteresis on "has signal" for each side.
        const float preMax  = std::max (preDbL[i],  preDbR[i]);
        const float postMax = std::max (postDbL[i], postDbR[i]);

        if (preHasSignal[(size_t) i])
        {
            if (preMax < kSignalLeaveDb) preHasSignal[(size_t) i] = 0;
        }
        else
        {
            if (preMax > kSignalEnterDb) preHasSignal[(size_t) i] = 1;
        }

        if (postHasSignal[(size_t) i])
        {
            if (postMax < kSignalLeaveDb) postHasSignal[(size_t) i] = 0;
        }
        else
        {
            if (postMax > kSignalEnterDb) postHasSignal[(size_t) i] = 1;
        }

        const bool preOk  = preHasSignal[(size_t) i]  != 0;
        const bool postOk = postHasSignal[(size_t) i] != 0;

        const float preRaw  = preOk  ? (preDbR[i]  - preDbL[i])  : kNoMeasurementDb;
        const float postRaw = postOk ? (postDbR[i] - postDbL[i]) : kNoMeasurementDb;

        // Device-added divergence needs both pre and post measurable: it
        // is the post stereo image minus the pre stereo image.
        const float devRaw = (preOk && postOk)
            ? ((postDbR[i] - postDbL[i]) - (preDbR[i] - preDbL[i]))
            : kNoMeasurementDb;

        preDiv   [(size_t) i] = smooth (preDiv   [(size_t) i], preRaw);
        postDiv  [(size_t) i] = smooth (postDiv  [(size_t) i], postRaw);
        deviceDiv[(size_t) i] = smooth (deviceDiv[(size_t) i], devRaw);
    }
}
