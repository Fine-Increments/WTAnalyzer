/*
  ==============================================================================

    SweepCurve.h
    Records a single scalar measurement across the sweep dimension to
    produce a 1D X-Y curve - the Plugin Doctor style parameter-sweep plot.

    Where SweepCapture records a whole spectrum row per sweep-position
    bucket (for the 2D heatmap), SweepCurve records just one number per
    channel per bucket: the active mode's headline metric (THD%, IMD%).
    The user automates the `sweepPosition` APVTS parameter from the DAW
    alongside whatever they sweep in the source plugin; WTAnalyzer drops
    the current metric value into the matching bucket while capture is
    armed.

    Each bucket averages every frame that lands in it over the whole
    capture: the headline metrics jitter frame-to-frame, so a per-bucket
    running mean gives a far cleaner, more trustworthy curve than the
    last-frame-wins it would otherwise show.

    L and R are stored separately; the display derives Diff (R - L) if
    it needs it. Fixed bucket count, so no prepare() step - just reset().

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>

class SweepCurve
{
public:
    // X-axis resolution along the 0..1 sweep dimension. Matches
    // SweepCapture::kPositionBuckets so both sweep features bucket the
    // same automation lane identically.
    static constexpr int kPositionBuckets = 128;

    // Sentinel for a (bucket, channel) slot with no captured value, or
    // for a channel whose measurement was invalid that frame.
    static constexpr float kNoData = -1.0e9f;

    void reset();

    // Audio-thread entry. position is expected in [0, 1]; valL / valR
    // are the metric values for the L and R channels, or kNoData for a
    // channel that had no valid measurement this frame. Each call folds
    // the values into the running mean for the matching bucket.
    void captureFrame (float position, float valL, float valR);

    // UI-thread accessors. Reads are racy with captureFrame writes but
    // the worst case is a single-pixel tear at repaint - acceptable for
    // visualisation.
    bool hasAnyData()    const noexcept { return filledBuckets.load (std::memory_order_relaxed) > 0; }
    int  getNumBuckets() const noexcept { return kPositionBuckets; }

    // Per-bucket mean value, or kNoData if nothing valid is there.
    float getValueL (int bucket) const noexcept;
    float getValueR (int bucket) const noexcept;

private:
    // Published per-bucket running means - the values the UI reads.
    std::array<float, kPositionBuckets> curveL {};
    std::array<float, kPositionBuckets> curveR {};

    // Audio-thread-private accumulators behind the means. double sums
    // keep precision across a long capture's many frames.
    std::array<double, kPositionBuckets> sumL {};
    std::array<double, kPositionBuckets> sumR {};
    std::array<int,    kPositionBuckets> countL {};
    std::array<int,    kPositionBuckets> countR {};

    // One flag per bucket so the UI can skip unrendered buckets. Atomic
    // for the audio-thread write / UI-thread read handoff.
    std::array<std::atomic<bool>, kPositionBuckets> bucketHasData {};
    std::atomic<int> filledBuckets { 0 };
};
