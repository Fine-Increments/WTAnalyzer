/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
WTAnalyzerAudioProcessor::WTAnalyzerAudioProcessor()
    : AudioProcessor (BusesProperties()
        // Bus 0 is the host-facing main input. With WTAnalyzer placed at the end
        // of an effect chain on the same track, this is the post-effect (wet) signal.
        .withInput  ("Post-Effect", juce::AudioChannelSet::stereo(), true)
        // Bus 1 surfaces as the sidechain bus in VST3/AU. The user routes the
        // dry pre-effect signal in via the host's sidechain UI.
        .withInput  ("Pre-Effect",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",      juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
WTAnalyzerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "preDelaySamples", 1 },
        "Pre-Effect Delay (samples)",
        0, kMaxDelaySamples, 0));

    return layout;
}

WTAnalyzerAudioProcessor::~WTAnalyzerAudioProcessor()
{
}

//==============================================================================
const juce::String WTAnalyzerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool WTAnalyzerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool WTAnalyzerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool WTAnalyzerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double WTAnalyzerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int WTAnalyzerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int WTAnalyzerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void WTAnalyzerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String WTAnalyzerAudioProcessor::getProgramName (int index)
{
    return {};
}

void WTAnalyzerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void WTAnalyzerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;  // pre-effect bus is stereo when active

    preDelayLine.prepare (spec);
    preDelayLine.setMaximumDelayInSamples (kMaxDelaySamples);
    preDelayLine.reset();

    xcorrPreRing.fill (0.0f);
    xcorrPostRing.fill (0.0f);
    xcorrRingPos = 0;
}

void WTAnalyzerAudioProcessor::runLatencyMeasurement()
{
    // Unwrap the ring buffers into the lower half of each scratch (oldest
    // sample first); the upper half stays zero, giving linear (non-circular)
    // cross-correlation.
    std::fill (xcorrScratchA.begin(), xcorrScratchA.end(), 0.0f);
    std::fill (xcorrScratchB.begin(), xcorrScratchB.end(), 0.0f);

    for (int i = 0; i < kXcorrSignalLen; ++i)
    {
        const int idx = (xcorrRingPos + i) % kXcorrSignalLen;
        xcorrScratchA[i] = xcorrPreRing[idx];
        xcorrScratchB[i] = xcorrPostRing[idx];
    }

    // Forward real FFT into the same buffers; output is interleaved complex
    // pairs (Re, Im) covering the full conjugate-symmetric spectrum.
    xcorrFft.performRealOnlyForwardTransform (xcorrScratchA.data(), false);
    xcorrFft.performRealOnlyForwardTransform (xcorrScratchB.data(), false);

    // Cross-spectrum: conj(A) * B - the FFT of pre-vs-post cross-correlation.
    // Write the result back into scratchA in place.
    for (int k = 0; k < kXcorrFftSize; ++k)
    {
        const float ar = xcorrScratchA[2 * k];
        const float ai = xcorrScratchA[2 * k + 1];
        const float br = xcorrScratchB[2 * k];
        const float bi = xcorrScratchB[2 * k + 1];

        xcorrScratchA[2 * k]     = ar * br + ai * bi;
        xcorrScratchA[2 * k + 1] = ar * bi - ai * br;
    }

    // Inverse FFT to recover the cross-correlation in the time domain.
    xcorrFft.performRealOnlyInverseTransform (xcorrScratchA.data());

    // Peak in non-negative lags [0, kXcorrMaxLag) is the offset by which
    // post lags pre - i.e. the amount we need to delay pre to align.
    int   peakLag = 0;
    float peakMag = 0.0f;
    for (int lag = 0; lag < kXcorrMaxLag; ++lag)
    {
        const float v = std::abs (xcorrScratchA[lag]);
        if (v > peakMag)
        {
            peakMag = v;
            peakLag = lag;
        }
    }

    lastMeasuredLatencyOffset.store (peakLag, std::memory_order_release);
}

void WTAnalyzerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool WTAnalyzerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto stereo   = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();

    if (layouts.getMainOutputChannelSet() != stereo)
        return false;

    // Bus 0 is the host-facing main input - must be stereo.
    if (layouts.getChannelSet (true, 0) != stereo)
        return false;

    // Bus 1 surfaces as the sidechain (pre-effect) bus and is optional;
    // hosts typically default-disable sidechain until the user wires it.
    const auto sidechain = layouts.getChannelSet (true, 1);
    if (sidechain != stereo && sidechain != disabled)
        return false;

    return true;
}

void WTAnalyzerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto postBus = getBusBuffer (buffer, true,  0);  // main input = post-effect (wet)
    auto preBus  = getBusBuffer (buffer, true,  1);  // sidechain = pre-effect (dry)
    auto outBus  = getBusBuffer (buffer, false, 0);

    const bool preActive = preBus.getNumChannels() > 0;
    preBusActive.store (preActive, std::memory_order_relaxed);

    // Capture raw pre and post (channel 0, before manual delay) into the
    // cross-correlation ring buffers. Doing this before applying the manual
    // delay means a measurement returns the absolute effect-induced offset,
    // which replaces the manual value cleanly instead of accumulating with it.
    if (preActive && postBus.getNumChannels() > 0)
    {
        auto* preData  = preBus.getReadPointer  (0);
        auto* postData = postBus.getReadPointer (0);

        for (int n = 0; n < numSamples; ++n)
        {
            xcorrPreRing[xcorrRingPos]  = preData[n];
            xcorrPostRing[xcorrRingPos] = postData[n];
            if (++xcorrRingPos >= kXcorrSignalLen)
                xcorrRingPos = 0;
        }
    }

    // Apply manual delay to the pre-effect (sidechain) bus so it can be aligned
    // with the post-effect signal. Integer-sample only at this stage.
    if (preActive)
    {
        const int delaySamples = (int) *apvts.getRawParameterValue ("preDelaySamples");
        preDelayLine.setDelay ((float) delaySamples);

        juce::dsp::AudioBlock<float> preBlock (preBus);
        preDelayLine.process (juce::dsp::ProcessContextReplacing<float> (preBlock));
    }

    auto rmsToDb = [] (float rms)
    {
        return juce::Decibels::gainToDecibels (rms, -100.0f);
    };

    if (postBus.getNumChannels() > 0)
        postEffectLevelDb.store (rmsToDb (postBus.getRMSLevel (0, 0, numSamples)),
                                 std::memory_order_relaxed);

    if (preActive)
        preEffectLevelDb.store (rmsToDb (preBus.getRMSLevel (0, 0, numSamples)),
                                std::memory_order_relaxed);
    else
        preEffectLevelDb.store (-100.0f, std::memory_order_relaxed);

    // Pass post-effect (main bus) to output. Output and main input share buffer
    // memory in most hosts, so this is typically a no-op self-copy; explicit
    // for safety on hosts where they don't alias.
    const int channelsToCopy = juce::jmin (outBus.getNumChannels(), postBus.getNumChannels());

    for (int ch = 0; ch < channelsToCopy; ++ch)
        outBus.copyFrom (ch, 0, postBus, ch, 0, numSamples);

    for (int ch = channelsToCopy; ch < outBus.getNumChannels(); ++ch)
        outBus.clear (ch, 0, numSamples);

    // If the editor requested an auto-measurement, run it now. Brief CPU spike
    // (~3 ms of FFT work) once per click - acceptable for a one-shot trigger.
    if (measureLatencyRequested.exchange (false, std::memory_order_acq_rel))
    {
        if (preActive && postBus.getNumChannels() > 0)
        {
            runLatencyMeasurement();
        }
        else
        {
            // No pre signal - report the current delay value so the editor's
            // "applied" path is a no-op rather than zeroing the parameter.
            lastMeasuredLatencyOffset.store (
                (int) *apvts.getRawParameterValue ("preDelaySamples"),
                std::memory_order_release);
        }

        measureLatencyCompleted.store (true, std::memory_order_release);
    }
}

//==============================================================================
bool WTAnalyzerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* WTAnalyzerAudioProcessor::createEditor()
{
    return new WTAnalyzerAudioProcessorEditor (*this);
}

//==============================================================================
void WTAnalyzerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);
    apvts.state.writeToStream (stream);
}

void WTAnalyzerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid() && tree.hasType (apvts.state.getType()))
        apvts.replaceState (tree);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WTAnalyzerAudioProcessor();
}
