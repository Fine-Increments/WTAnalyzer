/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SidecarReader.h"
#include "Analyses/FrequencyResponse.h"
#include "Analyses/THDMeasurement.h"
#include "Analyses/AliasingDetection.h"
#include "Analyses/IMDMeasurement.h"
#include "Analyses/ImpulseResponse.h"
#include "Analyses/FarinaIR.h"
#include "Analyses/SweepCapture.h"
#include "Analyses/StereoAnalysis.h"

//==============================================================================
/**
*/
class WTAnalyzerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    WTAnalyzerAudioProcessor();
    ~WTAnalyzerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Per-block level readouts for the editor's meters. dB scale; -100.0f
    // represents silence / inactive bus. Both peak (max abs sample) and RMS
    // are computed every block; the editor reads whichever the user selects
    // via the meterUseRms APVTS parameter.
    //
    // preBusActive tracks whether the sidechain (pre-effect) input is wired;
    // the main (post-effect) input is always present when the plugin is running.
    // _R variants are right-channel levels; meters render L on the top half
    // and R on the bottom half of each existing bar. When a bus is mono, R
    // is sourced from channel 0 so both halves show the same data.
    std::atomic<float> preEffectLevelDb    { -100.0f };
    std::atomic<float> preEffectLevelDb_R  { -100.0f };
    std::atomic<float> postEffectLevelDb   { -100.0f };
    std::atomic<float> postEffectLevelDb_R { -100.0f };
    std::atomic<float> preEffectPeakDb     { -100.0f };
    std::atomic<float> preEffectPeakDb_R   { -100.0f };
    std::atomic<float> postEffectPeakDb    { -100.0f };
    std::atomic<float> postEffectPeakDb_R  { -100.0f };
    std::atomic<bool>  preBusActive        { false };

    // Upper bound on pre-effect delay (samples). At 48 kHz this is ~340 ms,
    // generous for typical effect latencies including lookahead limiters and
    // linear-phase EQ.
    static constexpr int kMaxDelaySamples = 16384;

    // Public parameter tree. Editor binds controls to it via attachments;
    // host serializes it automatically via getStateInformation.
    juce::AudioProcessorValueTreeState apvts;

    // Cross-correlation auto-measurement: signal length captured and FFT size.
    // The signal length must exceed the largest delay we want to measure or
    // there is no temporal overlap between captured pre and post, and the
    // correlation peak defaults to 0. 16384-sample window padded to 32768
    // covers lags up to kMaxDelaySamples (~340 ms at 48 kHz).
    static constexpr int kXcorrFftOrder  = 15;
    static constexpr int kXcorrFftSize   = 1 << kXcorrFftOrder;     // 32768
    static constexpr int kXcorrSignalLen = kXcorrFftSize / 2;       // 16384
    static constexpr int kXcorrMaxLag    = kXcorrSignalLen;

    // Editor sets to true to request a measurement; audio thread clears it.
    std::atomic<bool> measureLatencyRequested  { false };
    // Audio thread sets to true after writing a result; editor clears it.
    std::atomic<bool> measureLatencyCompleted  { false };
    // Last measured pre-vs-post offset in samples.
    std::atomic<int>  lastMeasuredLatencyOffset { 0 };

    // Spectrum overlay: continuously updated FFT magnitudes of the
    // delay-compensated pre and the post-effect signals.
    //
    // Order 13 = 8192-point FFT, giving ~5.86 Hz/bin at 48 kHz (~2.93
    // Hz at 96 kHz). Better-than-Pro-Q-grade frequency resolution.
    // Net CPU cost is roughly the same as the previous order-12 setup
    // because each FFT runs ~1.65x slower but at half the rate (the
    // hop size also doubled to preserve the 75% overlap). The longer
    // window (170 ms at 48 kHz) provides additional inherent
    // smoothing that complements the EMA smoothing in
    // FrequencyResponse.
    static constexpr int kSpectrumFftOrder = 13;                       // 8192-point
    static constexpr int kSpectrumFftSize  = 1 << kSpectrumFftOrder;
    static constexpr int kSpectrumBins     = kSpectrumFftSize / 2;     // 4096 unique bins
    static constexpr int kSpectrumHopSize  = kSpectrumFftSize / 4;     // 75% overlap

    // Magnitude in dB per bin, written by audio thread and read directly by
    // the editor. Direct reads can momentarily tear across bins; visually
    // imperceptible at 30 Hz repaint and not worth the cost of double buffering.
    //
    // _R variants are the right-channel counterparts; existing analyses that
    // only need a mono read continue using the bare names (which are
    // semantically the L channel). Phase 1 stereo support feeds these
    // per-channel; downstream analyses opt into R as they get stereo-aware.
    std::array<float, kSpectrumBins> preSpectrumDb    {};
    std::array<float, kSpectrumBins> preSpectrumDb_R  {};
    std::array<float, kSpectrumBins> postSpectrumDb   {};
    std::array<float, kSpectrumBins> postSpectrumDb_R {};

    // Increments each time a new spectrum frame is written. Available for the
    // editor if it ever wants to repaint only on fresh data.
    std::atomic<int> spectrumFrameCount { 0 };

    // Sample rate captured during prepareToPlay so the editor can convert
    // FFT bin indices to frequencies for the X-axis.
    std::atomic<float> currentSampleRate { 48000.0f };

    // Active analysis (FrequencyResponse, etc.). Display reads via this enum
    // to decide which mode-specific overlays to draw. Stays in sync with the
    // APVTS `activeAnalysis` parameter via updates in processBlock. Indexes
    // here must match the StringArray order in createParameterLayout.
    enum class AnalysisMode
    {
        GenericOverlay    = 0,
        FrequencyResponse = 1,
        THDMeasurement    = 2,
        AliasingDetection = 3,
        IMDMeasurement    = 4,
        DirectImpulseIR   = 5,
        FarinaIR          = 6,
        StereoImage       = 7
    };

    // First analysis: derived from the existing pre/post spectrum FFT.
    // See Analyses/FrequencyResponse.h for the algorithm.
    FrequencyResponse frequencyResponse;

    // Second analysis: harmonic distortion of a sine-tone input. Like
    // FrequencyResponse, consumes the existing spectrum FFT output and
    // produces a derived measurement - no new DSP stream.
    THDMeasurement thdMeasurement;

    // Third analysis: per-bin alias residue across a sweep. Consumes the
    // existing pre/post spectrum FFT output; peak-holds off-grid added
    // energy so a sweep accumulates the full aliasing picture.
    AliasingDetection aliasingDetection;

    // Fourth analysis: intermodulation distortion from a two-tone input.
    // Same DSP-source pattern as THDMeasurement: consumes the existing
    // spectrum FFT output and produces per-product readouts.
    IMDMeasurement imdMeasurement;

    // Fifth analysis: time-domain impulse response via direct impulse
    // capture. Operates on raw pre/post audio (not the spectrum FFT) -
    // the first time-domain analysis in the suite. Trigger-detected,
    // accumulating average.
    ImpulseResponse impulseResponse;

    // Sixth analysis: time-domain impulse response via Farina log-sweep
    // deconvolution. Same output (IR plot) as DirectImpulseIR but
    // acquired from a log sine sweep, which WTSynth can deliver
    // cleanly where discrete impulses can't.
    FarinaIR farinaIR;

    // Seventh analysis: per-frequency stereo divergence (R - L level
    // difference). Consumes the existing spectrum FFT output; produces
    // pre / post / device-added divergence arrays for the Stereo Image
    // mode's bipolar centred-on-zero display.
    StereoAnalysis stereoAnalysis;

    // 2D capture across a sweep axis. Cross-cutting capability, not a
    // mode of its own: when active (`sweepCaptureActive` APVTS bool),
    // analyses that participate (FR for v1) record their per-frame
    // output bucketed by the current `sweepPosition` APVTS value
    // (DAW-automated, typically alongside WTSynth's WT Pos).
    SweepCapture sweepCapture;

    // Sidecar JSON reader: parameter-source for analyses that need to
    // know exactly what test signal the script generated (PLANNING.md
    // section 2.5). Lives on the message thread; analyses snapshot the
    // context at controlled moments rather than polling it per sample.
    SidecarReader sidecar;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void runLatencyMeasurement();
    int  computeCrossCorrelationFromScratch();

    // Delay line applied to the pre-effect (sidechain) bus to align it with
    // the post-effect (main) signal. Integer-sample resolution; sub-sample
    // alignment is a future concern.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>
        preDelayLine { kMaxDelaySamples };

    // FFT used for cross-correlation. Real-only forward + inverse transforms
    // are non-allocating after construction.
    juce::dsp::FFT xcorrFft { kXcorrFftOrder };

    // Hann window applied to both signals before FFT. Tapers the edges so the
    // captured signal mass is centered, sharpens the correlation peak, and
    // suppresses the secondary peaks that otherwise vote for random lags when
    // transient-rich signals are captured.
    juce::dsp::WindowingFunction<float> xcorrWindow {
        (size_t) kXcorrSignalLen,
        juce::dsp::WindowingFunction<float>::hann
    };

    // Continuously filled ring buffers (channel 0 only) of the raw pre and
    // post signals - filled before manual delay is applied so the measurement
    // sees the absolute effect-induced offset, not the residual.
    std::array<float, kXcorrSignalLen> xcorrPreRing  {};
    std::array<float, kXcorrSignalLen> xcorrPostRing {};
    int xcorrRingPos = 0;

    // Scratch buffers for FFT-based cross-correlation. Sized 2*FFT to hold the
    // full conjugate-symmetric complex output produced by JUCE's real FFT.
    std::array<float, 2 * kXcorrFftSize> xcorrScratchA {};
    std::array<float, 2 * kXcorrFftSize> xcorrScratchB {};

    void runSpectrumFft();

    juce::dsp::FFT spectrumFft { kSpectrumFftOrder };
    juce::dsp::WindowingFunction<float> spectrumWindow {
        (size_t) kSpectrumFftSize,
        juce::dsp::WindowingFunction<float>::hann
    };

    // Per-bus circular buffers holding the most recent kSpectrumFftSize samples
    // of each channel of the delay-compensated pre and the post bus. Mono
    // sources fall back to channel 0 for both L and R, so a stereo plugin
    // fed a mono signal still produces visible-on-screen overlap traces.
    std::array<float, kSpectrumFftSize> spectrumPreBuffer    {};
    std::array<float, kSpectrumFftSize> spectrumPreBuffer_R  {};
    std::array<float, kSpectrumFftSize> spectrumPostBuffer   {};
    std::array<float, kSpectrumFftSize> spectrumPostBuffer_R {};

    // Working buffer for the FFT: 2 * N floats because JUCE's real FFT writes
    // its output as interleaved complex pairs over the full buffer.
    std::array<float, 2 * kSpectrumFftSize> spectrumScratch {};

    int spectrumWritePos      = 0;
    int samplesSinceLastSpectrumFft = 0;

    // Tracks the activeAnalysis parameter value seen on the last FFT so we
    // can reset the active analysis state on mode change. Audio-thread only.
    int lastActiveAnalysisIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessor)
};
