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
    divergence       .assign ((size_t) numBins, kNoMeasurementDb);
    correlation      .assign ((size_t) numBins, kNoMeasurementDb);
    frLSmoothed      .assign ((size_t) numBins, kNoMeasurementDb);
    frRSmoothed      .assign ((size_t) numBins, kNoMeasurementDb);
    crossReAccum     .assign ((size_t) numBins, 0.0f);
    autoLLAccum      .assign ((size_t) numBins, 0.0f);
    autoRRAccum      .assign ((size_t) numBins, 0.0f);
    correlationSeeded.assign ((size_t) numBins, 0);
    preHasSignal     .assign ((size_t) numBins, 0);
    postHasSignal    .assign ((size_t) numBins, 0);
    broadbandCorrelation.store (kNoMeasurementDb, std::memory_order_relaxed);
}

void StereoAnalysis::reset()
{
    std::fill (divergence .begin(), divergence .end(), kNoMeasurementDb);
    std::fill (correlation.begin(), correlation.end(), kNoMeasurementDb);
    std::fill (frLSmoothed.begin(), frLSmoothed.end(), kNoMeasurementDb);
    std::fill (frRSmoothed.begin(), frRSmoothed.end(), kNoMeasurementDb);
    std::fill (crossReAccum.begin(), crossReAccum.end(), 0.0f);
    std::fill (autoLLAccum .begin(), autoLLAccum .end(), 0.0f);
    std::fill (autoRRAccum .begin(), autoRRAccum .end(), 0.0f);
    std::fill (correlationSeeded.begin(), correlationSeeded.end(), 0);
    std::fill (preHasSignal .begin(), preHasSignal .end(), 0);
    std::fill (postHasSignal.begin(), postHasSignal.end(), 0);
    broadbandCorrelation.store (kNoMeasurementDb, std::memory_order_relaxed);
}

void StereoAnalysis::update (const float* preDbL,  const float* preDbR,
                             const float* postDbL, const float* postDbR,
                             const float* postComplexL, const float* postComplexR)
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

        // ---- Correlation: post L/R per-frequency phase coherence -------
        // Cross-spectrum Re(L * conj(R)) = lRe*rRe + lIm*rIm; auto-spectra
        // |L|^2, |R|^2. Each is EMA-averaged; coherence is the normalised
        // ratio. Gated on the post signal's own energy - this is a
        // property of the output, not the device transfer function.
        if (postHasSignal[(size_t) i] != 0)
        {
            const float lRe = postComplexL[(size_t) (2 * i)];
            const float lIm = postComplexL[(size_t) (2 * i + 1)];
            const float rRe = postComplexR[(size_t) (2 * i)];
            const float rIm = postComplexR[(size_t) (2 * i + 1)];

            const float crossRe = lRe * rRe + lIm * rIm;
            const float sLL     = lRe * lRe + lIm * lIm;
            const float sRR     = rRe * rRe + rIm * rIm;

            if (correlationSeeded[(size_t) i] != 0)
            {
                crossReAccum[(size_t) i] = kCorrelationAlpha * crossRe
                                         + (1.0f - kCorrelationAlpha) * crossReAccum[(size_t) i];
                autoLLAccum[(size_t) i]  = kCorrelationAlpha * sLL
                                         + (1.0f - kCorrelationAlpha) * autoLLAccum[(size_t) i];
                autoRRAccum[(size_t) i]  = kCorrelationAlpha * sRR
                                         + (1.0f - kCorrelationAlpha) * autoRRAccum[(size_t) i];
            }
            else
            {
                crossReAccum[(size_t) i] = crossRe;
                autoLLAccum[(size_t) i]  = sLL;
                autoRRAccum[(size_t) i]  = sRR;
                correlationSeeded[(size_t) i] = 1;
            }

            const float denom = std::sqrt (autoLLAccum[(size_t) i] * autoRRAccum[(size_t) i]);
            correlation[(size_t) i] = denom > 1.0e-12f
                ? std::clamp (crossReAccum[(size_t) i] / denom, -1.0f, 1.0f)
                : kNoMeasurementDb;
        }
        else
        {
            correlationSeeded[(size_t) i] = 0;
            correlation[(size_t) i] = kNoMeasurementDb;
        }
    }

    // Broadband correlation: sum the (already EMA-averaged) cross / auto
    // spectra over every seeded bin, then normalise. Summing the spectra
    // before the ratio makes it energy-weighted - loud frequencies
    // dominate, quiet noise barely registers. Bin 0 (DC) is excluded.
    double sumCross = 0.0, sumLL = 0.0, sumRR = 0.0;
    for (int i = 1; i < n; ++i)
    {
        if (correlationSeeded[(size_t) i] == 0) continue;
        sumCross += (double) crossReAccum[(size_t) i];
        sumLL    += (double) autoLLAccum [(size_t) i];
        sumRR    += (double) autoRRAccum [(size_t) i];
    }

    const double denom = std::sqrt (sumLL * sumRR);
    broadbandCorrelation.store (denom > 1.0e-12
        ? (float) std::clamp (sumCross / denom, -1.0, 1.0)
        : kNoMeasurementDb, std::memory_order_relaxed);
}
