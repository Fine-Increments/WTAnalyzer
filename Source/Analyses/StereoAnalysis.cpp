/*
  ==============================================================================

    StereoAnalysis.cpp

  ==============================================================================
*/

#include "StereoAnalysis.h"

#include <algorithm>
#include <cmath>

void StereoAnalysis::prepare (int numBins)
{
    divergence   .assign ((size_t) numBins, kNoMeasurementDb);
    frLSmoothed  .assign ((size_t) numBins, kNoMeasurementDb);
    frRSmoothed  .assign ((size_t) numBins, kNoMeasurementDb);
    preHasSignal .assign ((size_t) numBins, 0);
    postHasSignal.assign ((size_t) numBins, 0);
}

void StereoAnalysis::reset()
{
    std::fill (divergence .begin(), divergence .end(), kNoMeasurementDb);
    std::fill (frLSmoothed.begin(), frLSmoothed.end(), kNoMeasurementDb);
    std::fill (frRSmoothed.begin(), frRSmoothed.end(), kNoMeasurementDb);
    std::fill (preHasSignal .begin(), preHasSignal .end(), 0);
    std::fill (postHasSignal.begin(), postHasSignal.end(), 0);
}

void StereoAnalysis::update (const float* preDbL,  const float* preDbR,
                             const float* postDbL, const float* postDbR)
{
    const int n = (int) divergence.size();
    constexpr float kSentinelFloor = kNoMeasurementDb + 0.5f;

    // Per-bin EMA. A no-measurement raw value resets the smoothed value to
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

        const bool bothOk = preHasSignal[(size_t) i] != 0
                         && postHasSignal[(size_t) i] != 0;

        // Smooth each channel's device response FR_x = post_x - pre_x.
        const float frLRaw = bothOk ? (postDbL[i] - preDbL[i]) : kNoMeasurementDb;
        const float frRRaw = bothOk ? (postDbR[i] - preDbR[i]) : kNoMeasurementDb;
        frLSmoothed[(size_t) i] = smooth (frLSmoothed[(size_t) i], frLRaw);
        frRSmoothed[(size_t) i] = smooth (frRSmoothed[(size_t) i], frRRaw);

        const float fL = frLSmoothed[(size_t) i];
        const float fR = frRSmoothed[(size_t) i];
        if (fL <= kSentinelFloor || fR <= kSentinelFloor)
        {
            divergence[(size_t) i] = kNoMeasurementDb;
        }
        else
        {
            // Magnitude = how much the device decorrelated the channels.
            // Sign attributes it to the channel modified MORE (larger
            // |FR|): + = R was acted on, - = L. Boost vs cut polarity is
            // intentionally not encoded.
            const float mag = std::abs (fR - fL);
            divergence[(size_t) i] = (std::abs (fR) >= std::abs (fL)) ? mag : -mag;
        }
    }
}
