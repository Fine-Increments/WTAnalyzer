/*
  ==============================================================================

    SweepCurve.cpp

  ==============================================================================
*/

#include "SweepCurve.h"

#include <algorithm>

void SweepCurve::reset()
{
    curveL.fill (kNoData);
    curveR.fill (kNoData);
    sumL  .fill (0.0);
    sumR  .fill (0.0);
    countL.fill (0);
    countR.fill (0);

    for (auto& b : bucketHasData)
        b.store (false, std::memory_order_relaxed);

    filledBuckets.store (0, std::memory_order_release);
}

void SweepCurve::captureFrame (float position, float valL, float valR)
{
    const float clamped = std::clamp (position, 0.0f, 1.0f);
    int bucket = (int) (clamped * (float) (kPositionBuckets - 1) + 0.5f);
    bucket = std::clamp (bucket, 0, kPositionBuckets - 1);
    const auto b = (size_t) bucket;

    bool any = false;

    // Fold each valid channel into its bucket's running mean. An invalid
    // channel leaves that channel's mean untouched (stays kNoData if it
    // never had a sample here).
    if (valL != kNoData)
    {
        sumL[b] += (double) valL;
        ++countL[b];
        curveL[b] = (float) (sumL[b] / (double) countL[b]);
        any = true;
    }
    if (valR != kNoData)
    {
        sumR[b] += (double) valR;
        ++countR[b];
        curveR[b] = (float) (sumR[b] / (double) countR[b]);
        any = true;
    }

    if (any && ! bucketHasData[b].exchange (true, std::memory_order_acq_rel))
        filledBuckets.fetch_add (1, std::memory_order_relaxed);
}

float SweepCurve::getValueL (int bucket) const noexcept
{
    if (bucket < 0 || bucket >= kPositionBuckets) return kNoData;
    if (! bucketHasData[(size_t) bucket].load (std::memory_order_acquire)) return kNoData;
    return curveL[(size_t) bucket];
}

float SweepCurve::getValueR (int bucket) const noexcept
{
    if (bucket < 0 || bucket >= kPositionBuckets) return kNoData;
    if (! bucketHasData[(size_t) bucket].load (std::memory_order_acquire)) return kNoData;
    return curveR[(size_t) bucket];
}
