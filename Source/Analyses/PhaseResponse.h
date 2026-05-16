/*
  ==============================================================================

    PhaseResponse.h
    Per-frequency phase response and group delay of the device under test.

    Companion to FrequencyResponse: where FR gives magnitude, this gives
    the phase side of the transfer function. For each channel the device's
    effect at a bin is the cross-spectrum post * conj(pre); its angle is
    the pre-vs-post phase difference. Forming the difference this way (one
    complex product) avoids wrapping two separate angles.

    Two outputs per channel:

      - Phase (degrees): the device's phase response with its best-fit
        linear-phase component removed. Bulk latency - integer or
        fractional - is a straight line in phase versus frequency, and
        uninformative; subtracting the least-squares line leaves the
        device's actual phase distortion, which then sits within a single
        +/-180 degree wrap for most devices. This is why the Phase view
        does not need the user to null latency first.

      - Group delay (ms): the negated slope of the UNWRAPPED phase versus
        frequency. Not detrended - group delay should show true delay,
        including any flat component.

    The cross-spectrum is EMA-smoothed (re/im) before the angle is taken;
    averaging the complex form is correct, averaging wrapped angles is not.
    Bins where the pre signal has no energy carry the kNoMeasurement
    sentinel. Consumes the spectrum-FFT complex output - no new DSP stream.

  ==============================================================================
*/

#pragma once

#include <vector>

class PhaseResponse
{
public:
    // A bin "has signal" once pre rises above kSignalEnterDb and keeps it
    // until pre falls below kSignalLeaveDb - 20 dB hysteresis against
    // bin-by-bin flicker, matching FrequencyResponse.
    static constexpr float kSignalEnterDb = -80.0f;
    static constexpr float kSignalLeaveDb = -100.0f;

    // Sentinel for bins with no valid measurement. Well outside the range
    // of any real phase (degrees) or group delay (ms) value.
    static constexpr float kNoMeasurement = -1.0e9f;

    // Per-bin EMA factor for the cross-spectrum. Matches the steady-state
    // smoothing used elsewhere in the suite.
    static constexpr float kSmoothingAlpha = 0.15f;

    enum class Channel { L, R };

    void prepare (int numBins, float binFrequencyScaleHz);
    void reset();

    // preDb: normalised magnitude-dB arrays (validity gating).
    // pre/postComplex: interleaved [re, im] pairs, 2*numBins floats.
    // Real-time-safe.
    void update (const float* preDbL,       const float* preDbR,
                 const float* preComplexL,  const float* preComplexR,
                 const float* postComplexL, const float* postComplexR);

    // Detrended wrapped phase, degrees (-180..+180), per bin. Phase view.
    const std::vector<float>& getPhaseDegrees (Channel ch) const noexcept
    {
        return ch == Channel::L ? chL.phaseDeg : chR.phaseDeg;
    }

    // Group delay, milliseconds, per bin. Group Delay view.
    const std::vector<float>& getGroupDelayMs (Channel ch) const noexcept
    {
        return ch == Channel::L ? chL.groupDelayMs : chR.groupDelayMs;
    }

    int getNumBins() const noexcept { return numBins; }

private:
    struct ChannelState
    {
        std::vector<float> crossRe;        // EMA-smoothed cross-spectrum
        std::vector<float> crossIm;
        std::vector<char>  hasSignal;      // pre-signal hysteresis state
        std::vector<char>  seeded;         // EMA primed at this bin
        std::vector<float> unwrapped;      // scratch: unwrapped phase (rad)
        std::vector<float> phaseDeg;       // output: detrended wrapped deg
        std::vector<float> groupDelayMs;   // output: group delay (ms)

        // Warm-up state: output is suppressed for a few frames after the
        // signal first appears, while the FFT window is still filling.
        bool wasActive      = false;
        int  settleCountdown = 0;
    };

    static void prepareChannel (ChannelState& c, int numBins);
    static void resetChannel   (ChannelState& c);
    void updateChannel (ChannelState& c, const float* preDb,
                        const float* preCx, const float* postCx);

    int   numBins      = 0;
    float binFreqScale = 0.0f;

    ChannelState chL;
    ChannelState chR;
};
