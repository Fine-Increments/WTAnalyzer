/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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

    // Per-block RMS readouts for the editor's routing-check meters.
    // dB scale; -100.0f represents silence / inactive bus.
    // preBusActive tracks whether the sidechain (pre-effect) input is wired;
    // the main (post-effect) input is always present when the plugin is running.
    std::atomic<float> preEffectLevelDb  { -100.0f };
    std::atomic<float> postEffectLevelDb { -100.0f };
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

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void runLatencyMeasurement();

    // Delay line applied to the pre-effect (sidechain) bus to align it with
    // the post-effect (main) signal. Integer-sample resolution; sub-sample
    // alignment is a future concern.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>
        preDelayLine { kMaxDelaySamples };

    // FFT used for cross-correlation. Real-only forward + inverse transforms
    // are non-allocating after construction.
    juce::dsp::FFT xcorrFft { kXcorrFftOrder };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessor)
};
