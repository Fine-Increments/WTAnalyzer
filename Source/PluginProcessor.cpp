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

    // Active analysis mode. Index order matches AnalysisMode enum in
    // PluginProcessor.h; adding an analysis means appending to both the
    // enum and this StringArray.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "activeAnalysis", 1 },
        "Active Analysis",
        juce::StringArray { "Generic Overlay", "Frequency Response", "THD Measurement",
                            "Aliasing Detection", "IMD Measurement" },
        0));

    // Level meter mode: false = Peak (default, matches DAW meter behaviour),
    // true = RMS (averaged level, useful for sustained material).
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "meterUseRms", 1 },
        "Use RMS Meter",
        false));

    // THD bars view: which harmonic-bar visualisation to show. Differential
    // (default) shows added energy per harmonic; Pre/Post show each signal's
    // classical own-fundamental-referenced harmonic content. The THD%
    // readout itself is always the differential value regardless of view.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "thdBarsView", 1 },
        "THD Bars View",
        juce::StringArray { "Differential", "Pre", "Post" },
        0));

    // Aliasing view: Composite (default) shows post decomposed - on-grid
    // bins in the pre channel colour, off-grid bins (the aliases) in the
    // analysis colour. Pre / Post show the respective channel trace alone
    // for sanity-checking the underlying signals.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "aliasingView", 1 },
        "Aliasing View",
        juce::StringArray { "Composite", "Pre", "Post" },
        0));

    // IMD bars view: which intermodulation-product bar set to show.
    // Differential (default) is the added-energy view; Pre/Post show each
    // signal's own product magnitudes as a sanity check. The IMD% readout
    // is always the differential regardless of view.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "imdBarsView", 1 },
        "IMD Bars View",
        juce::StringArray { "Differential", "Pre", "Post" },
        0));

    // IMD bars layout: false (default) = "By Order" - bars in fixed slots
    // ordered by |m|+|n| with formula labels in the bottom gutter.
    // true = "By Hz" - bars positioned at their actual product frequency
    // on a log axis with frequency labels along the bottom.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "imdHzLayout", 1 },
        "IMD Bars by Hz",
        false));

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

    currentSampleRate.store ((float) sampleRate, std::memory_order_relaxed);
    spectrumPreBuffer.fill (0.0f);
    spectrumPostBuffer.fill (0.0f);
    spectrumWritePos = 0;
    samplesSinceLastSpectrumFft = 0;
    preSpectrumDb.fill (-120.0f);
    postSpectrumDb.fill (-120.0f);

    frequencyResponse.prepare (kSpectrumBins);

    const float binFreqScale = (float) sampleRate / (float) kSpectrumFftSize;
    thdMeasurement   .prepare (kSpectrumBins, binFreqScale);
    aliasingDetection.prepare (kSpectrumBins, binFreqScale);
    imdMeasurement   .prepare (kSpectrumBins, binFreqScale);

    lastActiveAnalysisIndex = (int) *apvts.getRawParameterValue ("activeAnalysis");
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

    const int peakLag = computeCrossCorrelationFromScratch();
    lastMeasuredLatencyOffset.store (peakLag, std::memory_order_release);
}

void WTAnalyzerAudioProcessor::runSpectrumFft()
{
    // Hann + real FFT + magnitude conversion, twice (pre and post). The
    // window is applied to a windowed copy in scratch, so the circular
    // buffers themselves stay raw for the next overlap-and-FFT cycle.
    auto fftOne = [this] (const std::array<float, kSpectrumFftSize>& source,
                          std::array<float, kSpectrumBins>& outputDb)
    {
        std::fill (spectrumScratch.begin(), spectrumScratch.end(), 0.0f);

        for (int i = 0; i < kSpectrumFftSize; ++i)
        {
            const int idx = (spectrumWritePos + i) % kSpectrumFftSize;
            spectrumScratch[i] = source[idx];
        }

        spectrumWindow.multiplyWithWindowingTable (spectrumScratch.data(),
                                                   (size_t) kSpectrumFftSize);

        // performFrequencyOnlyForwardTransform with ignoreNegativeFreqs=true
        // writes |X[k]| for k = 0..N/2 into the first N/2+1 floats. Bins above
        // N/2 are conjugates of the lower bins for a real signal and don't
        // need to be computed.
        spectrumFft.performFrequencyOnlyForwardTransform (spectrumScratch.data(), true);

        // Normalize so a full-scale sine at any (non-DC, non-Nyquist) bin maps
        // to 0 dB. Factor of 4/N corrects both the FFT scaling (N/2 per side)
        // and the Hann window's 0.5 coherent gain.
        constexpr float normFactor = 4.0f / (float) kSpectrumFftSize;
        for (int bin = 0; bin < kSpectrumBins; ++bin)
            outputDb[bin] = juce::Decibels::gainToDecibels (spectrumScratch[bin] * normFactor,
                                                            -120.0f);
    };

    fftOne (spectrumPreBuffer,  preSpectrumDb);
    fftOne (spectrumPostBuffer, postSpectrumDb);

    // Drive the active analysis from the same spectrum data. Selective
    // execution per PRINCIPLES.md section 9: only the active analysis runs.
    // The Generic Overlay mode (index 0) needs nothing beyond the pre/post
    // dB arrays we just produced for the universal spectrum overlay.
    const int activeIndex = (int) *apvts.getRawParameterValue ("activeAnalysis");

    if (activeIndex != lastActiveAnalysisIndex)
    {
        frequencyResponse.reset();
        thdMeasurement   .reset();
        aliasingDetection.reset();
        imdMeasurement   .reset();
        lastActiveAnalysisIndex = activeIndex;
    }

    if (activeIndex == (int) AnalysisMode::FrequencyResponse)
        frequencyResponse.update (preSpectrumDb.data(), postSpectrumDb.data());
    else if (activeIndex == (int) AnalysisMode::THDMeasurement)
        thdMeasurement.update (preSpectrumDb.data(), postSpectrumDb.data());
    else if (activeIndex == (int) AnalysisMode::AliasingDetection)
        aliasingDetection.update (preSpectrumDb.data(), postSpectrumDb.data());
    else if (activeIndex == (int) AnalysisMode::IMDMeasurement)
        imdMeasurement.update (preSpectrumDb.data(), postSpectrumDb.data());

    spectrumFrameCount.fetch_add (1, std::memory_order_release);
}

int WTAnalyzerAudioProcessor::computeCrossCorrelationFromScratch()
{
    // Remove DC offset from each captured signal. Without this, a DC component
    // contributes a flat N*DC^2 plateau to the cross-correlation at every lag,
    // and the peak finder pins to lag 0 (the first lag it sees with that
    // magnitude).
    {
        double sumA = 0.0;
        double sumB = 0.0;
        for (int i = 0; i < kXcorrSignalLen; ++i)
        {
            sumA += xcorrScratchA[i];
            sumB += xcorrScratchB[i];
        }
        const float meanA = (float) (sumA / (double) kXcorrSignalLen);
        const float meanB = (float) (sumB / (double) kXcorrSignalLen);
        for (int i = 0; i < kXcorrSignalLen; ++i)
        {
            xcorrScratchA[i] -= meanA;
            xcorrScratchB[i] -= meanB;
        }
    }

    // Hann-window both signals so the captured mass is centered, the
    // correlation peak is sharpened, and spurious secondary peaks (from
    // transients sliding around in the capture window between clicks) are
    // dampened.
    xcorrWindow.multiplyWithWindowingTable (xcorrScratchA.data(), (size_t) kXcorrSignalLen);
    xcorrWindow.multiplyWithWindowingTable (xcorrScratchB.data(), (size_t) kXcorrSignalLen);

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

    return peakLag;
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

    auto gainToDb = [] (float gain)
    {
        return juce::Decibels::gainToDecibels (gain, -100.0f);
    };

    if (postBus.getNumChannels() > 0)
    {
        postEffectLevelDb.store (gainToDb (postBus.getRMSLevel  (0, 0, numSamples)),
                                 std::memory_order_relaxed);
        postEffectPeakDb .store (gainToDb (postBus.getMagnitude (0, 0, numSamples)),
                                 std::memory_order_relaxed);
    }

    if (preActive)
    {
        preEffectLevelDb.store (gainToDb (preBus.getRMSLevel  (0, 0, numSamples)),
                                std::memory_order_relaxed);
        preEffectPeakDb .store (gainToDb (preBus.getMagnitude (0, 0, numSamples)),
                                std::memory_order_relaxed);
    }
    else
    {
        preEffectLevelDb.store (-100.0f, std::memory_order_relaxed);
        preEffectPeakDb .store (-100.0f, std::memory_order_relaxed);
    }

    // Spectrum overlay: stream channel 0 of the (delay-compensated) pre and
    // post buses into the circular buffers, and run an FFT every kSpectrumHopSize
    // samples for 75% overlap.
    if (postBus.getNumChannels() > 0)
    {
        const float* preCh0  = preActive ? preBus.getReadPointer (0) : nullptr;
        const float* postCh0 = postBus.getReadPointer (0);

        for (int n = 0; n < numSamples; ++n)
        {
            spectrumPostBuffer[spectrumWritePos] = postCh0[n];
            spectrumPreBuffer [spectrumWritePos] = preCh0 ? preCh0[n] : 0.0f;

            if (++spectrumWritePos >= kSpectrumFftSize)
                spectrumWritePos = 0;

            if (++samplesSinceLastSpectrumFft >= kSpectrumHopSize)
            {
                runSpectrumFft();
                samplesSinceLastSpectrumFft = 0;
            }
        }
    }

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
