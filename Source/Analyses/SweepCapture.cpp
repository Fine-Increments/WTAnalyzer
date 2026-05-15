/*
  ==============================================================================

    SweepCapture.cpp

  ==============================================================================
*/

#include "SweepCapture.h"

#include <algorithm>
#include <cmath>

void SweepCapture::prepare (int numFreqBins)
{
    numBins = numFreqBins;
    data.assign ((size_t) (kPositionBuckets * numBins), kNoDataDb);

    bucketHasData = std::vector<std::atomic<bool>> (kPositionBuckets);
    for (auto& b : bucketHasData)
        b.store (false, std::memory_order_relaxed);

    filledBuckets.store (0, std::memory_order_relaxed);
}

void SweepCapture::reset()
{
    std::fill (data.begin(), data.end(), kNoDataDb);

    for (auto& b : bucketHasData)
        b.store (false, std::memory_order_relaxed);

    filledBuckets.store (0, std::memory_order_release);
}

void SweepCapture::captureFrame (float position, const float* responseDb, int n)
{
    if (numBins <= 0 || n <= 0) return;

    const float clamped = std::clamp (position, 0.0f, 1.0f);
    int bucket = (int) (clamped * (float) (kPositionBuckets - 1) + 0.5f);
    if (bucket < 0) bucket = 0;
    if (bucket >= kPositionBuckets) bucket = kPositionBuckets - 1;

    const int copyBins = std::min (n, numBins);
    float* row = data.data() + (size_t) bucket * (size_t) numBins;
    for (int i = 0; i < copyBins; ++i)
        row[i] = responseDb[i];

    if (! bucketHasData[(size_t) bucket].exchange (true, std::memory_order_acq_rel))
        filledBuckets.fetch_add (1, std::memory_order_relaxed);
}

float SweepCapture::getValue (int positionBucket, int bin) const noexcept
{
    if (positionBucket < 0 || positionBucket >= kPositionBuckets) return kNoDataDb;
    if (bin < 0 || bin >= numBins) return kNoDataDb;
    if (! bucketHasData[(size_t) positionBucket].load (std::memory_order_acquire))
        return kNoDataDb;
    return data[(size_t) positionBucket * (size_t) numBins + (size_t) bin];
}
