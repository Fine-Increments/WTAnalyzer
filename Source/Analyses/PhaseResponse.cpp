/*
  ==============================================================================

    PhaseResponse.cpp

  ==============================================================================
*/

#include "PhaseResponse.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    // Centred-difference half-span for the group-delay derivative. A few
    // bins of span tames the per-bin derivative noise without smearing
    // real group-delay structure.
    constexpr int kGroupDelaySpan = 2;

    // Frames of output suppressed after the signal first appears. The
    // FFT window straddles silence while it fills (~4 hops for the
    // order-13 / 4x-overlap spectrum); a generous margin past that lets
    // any device-under-test startup transient pass too.
    constexpr int kSettleFrames = 16;
}

void PhaseResponse::prepareChannel (ChannelState& c, int numBins)
{
    c.crossRe     .assign ((size_t) numBins, 0.0f);
    c.crossIm     .assign ((size_t) numBins, 0.0f);
    c.hasSignal   .assign ((size_t) numBins, 0);
    c.seeded      .assign ((size_t) numBins, 0);
    c.unwrapped   .assign ((size_t) numBins, 0.0f);
    c.phaseDeg    .assign ((size_t) numBins, kNoMeasurement);
    c.groupDelayMs.assign ((size_t) numBins, kNoMeasurement);
    c.wasActive       = false;
    c.settleCountdown = 0;
}

void PhaseResponse::prepare (int n, float binFrequencyScaleHz)
{
    numBins      = n;
    binFreqScale = binFrequencyScaleHz;
    prepareChannel (chL, n);
    prepareChannel (chR, n);
}

void PhaseResponse::resetChannel (ChannelState& c)
{
    std::fill (c.crossRe     .begin(), c.crossRe     .end(), 0.0f);
    std::fill (c.crossIm     .begin(), c.crossIm     .end(), 0.0f);
    std::fill (c.hasSignal   .begin(), c.hasSignal   .end(), 0);
    std::fill (c.seeded      .begin(), c.seeded      .end(), 0);
    std::fill (c.unwrapped   .begin(), c.unwrapped   .end(), 0.0f);
    std::fill (c.phaseDeg    .begin(), c.phaseDeg    .end(), kNoMeasurement);
    std::fill (c.groupDelayMs.begin(), c.groupDelayMs.end(), kNoMeasurement);
    c.wasActive       = false;
    c.settleCountdown = 0;
}

void PhaseResponse::reset()
{
    resetChannel (chL);
    resetChannel (chR);
}

void PhaseResponse::update (const float* preDbL,       const float* preDbR,
                            const float* preComplexL,  const float* preComplexR,
                            const float* postComplexL, const float* postComplexR)
{
    updateChannel (chL, preDbL, preComplexL, postComplexL);
    updateChannel (chR, preDbR, preComplexR, postComplexR);
}

void PhaseResponse::updateChannel (ChannelState& c, const float* preDb,
                                   const float* preCx, const float* postCx)
{
    const int n = numBins;

    // ---- 1. Cross-spectrum post * conj(pre), EMA-smoothed -------------
    // Gated by the pre signal: phase is meaningless where there is no
    // input energy. A no-signal bin keeps its last smoothed value so the
    // unwrap stays continuous, but is excluded from the outputs below.
    for (int i = 0; i < n; ++i)
    {
        const float pd = preDb[i];
        if (c.hasSignal[(size_t) i])
        {
            if (pd < kSignalLeaveDb) c.hasSignal[(size_t) i] = 0;
        }
        else
        {
            if (pd > kSignalEnterDb) c.hasSignal[(size_t) i] = 1;
        }

        if (! c.hasSignal[(size_t) i])
        {
            c.seeded[(size_t) i] = 0;
            continue;
        }

        const float preRe  = preCx[(size_t) (2 * i)];
        const float preIm  = preCx[(size_t) (2 * i + 1)];
        const float postRe = postCx[(size_t) (2 * i)];
        const float postIm = postCx[(size_t) (2 * i + 1)];

        // post * conj(pre)
        const float xr = postRe * preRe + postIm * preIm;
        const float xi = postIm * preRe - postRe * preIm;

        if (c.seeded[(size_t) i])
        {
            c.crossRe[(size_t) i] = kSmoothingAlpha * xr
                                  + (1.0f - kSmoothingAlpha) * c.crossRe[(size_t) i];
            c.crossIm[(size_t) i] = kSmoothingAlpha * xi
                                  + (1.0f - kSmoothingAlpha) * c.crossIm[(size_t) i];
        }
        else
        {
            c.crossRe[(size_t) i] = xr;
            c.crossIm[(size_t) i] = xi;
            c.seeded[(size_t) i]  = 1;
        }
    }

    // ---- 2. Contiguous valid range (skip DC) --------------------------
    auto valid = [&c] (int i) -> bool
    {
        return c.hasSignal[(size_t) i] != 0 && c.seeded[(size_t) i] != 0;
    };

    int lo = -1, hi = -1;
    for (int i = 1; i < n; ++i)
        if (valid (i)) { if (lo < 0) lo = i; hi = i; }

    // ---- Warm-up: suppress the FFT-window-fill transient --------------
    // The first frames after the signal appears have the analysis window
    // straddling silence, so the cross-spectrum phase is garbage and the
    // unwrap random-walks across the whole +/-180 range. Hold output back
    // until the window is fully signal. Re-armed only when the signal is
    // lost entirely, so a momentary single-bin dropout does not retrigger.
    const bool active = (lo >= 0);
    if (active && ! c.wasActive)
        c.settleCountdown = kSettleFrames;
    c.wasActive = active;

    if (! active || c.settleCountdown > 0)
    {
        if (c.settleCountdown > 0) --c.settleCountdown;
        std::fill (c.phaseDeg    .begin(), c.phaseDeg    .end(), kNoMeasurement);
        std::fill (c.groupDelayMs.begin(), c.groupDelayMs.end(), kNoMeasurement);
        return;
    }

    // ---- 3. Unwrap phase across [lo, hi] ------------------------------
    float prevWrapped = std::atan2 (c.crossIm[(size_t) lo], c.crossRe[(size_t) lo]);
    float acc = prevWrapped;
    c.unwrapped[(size_t) lo] = acc;
    for (int i = lo + 1; i <= hi; ++i)
    {
        const float ph = std::atan2 (c.crossIm[(size_t) i], c.crossRe[(size_t) i]);
        float d = ph - prevWrapped;
        while (d >  kPi) d -= kTwoPi;
        while (d < -kPi) d += kTwoPi;
        acc += d;
        c.unwrapped[(size_t) i] = acc;
        prevWrapped = ph;
    }

    // ---- 4. Group delay = -d(phase)/d(omega), centred difference ------
    std::fill (c.groupDelayMs.begin(), c.groupDelayMs.end(), kNoMeasurement);
    for (int i = lo; i <= hi; ++i)
    {
        if (! valid (i)) continue;
        const int a = std::max (lo, i - kGroupDelaySpan);
        const int b = std::min (hi, i + kGroupDelaySpan);
        if (b <= a) continue;

        const float dPhase = c.unwrapped[(size_t) b] - c.unwrapped[(size_t) a];
        const float dOmega = kTwoPi * binFreqScale * (float) (b - a);
        if (dOmega <= 0.0f) continue;

        c.groupDelayMs[(size_t) i] = (-dPhase / dOmega) * 1000.0f;
    }

    // ---- 5. Detrend for the Phase view --------------------------------
    // Least-squares fit unwrapped[i] ~ slope*i + intercept over the valid
    // bins; subtracting it removes the bulk linear-phase (latency) term,
    // so the Phase view shows the device's actual phase distortion
    // independent of how much delay sits in the signal path.
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int    m  = 0;
    for (int i = lo; i <= hi; ++i)
    {
        if (! valid (i)) continue;
        const double x = (double) i;
        const double y = (double) c.unwrapped[(size_t) i];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        ++m;
    }

    double slope = 0.0, intercept = (m > 0 ? sy / (double) m : 0.0);
    const double denom = (double) m * sxx - sx * sx;
    if (m >= 2 && std::abs (denom) > 1.0e-9)
    {
        slope     = ((double) m * sxy - sx * sy) / denom;
        intercept = (sy - slope * sx) / (double) m;
    }

    std::fill (c.phaseDeg.begin(), c.phaseDeg.end(), kNoMeasurement);
    for (int i = lo; i <= hi; ++i)
    {
        if (! valid (i)) continue;
        float d = (float) ((double) c.unwrapped[(size_t) i] - (slope * (double) i + intercept));
        while (d >  kPi) d -= kTwoPi;
        while (d < -kPi) d += kTwoPi;
        c.phaseDeg[(size_t) i] = d * (180.0f / kPi);
    }
}
