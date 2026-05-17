/*
  ==============================================================================

    SweepGrid.h
    Records a per-frame ROW of values across the sweep dimension, for the
    Parameter Sweep mode's heatmap view.

    Where SweepCurve stores one scalar per channel per sweep-position
    bucket (the 1D line plot), SweepGrid stores a short row: the active
    metric's full distribution at that bucket - THD's per-harmonic
    differential dB, or IMD's per-product differential dB. Stacked across
    the 0..1 sweep dimension this becomes a heatmap (column = harmonic /
    product, row = sweep position, colour = level).

    Both channels are kept; the display picks one (a heatmap cannot
    overlay L and R). Fixed sizes, so no prepare() - just reset().

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>

class SweepGrid
{
public:
    // X-axis resolution along the 0..1 sweep dimension. Matches
    // SweepCurve so both Parameter Sweep views bucket identically.
    static constexpr int kPositionBuckets = 128;

    // Widest row: THD exposes up to 16 harmonics, IMD 12 products.
    static constexpr int kMaxCols = 16;

    // Sentinel for an empty (bucket, column) cell.
    static constexpr float kNoData = -1.0e9f;

    void reset();

    // Audio-thread entry. position in [0, 1]; rowL / rowR hold numCols
    // values each (cells may themselves be kNoData for an invalid slot).
    void captureFrame (float position, const float* rowL, const float* rowR, int numCols);

    bool hasAnyData()    const noexcept { return filledBuckets.load (std::memory_order_relaxed) > 0; }
    int  getNumBuckets() const noexcept { return kPositionBuckets; }

    // Value at (bucket, column) for channel 0 (L) or 1 (R); kNoData if
    // nothing has been captured at that bucket yet.
    float getValueL (int bucket, int col) const noexcept;
    float getValueR (int bucket, int col) const noexcept;

private:
    std::array<float, kPositionBuckets * kMaxCols> gridL {};
    std::array<float, kPositionBuckets * kMaxCols> gridR {};

    std::array<std::atomic<bool>, kPositionBuckets> bucketHasData {};
    std::atomic<int> filledBuckets { 0 };
};
