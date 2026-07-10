/*
  ==============================================================================

    SweepCapture.h
    Records analysis output across a sweep dimension (drive amount,
    filter cutoff, an expression parameter, ...) to produce 2D plots
    that reveal how a device responds across a parameter axis - a
    differentiating capability of the WTGenerator / WTAnalyzer pairing.

    The user drives this via a single APVTS parameter (`sweepPosition`,
    0..1) that they automate from the DAW alongside whatever parameter
    they are sweeping in WTGenerator. WTAnalyzer captures the current
    analysis output (e.g., the FrequencyResponse trace) at the current
    sweep position, bucketed along the 0..1 axis.

    For v1 this records FrequencyResponse output only - one row of N
    frequency bins per sweep-position bucket. Other analyses can plug
    into the same infrastructure later by calling captureFrame with
    their own data shape (THD harmonics, IMD products, etc.) once the
    display layer learns to render them.

  ==============================================================================
*/

#pragma once

#include <atomic>
#include <vector>

class SweepCapture
{
public:
    // X-axis resolution along the sweep dimension. 128 buckets gives
    // fine resolution when the swept parameter's automation runs across a
    // typical 5-10 second sweep, with the heatmap remaining cheap to render and
    // fitting in well under a megabyte for the 2048-bin case.
    static constexpr int kPositionBuckets = 128;

    // Sentinel for "no data captured at this (position, bin) yet."
    // Display treats this as "draw the floor colour."
    static constexpr float kNoDataDb = -200.0f;

    void prepare (int numFreqBins);
    void reset();

    // Audio-thread entry. Drops the response array into the appropriate
    // sweep-position bucket. position is expected in [0, 1].
    void captureFrame (float position, const float* responseDb, int numBins);

    // UI-thread accessors. Reads are racy with captureFrame writes but
    // the worst case is a single-pixel tear at repaint - acceptable for
    // visualisation.
    bool  hasAnyData()   const noexcept { return filledBuckets.load (std::memory_order_relaxed) > 0; }
    int   getNumBins()   const noexcept { return numBins; }
    int   getNumBuckets() const noexcept { return kPositionBuckets; }

    // Returns the captured value at (positionBucket, bin), or
    // kNoDataDb if nothing has been captured there yet.
    float getValue (int positionBucket, int bin) const noexcept;

private:
    int  numBins = 0;

    // Row-major layout: rows = position buckets, columns = freq bins.
    // Single contiguous buffer for cache-friendly heatmap traversal.
    std::vector<float> data;

    // One-bit-per-bucket valid flag. Atomic so the UI can tell when
    // the audio thread has filled a bucket and skip unrendered ones.
    std::vector<std::atomic<bool>> bucketHasData;
    std::atomic<int> filledBuckets { 0 };
};
