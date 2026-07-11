/*
  ==============================================================================

    DynamicsCurve.h
    Records the device's static input-vs-output transfer curve - the
    plot that reveals compression, expansion, gating and limiting.

    Each processed block contributes one (input level, output level)
    pair per channel: the pre-effect RMS level in dB is the input, the
    post-effect RMS level in dB is the output. The input axis is divided
    into fixed dB bins; every block's output level is folded into the
    running mean of the bin its input level lands in. Drive a slow
    amplitude ramp through the device and the bins fill in across the
    range, tracing the transfer curve.

    Per-bin averaging is what makes the curve trustworthy: block RMS
    jitters frame to frame, so a running mean per bin gives a far
    cleaner shape than last-block-wins would.

    L and R are stored separately; the display draws both. Fixed bin
    count, so no prepare() step - just reset(). Accumulation is
    continuous while the Dynamics mode is active; the display's Clear
    button calls reset().

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>

class DynamicsCurve
{
public:
    // Input-axis resolution and span. The range is generous for
    // normalised audio: -90 dB reaches well into the noise floor a gate
    // would act on, +6 dB covers hot signals short of hard clipping.
    static constexpr int   kNumBins    = 128;
    static constexpr float kMinInputDb = -90.0f;
    static constexpr float kMaxInputDb = 6.0f;

    // Sentinel for a bin with no captured output value.
    static constexpr float kNoData = -1.0e9f;

    // Audio-thread reset. Called from the mode-change path in processBlock and,
    // deferred, from the UI Clear (see requestClear). Do not call directly from
    // the message thread - it would race captureFrame's non-atomic accumulators
    // and could tear a bin count to zero mid-divide (NaN/inf points).
    void reset();

    // UI-thread Clear. Defers reset to the audio thread (consumed at the top of
    // captureFrame) so it never races the accumulators.
    void requestClear() noexcept { clearRequested.store (true, std::memory_order_relaxed); }

    // Audio-thread entry. Per-block pre/post RMS levels in dB. A channel
    // whose input level falls outside [kMinInputDb, kMaxInputDb] is
    // skipped - there is no meaningful transfer point to record for it.
    void captureFrame (float preDbL, float postDbL, float preDbR, float postDbR);

    // UI-thread accessors. Reads race with captureFrame writes; the worst
    // case is a single-pixel tear at repaint - acceptable for a plot.
    bool hasAnyData() const noexcept { return filledBins.load (std::memory_order_relaxed) > 0; }
    int  getNumBins() const noexcept { return kNumBins; }

    // Input level (dB) at the centre of a bin - the curve's X coordinate.
    static float binInputDb (int bin) noexcept;

    // Per-bin mean output level (dB), or kNoData if the bin is empty.
    float getOutputDbL (int bin) const noexcept;
    float getOutputDbR (int bin) const noexcept;

private:
    // Maps an input level in dB to a bin index, or -1 if out of range.
    static int binForInputDb (float inputDb) noexcept;

    // Published per-bin running means - the values the UI reads.
    std::array<float, kNumBins> curveL {};
    std::array<float, kNumBins> curveR {};

    // Audio-thread-private accumulators. double sums keep precision
    // across a long capture's many blocks.
    std::array<double, kNumBins> sumL {};
    std::array<double, kNumBins> sumR {};
    std::array<int,    kNumBins> countL {};
    std::array<int,    kNumBins> countR {};

    // One flag per bin so the UI can skip unrendered bins. Atomic for the
    // audio-thread write / UI-thread read handoff.
    std::array<std::atomic<bool>, kNumBins> binHasData {};
    std::atomic<int>  filledBins     { 0 };
    std::atomic<bool> clearRequested { false };
};
