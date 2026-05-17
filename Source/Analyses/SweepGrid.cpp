/*
  ==============================================================================

    SweepGrid.cpp

  ==============================================================================
*/

#include "SweepGrid.h"

#include <algorithm>

void SweepGrid::reset()
{
    gridL.fill (kNoData);
    gridR.fill (kNoData);

    for (auto& b : bucketHasData)
        b.store (false, std::memory_order_relaxed);

    filledBuckets.store (0, std::memory_order_release);
}

void SweepGrid::captureFrame (float position, const float* rowL, const float* rowR, int numCols)
{
    const float clamped = std::clamp (position, 0.0f, 1.0f);
    int bucket = (int) (clamped * (float) (kPositionBuckets - 1) + 0.5f);
    bucket = std::clamp (bucket, 0, kPositionBuckets - 1);

    const int n = std::min (numCols, kMaxCols);
    float* rL = gridL.data() + (size_t) bucket * (size_t) kMaxCols;
    float* rR = gridR.data() + (size_t) bucket * (size_t) kMaxCols;
    for (int i = 0; i < n; ++i)
    {
        rL[i] = rowL[i];
        rR[i] = rowR[i];
    }

    if (! bucketHasData[(size_t) bucket].exchange (true, std::memory_order_acq_rel))
        filledBuckets.fetch_add (1, std::memory_order_relaxed);
}

float SweepGrid::getValueL (int bucket, int col) const noexcept
{
    if (bucket < 0 || bucket >= kPositionBuckets || col < 0 || col >= kMaxCols) return kNoData;
    if (! bucketHasData[(size_t) bucket].load (std::memory_order_acquire)) return kNoData;
    return gridL[(size_t) bucket * (size_t) kMaxCols + (size_t) col];
}

float SweepGrid::getValueR (int bucket, int col) const noexcept
{
    if (bucket < 0 || bucket >= kPositionBuckets || col < 0 || col >= kMaxCols) return kNoData;
    if (! bucketHasData[(size_t) bucket].load (std::memory_order_acquire)) return kNoData;
    return gridR[(size_t) bucket * (size_t) kMaxCols + (size_t) col];
}
