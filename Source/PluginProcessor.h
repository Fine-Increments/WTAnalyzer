/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Analyses/FrequencyResponse.h"
#include "Analyses/THDMeasurement.h"
#include "Analyses/AliasingDetection.h"
#include "Analyses/IMDMeasurement.h"

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
    std::atomic<float> preEffectLevelDb  { -100.0f };
    std::atomic<float> postEffectLevelDb { -100.0f };
    std::atomic<float> preEffectPeakDb   { -100.0f };
    std::atomic<float> postEffectPeakDb  { -100.0f };
    std::atomic<bool>  preBusActive      { false };

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
    static constexpr int kSpectrumFftOrder = 12;                       // 4096-point
    static constexpr int kSpectrumFftSize  = 1 << kSpectrumFftOrder;
    static constexpr int kSpectrumBins     = kSpectrumFftSize / 2;     // 2048 unique bins
    static constexpr int kSpectrumHopSize  = kSpectrumFftSize / 4;     // 75% overlap

    // Magnitude in dB per bin, written by audio thread and read directly by
    // the editor. Direct reads can momentarily tear across bins; visually
    // imperceptible at 30 Hz repaint and not worth the cost of double buffering.
    std::array<float, kSpectrumBins> preSpectrumDb  {};
    std::array<float, kSpectrumBins> postSpectrumDb {};

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
        IMDMeasurement    = 4
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
    // of channel 0 of the delay-compensated pre and the post bus.
    std::array<float, kSpectrumFftSize> spectrumPreBuffer  {};
    std::array<float, kSpectrumFftSize> spectrumPostBuffer {};

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
