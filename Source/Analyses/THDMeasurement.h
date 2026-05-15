/*
  ==============================================================================

    THDMeasurement.h
    Total Harmonic Distortion analysis.

    Three concurrent views are produced from each spectrum frame:

      - Pre (classical):  Pre's own harmonics, in dB relative to pre's
                          fundamental. Useful for verifying that the test
                          signal really is a clean sine.
      - Post (classical): Post's own harmonics, in dB relative to post's
                          fundamental. The classical "what does the device
                          produce" view.
      - Diff:             Added harmonic energy, per harmonic, in dB relative
                          to pre's fundamental. Computed from
                              added_amp_n = sqrt(max(0, post_n^2 - pre_n^2))
                          so it isolates energy the device added on top of
                          whatever was already present in the input.

    The top-level THD% / THD dB readout is always computed from the Diff
    view: total added harmonic energy across h2..hN divided by pre's
    fundamental. The classical views exist only as visualisations of each
    signal's harmonic content - they do not produce a THD percentage.

    Caller drives this from the spectrum-FFT path - no new DSP stream, just a
    derived measurement on the pre/post dB arrays we already produce.

  ==============================================================================
*/

#pragma once

#include <array>

class THDMeasurement
{
public:
    static constexpr int kMaxHarmonics = 16;

    // Pre's fundamental bin must read at least this loud for the measurement
    // to be considered valid. Below this we declare "no valid input" rather
    // than report garbage. Set well above the FFT noise floor (~-110 dB FS)
    // so the panel cleanly transitions to "no signal" when the test tone
    // ends - otherwise harmonic ratios approach 0 dB as their absolute
    // levels and the fundamental converge on the noise floor.
    static constexpr float kFundamentalMinDb = -30.0f;

    // Sentinel for harmonic dB slots where no valid measurement exists
    // (harmonic above Nyquist, or analysis not yet primed).
    static constexpr float kNoMeasurementDb = -200.0f;

    // Selects which set of bars to expose via the getHarmonic* accessors.
    enum class Source { Diff, Pre, Post };

    // Channel selector for the stereo getters below.
    enum class Channel { L, R };

    void prepare (int numSpectrumBins, float binFrequencyScale);
    void reset();

    // Stereo update: each channel runs the THD algorithm independently
    // on its own pre / post spectrum data. Mono callers can pass the
    // same L and R arrays. Real-time-safe.
    void update (const float* preDbL, const float* postDbL,
                 const float* preDbR, const float* postDbR);

    // Validity, fundamental, and totals - per channel. The unsuffixed
    // forms return the L channel for backwards compatibility with
    // pre-stereo callers.
    bool  isValid              (Channel ch = Channel::L) const noexcept { return get(ch).valid; }
    float getFundamentalHz     (Channel ch = Channel::L) const noexcept { return get(ch).fundamentalHz; }
    int   getFundamentalBin    (Channel ch = Channel::L) const noexcept { return get(ch).fundamentalBin; }
    float getTotalThdPercent   (Channel ch = Channel::L) const noexcept { return get(ch).thdPercent; }
    float getTotalThdDb        (Channel ch = Channel::L) const noexcept { return get(ch).thdDb; }
    int   getNumValidHarmonics (Channel ch = Channel::L) const noexcept { return get(ch).numValidHarmonics; }
    float getPreFundamentalDb  (Channel ch = Channel::L) const noexcept { return get(ch).preFundamentalDb; }
    float getPostFundamentalDb (Channel ch = Channel::L) const noexcept { return get(ch).postFundamentalDb; }

    // harmonic = 1 is the fundamental, 2 is the 2nd harmonic, etc.
    // Returns kNoMeasurementDb for harmonics out of range or above
    // Nyquist. getHarmonicDb is meaningful for Pre / Post (absolute
    // spectrum magnitudes); for Diff it returns the same value as the
    // ratio since the differential bar is a ratio by construction.
    float getHarmonicDb      (Source source, int harmonic, Channel ch = Channel::L) const noexcept;
    float getHarmonicRatioDb (Source source, int harmonic, Channel ch = Channel::L) const noexcept;

private:
    struct SourceData
    {
        std::array<float, kMaxHarmonics> harmonicDb      {};
        std::array<float, kMaxHarmonics> harmonicRatioDb {};
    };

    struct ChannelState
    {
        bool  valid              = false;
        int   fundamentalBin     = 0;
        float fundamentalHz      = 0.0f;
        float thdPercent         = 0.0f;
        float thdDb              = kNoMeasurementDb;
        float preFundamentalDb   = kNoMeasurementDb;
        float postFundamentalDb  = kNoMeasurementDb;
        int   numValidHarmonics  = 0;

        SourceData preData;
        SourceData postData;
        SourceData diffData;
    };

    int   numBins      = 0;
    float binFreqScale = 0.0f;

    ChannelState chL;
    ChannelState chR;

    const ChannelState& get (Channel ch) const noexcept
    {
        return ch == Channel::L ? chL : chR;
    }

    static void resetSourceData (SourceData& d);
    static void resetChannel    (ChannelState& ch);
    void updateChannel (ChannelState& ch, const float* preDb, const float* postDb);
};
