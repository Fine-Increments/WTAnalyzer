/*
  ==============================================================================

    IMDMeasurement.cpp

  ==============================================================================
*/

#include "IMDMeasurement.h"

#include <algorithm>
#include <cmath>

constexpr std::array<IMDMeasurement::ProductDef, IMDMeasurement::kNumProducts>
    IMDMeasurement::kProducts;

void IMDMeasurement::resetSourceData (SourceData& d)
{
    std::fill (d.productDb     .begin(), d.productDb     .end(), kNoMeasurementDb);
    std::fill (d.productRatioDb.begin(), d.productRatioDb.end(), kNoMeasurementDb);
}

void IMDMeasurement::resetChannel (ChannelState& ch)
{
    ch.valid      = false;
    ch.f1Bin      = 0;
    ch.f2Bin      = 0;
    ch.f1Hz       = 0.0f;
    ch.f2Hz       = 0.0f;
    ch.preF1Db    = kNoMeasurementDb;
    ch.preF2Db    = kNoMeasurementDb;
    ch.postF1Db   = kNoMeasurementDb;
    ch.postF2Db   = kNoMeasurementDb;
    ch.imdPercent = 0.0f;
    ch.imdDb      = kNoMeasurementDb;

    resetSourceData (ch.preData);
    resetSourceData (ch.postData);
    resetSourceData (ch.diffData);

    ch.productBin.fill (0);
    ch.productHz .fill (0.0f);
}

void IMDMeasurement::prepare (int numSpectrumBins, float binFrequencyScale)
{
    numBins      = numSpectrumBins;
    binFreqScale = binFrequencyScale;
    reset();
}

void IMDMeasurement::reset()
{
    resetChannel (chL);
    resetChannel (chR);
}

void IMDMeasurement::update (const float* preDbL, const float* postDbL,
                             const float* preDbR, const float* postDbR)
{
    updateChannel (chL, preDbL, postDbL);
    updateChannel (chR, preDbR, postDbR);
}

void IMDMeasurement::updateChannel (ChannelState& ch,
                                    const float* preDb, const float* postDb)
{
    constexpr float kMinUsefulFreq = 20.0f;
    constexpr float kMaxUsefulFreq = 20000.0f;

    // Find the two loudest peaks in pre's audible band. f1 is the louder
    // of the two; f2 must be at least kMinSeparationHz away from f1 to
    // be distinguishable.
    const int   minSepBins = std::max (2, (int) std::ceil (kMinSeparationHz / binFreqScale));

    int   peak1Bin = -1;
    float peak1Db  = -1000.0f;

    for (int b = 1; b < numBins; ++b)
    {
        const float f = (float) b * binFreqScale;
        if (f < kMinUsefulFreq) continue;
        if (f > kMaxUsefulFreq) break;

        if (preDb[b] > peak1Db)
        {
            peak1Db  = preDb[b];
            peak1Bin = b;
        }
    }

    int   peak2Bin = -1;
    float peak2Db  = -1000.0f;

    if (peak1Bin > 0)
    {
        for (int b = 1; b < numBins; ++b)
        {
            const float f = (float) b * binFreqScale;
            if (f < kMinUsefulFreq) continue;
            if (f > kMaxUsefulFreq) break;
            if (std::abs (b - peak1Bin) < minSepBins) continue;

            if (preDb[b] > peak2Db)
            {
                peak2Db  = preDb[b];
                peak2Bin = b;
            }
        }
    }

    if (peak1Bin < 0 || peak2Bin < 0
        || peak1Db < kFundamentalMinDb
        || peak2Db < kFundamentalMinDb)
    {
        ch.valid      = false;
        ch.imdPercent = 0.0f;
        ch.imdDb      = kNoMeasurementDb;
        return;
    }

    // Convention: f1 is the LOWER-frequency tone, f2 is the HIGHER. This is
    // stable as the user sweeps pitch - by-amplitude labelling (textbook
    // f1 = louder) flips whenever scalloping loss makes the tones read
    // slightly unequal and is confusing in practice.
    ch.f1Bin = std::min (peak1Bin, peak2Bin);
    ch.f2Bin = std::max (peak1Bin, peak2Bin);
    ch.f1Hz  = (float) ch.f1Bin * binFreqScale;
    ch.f2Hz  = (float) ch.f2Bin * binFreqScale;

    ch.preF1Db  = preDb [ch.f1Bin];
    ch.preF2Db  = preDb [ch.f2Bin];
    ch.postF1Db = postDb[ch.f1Bin];
    ch.postF2Db = postDb[ch.f2Bin];

    // Resolve every product's bin and frequency. Bins are folded to
    // positive (negative-frequency products mirror onto the real spectrum).
    for (int i = 0; i < kNumProducts; ++i)
    {
        const auto& p = kProducts[(size_t) i];
        const int   signedBin = p.m * ch.f1Bin + p.sign * p.n * ch.f2Bin;
        const int   bin       = std::abs (signedBin);

        ch.productBin[(size_t) i] = bin;
        ch.productHz [(size_t) i] = (float) bin * binFreqScale;
    }

    resetSourceData (ch.preData);
    resetSourceData (ch.postData);
    resetSourceData (ch.diffData);

    // Normalize against the LOUDER of the two fundamentals so the
    // percentage stays meaningful for asymmetric test signals (SMPTE-style
    // 4:1 ratio, etc.). For equal-amplitude tests this picks whichever
    // happens to read marginally higher per frame, which is fine.
    const float preF1Amp  = std::pow (10.0f, ch.preF1Db / 20.0f);
    const float preF2Amp  = std::pow (10.0f, ch.preF2Db / 20.0f);
    const float preRefAmp = std::max (preF1Amp, preF2Amp);

    float diffPower = 0.0f;

    for (int i = 0; i < kNumProducts; ++i)
    {
        const int bin = ch.productBin[(size_t) i];
        if (bin <= 0 || bin >= numBins)
            continue;   // product folds out of range; skip

        const float preVal  = preDb [bin];
        const float postVal = postDb[bin];

        ch.preData .productDb[(size_t) i] = preVal;
        ch.postData.productDb[(size_t) i] = postVal;

        // Differential added-energy per product (same math as THD).
        const float preAmp  = std::pow (10.0f, preVal  / 20.0f);
        const float postAmp = std::pow (10.0f, postVal / 20.0f);
        const float addedPower = std::max (0.0f, postAmp * postAmp - preAmp * preAmp);
        const float addedAmp   = std::sqrt (addedPower);

        diffPower += addedPower;

        if (preRefAmp > 1.0e-10f && addedAmp > 1.0e-12f)
        {
            const float ratio = addedAmp / preRefAmp;
            ch.diffData.productRatioDb[(size_t) i] = 20.0f * std::log10 (ratio);
            ch.diffData.productDb     [(size_t) i] = ch.diffData.productRatioDb[(size_t) i];
        }
        else
        {
            ch.diffData.productRatioDb[(size_t) i] = kNoMeasurementDb;
            ch.diffData.productDb     [(size_t) i] = kNoMeasurementDb;
        }

        // Classical ratios relative to the louder fundamental (same
        // reference the diff view uses, so bars are visually comparable
        // across views).
        const float refDb = (preF1Amp >= preF2Amp) ? ch.preF1Db : ch.preF2Db;
        ch.preData .productRatioDb[(size_t) i] = preVal  - refDb;
        ch.postData.productRatioDb[(size_t) i] = postVal - refDb;
    }

    if (preRefAmp > 1.0e-10f)
    {
        const float imdRatio = std::sqrt (diffPower) / preRefAmp;
        ch.imdPercent = imdRatio * 100.0f;
        ch.imdDb      = 20.0f * std::log10 (imdRatio + 1.0e-10f);
    }
    else
    {
        ch.imdPercent = 0.0f;
        ch.imdDb      = kNoMeasurementDb;
    }

    ch.valid = true;
}

float IMDMeasurement::getF1Db (Source s, Channel chSel) const noexcept
{
    const auto& ch = get (chSel);
    switch (s)
    {
        case Source::Pre:  return ch.preF1Db;
        case Source::Post: return ch.postF1Db;
        case Source::Diff: return ch.postF1Db - ch.preF1Db;
    }
    return kNoMeasurementDb;
}

float IMDMeasurement::getF2Db (Source s, Channel chSel) const noexcept
{
    const auto& ch = get (chSel);
    switch (s)
    {
        case Source::Pre:  return ch.preF2Db;
        case Source::Post: return ch.postF2Db;
        case Source::Diff: return ch.postF2Db - ch.preF2Db;
    }
    return kNoMeasurementDb;
}

int IMDMeasurement::getProductOrder (int idx) const noexcept
{
    if (idx < 0 || idx >= kNumProducts) return 0;
    const auto& p = kProducts[(size_t) idx];
    return p.m + p.n;
}

float IMDMeasurement::getProductHz (int idx, Channel chSel) const noexcept
{
    if (idx < 0 || idx >= kNumProducts) return 0.0f;
    return get (chSel).productHz[(size_t) idx];
}

float IMDMeasurement::getProductDb (Source s, int idx, Channel chSel) const noexcept
{
    if (idx < 0 || idx >= kNumProducts) return kNoMeasurementDb;
    const auto& ch = get (chSel);
    switch (s)
    {
        case Source::Pre:  return ch.preData .productDb[(size_t) idx];
        case Source::Post: return ch.postData.productDb[(size_t) idx];
        case Source::Diff: return ch.diffData.productDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}

float IMDMeasurement::getProductRatioDb (Source s, int idx, Channel chSel) const noexcept
{
    if (idx < 0 || idx >= kNumProducts) return kNoMeasurementDb;
    const auto& ch = get (chSel);
    switch (s)
    {
        case Source::Pre:  return ch.preData .productRatioDb[(size_t) idx];
        case Source::Post: return ch.postData.productRatioDb[(size_t) idx];
        case Source::Diff: return ch.diffData.productRatioDb[(size_t) idx];
    }
    return kNoMeasurementDb;
}

const char* IMDMeasurement::getProductLabel (int idx) const noexcept
{
    if (idx < 0 || idx >= kNumProducts) return "";
    return kProducts[(size_t) idx].label;
}
