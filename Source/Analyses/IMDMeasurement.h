/*
  ==============================================================================

    IMDMeasurement.h
    Intermodulation Distortion analysis. Companion to THDMeasurement:
    THD characterises nonlinearity with a single tone (harmonics at
    integer multiples of f0); IMD characterises it with two tones (sum/
    difference products at m*f1 +/- n*f2). Most subjectively offensive
    distortion shows up in IMD rather than THD because real audio is
    multi-tonal.

    Algorithm per frame:
      1. Locate pre's two loudest distinct peaks (f1 = louder, f2 = next).
      2. For each (m, n) pair up to order |m|+|n| <= kMaxOrder with both
         m and n non-zero, compute the product bin at |m*f1_bin +/- n*f2_bin|.
      3. Read pre and post values at each product bin; for the differential
         compute added energy = max(0, post_amp^2 - pre_amp^2).
      4. Total IMD% is the RMS of all differential products divided by
         f1's pre amplitude.

    Three concurrent views are produced (matching THD's structure):
      - Pre  (classical):  pre values at product bins, dB relative to f1.
                           Useful for verifying the test signal is clean.
      - Post (classical):  post values at product bins, dB relative to f1.
                           The classical "what does the device produce".
      - Diff:              Added energy at each product, dB relative to f1.
                           Strictly what the device introduced.

    The top-level IMD% / IMD dB readout is always the differential value,
    same as THDMeasurement.

    Consumes the existing pre/post spectrum FFT - no new DSP stream.

  ==============================================================================
*/

#pragma once

#include <array>

class IMDMeasurement
{
public:
    // Product list covers orders 2 through 4 (sum and difference for each
    // |m|, |n| pair). Order 2: 2 products. Order 3: 4. Order 4: 6.
    static constexpr int kMaxOrder    = 4;
    static constexpr int kNumProducts = 12;

    // Both fundamentals must read at least this loud for the frame to
    // produce a valid measurement.
    static constexpr float kFundamentalMinDb = -30.0f;

    // Sentinel for slots where no valid measurement exists.
    static constexpr float kNoMeasurementDb  = -200.0f;

    // f1 vs f2 separation in Hz. Below this the two peaks are too close
    // to be reliably distinguished and we fall back to "invalid".
    static constexpr float kMinSeparationHz  = 100.0f;

    // Selects which set of bars to expose via the getProduct* accessors.
    enum class Source { Diff, Pre, Post };

    void prepare (int numSpectrumBins, float binFrequencyScale);
    void reset();
    void update (const float* preDb, const float* postDb);

    bool  isValid() const noexcept { return valid; }

    // Fundamental frequencies. f1 is always the louder of the two.
    float getF1Hz() const noexcept { return f1Hz; }
    float getF2Hz() const noexcept { return f2Hz; }

    // Pre / Post absolute dB at each fundamental, for the subline readout.
    float getF1Db (Source) const noexcept;
    float getF2Db (Source) const noexcept;

    // Total IMD% across all products. Always the differential value.
    float getTotalImdPercent() const noexcept { return imdPercent; }
    float getTotalImdDb()      const noexcept { return imdDb; }

    int   getNumProducts() const noexcept { return kNumProducts; }

    // Per-product accessors. Index range: 0..kNumProducts-1.
    int   getProductOrder    (int idx) const noexcept;
    float getProductHz       (int idx) const noexcept;
    float getProductDb       (Source, int idx) const noexcept;   // absolute (FS)
    float getProductRatioDb  (Source, int idx) const noexcept;   // relative to f1
    const char* getProductLabel (int idx) const noexcept;        // "f1+f2", "2f1-f2", ...

private:
    struct ProductDef
    {
        int  m;          // unsigned coefficient of f1
        int  n;          // unsigned coefficient of f2
        int  sign;       // +1 for sum (mf1 + nf2), -1 for difference (mf1 - nf2)
        const char* label;
    };

    static constexpr std::array<ProductDef, kNumProducts> kProducts {{
        // Order 2
        { 1, 1, +1, "f1+f2"   }, { 1, 1, -1, "f1-f2"   },
        // Order 3
        { 2, 1, +1, "2f1+f2"  }, { 2, 1, -1, "2f1-f2"  },
        { 1, 2, +1, "f1+2f2"  }, { 1, 2, -1, "f1-2f2"  },
        // Order 4
        { 3, 1, +1, "3f1+f2"  }, { 3, 1, -1, "3f1-f2"  },
        { 2, 2, +1, "2f1+2f2" }, { 2, 2, -1, "2f1-2f2" },
        { 1, 3, +1, "f1+3f2"  }, { 1, 3, -1, "f1-3f2"  }
    }};

    struct SourceData
    {
        std::array<float, kNumProducts> productDb      {};
        std::array<float, kNumProducts> productRatioDb {};
    };

    SourceData preData;
    SourceData postData;
    SourceData diffData;

    int   numBins      = 0;
    float binFreqScale = 0.0f;

    bool  valid        = false;
    int   f1Bin        = 0;
    int   f2Bin        = 0;
    float f1Hz         = 0.0f;
    float f2Hz         = 0.0f;
    float preF1Db      = kNoMeasurementDb;
    float preF2Db      = kNoMeasurementDb;
    float postF1Db     = kNoMeasurementDb;
    float postF2Db     = kNoMeasurementDb;

    float imdPercent   = 0.0f;
    float imdDb        = kNoMeasurementDb;

    std::array<int,   kNumProducts> productBin {};
    std::array<float, kNumProducts> productHz  {};

    static void resetSourceData (SourceData& d);
};
