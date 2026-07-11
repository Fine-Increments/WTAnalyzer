/*
  ==============================================================================

    DynamicsCurve.cpp

  ==============================================================================
*/

#include "DynamicsCurve.h"

#include <algorithm>
#include <cmath>

void DynamicsCurve::reset()
{
    curveL.fill (kNoData);
    curveR.fill (kNoData);
    sumL  .fill (0.0);
    sumR  .fill (0.0);
    countL.fill (0);
    countR.fill (0);

    for (auto& b : binHasData)
        b.store (false, std::memory_order_relaxed);

    filledBins.store (0, std::memory_order_release);
}

int DynamicsCurve::binForInputDb (float inputDb) noexcept
{
    if (inputDb < kMinInputDb || inputDb > kMaxInputDb)
        return -1;

    const float t = (inputDb - kMinInputDb) / (kMaxInputDb - kMinInputDb);
    int bin = (int) (t * (float) (kNumBins - 1) + 0.5f);
    return std::clamp (bin, 0, kNumBins - 1);
}

float DynamicsCurve::binInputDb (int bin) noexcept
{
    const float t = (float) bin / (float) (kNumBins - 1);
    return kMinInputDb + t * (kMaxInputDb - kMinInputDb);
}

void DynamicsCurve::captureFrame (float preDbL, float postDbL,
                                  float preDbR, float postDbR)
{
    // Consume a deferred UI Clear on the audio thread, where captureFrame is
    // the only writer of the accumulators (no tear, no divide-by-zeroed-count).
    if (clearRequested.load (std::memory_order_relaxed))
    {
        clearRequested.store (false, std::memory_order_relaxed);
        reset();
    }

    // Fold each channel into the bin its input level lands in. The
    // output level is stored raw (no clamp) so a gate driving its
    // output toward silence still reads a real measurement; the
    // display clamps to its axis.
    const int bL = binForInputDb (preDbL);
    if (bL >= 0)
    {
        const auto i = (size_t) bL;
        sumL[i] += (double) postDbL;
        ++countL[i];
        curveL[i] = (float) (sumL[i] / (double) countL[i]);
        if (! binHasData[i].exchange (true, std::memory_order_acq_rel))
            filledBins.fetch_add (1, std::memory_order_relaxed);
    }

    const int bR = binForInputDb (preDbR);
    if (bR >= 0)
    {
        const auto i = (size_t) bR;
        sumR[i] += (double) postDbR;
        ++countR[i];
        curveR[i] = (float) (sumR[i] / (double) countR[i]);
        if (! binHasData[i].exchange (true, std::memory_order_acq_rel))
            filledBins.fetch_add (1, std::memory_order_relaxed);
    }
}

float DynamicsCurve::getOutputDbL (int bin) const noexcept
{
    if (bin < 0 || bin >= kNumBins) return kNoData;
    if (! binHasData[(size_t) bin].load (std::memory_order_acquire)) return kNoData;
    return curveL[(size_t) bin];
}

float DynamicsCurve::getOutputDbR (int bin) const noexcept
{
    if (bin < 0 || bin >= kNumBins) return kNoData;
    if (! binHasData[(size_t) bin].load (std::memory_order_acquire)) return kNoData;
    return curveR[(size_t) bin];
}
