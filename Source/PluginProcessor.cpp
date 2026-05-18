/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

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
        juce::StringArray { "Frequency Response", "THD Measurement",
                            "Aliasing Detection", "IMD Measurement", "Direct Impulse IR",
                            "Farina IR", "MLS IR", "Step Response", "Stereo Image",
                            "Parameter Sweep", "Phase Response", "Dynamics" },
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

    // Stereo Image sub-view selector. Divergence (default) is the
    // per-frequency device-added stereo divergence; Correlation is the
    // post L/R phase coherence; Goniometer is the time-domain L/R scope.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "stereoView", 1 },
        "Stereo Image View",
        juce::StringArray { "Divergence", "Correlation", "Goniometer" },
        0));

    // Goniometer rendering mode. "Pre / Post" overlays the input and
    // output stereo clouds in their respective colours; "Difference"
    // scopes the device-added signal (post - aligned pre) per channel.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gonioMode", 1 },
        "Goniometer Mode",
        juce::StringArray { "Pre / Post", "Difference" },
        0));

    // Impulse-response window length in milliseconds. Maximum is 120000
    // (2 minutes) to accommodate supermassive-style reverb tails.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "irWindowMs", 1 },
        "IR Window (ms)",
        ImpulseResponse::kMinWindowMs,
        ImpulseResponse::kMaxWindowMs,
        ImpulseResponse::kDefaultWindowMs));

    // Number of impulses to average. Higher = lower noise floor in the
    // captured IR; lower = faster turn-around per test.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "irAverageCount", 1 },
        "IR Average Count",
        ImpulseResponse::kMinAverages,
        ImpulseResponse::kMaxAverages,
        ImpulseResponse::kDefaultAverages));

    // Farina IR sweep parameters. f0/f1 are the start/end frequencies of
    // the log sweep the user is driving through the device; duration is
    // how long the sweep lasts; tail is how much additional post audio
    // to capture after sweep end (= length of the resulting IR).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "farinaF0Hz", 1 },
        "Farina f0 (Hz)",
        juce::NormalisableRange<float> (FarinaIR::kMinF0Hz, FarinaIR::kMaxF0Hz, 0.0f, 0.3f),
        FarinaIR::kDefaultF0Hz));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "farinaF1Hz", 1 },
        "Farina f1 (Hz)",
        juce::NormalisableRange<float> (FarinaIR::kMinF1Hz, FarinaIR::kMaxF1Hz, 0.0f, 0.3f),
        FarinaIR::kDefaultF1Hz));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "farinaSweepSec", 1 },
        "Farina Sweep (s)",
        juce::NormalisableRange<float> (FarinaIR::kMinSweepSec, FarinaIR::kMaxSweepSec, 0.0f, 0.5f),
        FarinaIR::kDefaultSweepSec));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "farinaTailSec", 1 },
        "Farina Tail (s)",
        juce::NormalisableRange<float> (FarinaIR::kMinTailSec, FarinaIR::kMaxTailSec, 0.0f, 0.5f),
        FarinaIR::kDefaultTailSec));

    // MLS IR parameters. The order (sequence period) must match the order
    // WTGenerator's MLS generator is running; tail is the displayed length
    // of the recovered IR. Version hint 2 - added after the original param
    // set, so AU parameter indices for the existing params are unchanged.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "mlsIrOrder", 2 },
        "MLS IR Order",
        MlsIR::kMinOrder, MlsIR::kMaxOrder, MlsIR::kDefaultOrder));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mlsIrTailSec", 2 },
        "MLS IR Tail (s)",
        juce::NormalisableRange<float> (MlsIR::kMinTailSec, MlsIR::kMaxTailSec, 0.0f, 0.5f),
        MlsIR::kDefaultTailSec));

    // Step Response: the post-step capture window.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stepWindowMs", 2 },
        "Step Window (ms)",
        juce::NormalisableRange<float> (StepResponse::kMinWindowMs,
                                        StepResponse::kMaxWindowMs, 0.0f, 0.5f),
        StepResponse::kDefaultWindowMs));

    // 2D sweep capture across signal-character / parameter axes.
    // sweepPosition is the DAW-automatable lane; the user routes the
    // same automation to this AND to WTSynth's WT Pos (or whatever the
    // source plugin's swept parameter is). When sweepCaptureActive is
    // true, the FrequencyResponse analysis writes its per-bin output
    // into a 2D buffer bucketed by sweepPosition.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sweepPosition", 1 },
        "Sweep Position",
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "sweepCaptureActive", 1 },
        "Sweep Capture Active",
        false));

    // Parameter Sweep mode: which headline metric is recorded as the
    // 1D X-Y curve. Index order matches SweepCurveDisplay's metric
    // selector. THD% and IMD% are differential percentages whose
    // per-channel scalars the THD / IMD analyses already produce.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "sweepMetric", 1 },
        "Sweep Metric",
        juce::StringArray { "THD%", "IMD%" },
        0));

    // Parameter Sweep view: the 1D scalar line plot, or a heatmap of the
    // metric's full per-harmonic / per-product distribution across the
    // sweep. Index order matches SweepCurveDisplay's view selector.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "sweepView", 1 },
        "Sweep View",
        juce::StringArray { "Line", "Heatmap" },
        0));

    // Phase Response mode sub-view selector: the detrended phase curve
    // (degrees) or group delay (ms). Index order matches PhaseDisplay's
    // view selector.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "phaseView", 1 },
        "Phase Response View",
        juce::StringArray { "Phase", "Group Delay" },
        0));

    // IR-mode sub-view selector, shared by Direct Impulse IR and Farina
    // IR: the time-domain waveform, or the captured IR's Cumulative
    // Spectral Decay as a heatmap or a 3D waterfall. Index order matches
    // the view selector in both IR displays.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "irView", 1 },
        "IR View",
        juce::StringArray { "Waveform", "CSD Heatmap", "CSD 3D" },
        0));

    // Stereo display toggles - one set shared across every mode that
    // participates in the L / R / Diff convention (PLANNING.md 8.5.1).
    // L and R are independent on/off; at least one must stay on
    // (enforced in the editor). Diff is a separate additive overlay.
    // Defaults: L on, R on, Diff off.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "showChannelL", 1 },
        "Show L Channel",
        true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "showChannelR", 1 },
        "Show R Channel",
        true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "showChannelDiff", 1 },
        "Show Diff Overlay",
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
    spectrumPreBuffer    .fill (0.0f);
    spectrumPreBuffer_R  .fill (0.0f);
    spectrumPostBuffer   .fill (0.0f);
    spectrumPostBuffer_R .fill (0.0f);
    spectrumWritePos = 0;
    samplesSinceLastSpectrumFft = 0;
    preSpectrumDb    .fill (-120.0f);
    preSpectrumDb_R  .fill (-120.0f);
    postSpectrumDb   .fill (-120.0f);
    postSpectrumDb_R .fill (-120.0f);

    frequencyResponse.prepare (kSpectrumBins);

    const float binFreqScale = (float) sampleRate / (float) kSpectrumFftSize;
    thdMeasurement   .prepare (kSpectrumBins, binFreqScale);
    aliasingDetection.prepare (kSpectrumBins, binFreqScale);
    imdMeasurement   .prepare (kSpectrumBins, binFreqScale);
    stereoAnalysis   .prepare (kSpectrumBins);
    phaseResponse    .prepare (kSpectrumBins, binFreqScale);
    impulseResponse  .prepare (sampleRate, samplesPerBlock);
    impulseResponse  .setWindowMs    ((int) *apvts.getRawParameterValue ("irWindowMs"));
    impulseResponse  .setAverageGoal ((int) *apvts.getRawParameterValue ("irAverageCount"));

    farinaIR.prepare (sampleRate, samplesPerBlock);
    farinaIR.setSweepParams (*apvts.getRawParameterValue ("farinaF0Hz"),
                             *apvts.getRawParameterValue ("farinaF1Hz"),
                             *apvts.getRawParameterValue ("farinaSweepSec"),
                             *apvts.getRawParameterValue ("farinaTailSec"));

    mlsIR.prepare (sampleRate, samplesPerBlock);
    mlsIR.setParams ((int) *apvts.getRawParameterValue ("mlsIrOrder"),
                     *apvts.getRawParameterValue ("mlsIrTailSec"));

    stepResponse.prepare (sampleRate, samplesPerBlock);
    stepResponse.setParams (*apvts.getRawParameterValue ("stepWindowMs"));

    sweepCapture.prepare (kSpectrumBins);
    sweepCurve  .reset();
    sweepGrid   .reset();

    lastActiveAnalysisIndex = (int) *apvts.getRawParameterValue ("activeAnalysis");
    lastSweepMetric          = (int) *apvts.getRawParameterValue ("sweepMetric");
    sweepCaptureWasArmed     = false;
    sweepTransportWasPlaying = false;
    sweepLastPosition        = 0.0f;
    sweepCaptureWarmup       = 0;
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
    // Hann + real FFT, four times (pre/post, L/R). Every channel retains
    // its complex (re/im) spectrum: the Stereo Image Correlation view and
    // the Phase Response mode both form a pre/post cross-spectrum, which
    // needs the imaginary part. performRealOnlyForwardTransform writes
    // interleaved [re, im] pairs; bin k lives at scratch[2k], scratch[2k+1].
    // Magnitude is hypot(re, im), matching performFrequencyOnlyForwardTransform.
    // The window is applied to a windowed copy in scratch, so the circular
    // buffers themselves stay raw for the next overlap-and-FFT cycle.
    auto fftComplexOne = [this] (const std::array<float, kSpectrumFftSize>& source,
                                 std::array<float, kSpectrumBins>& outputDb,
                                 std::array<float, 2 * kSpectrumBins>& outputComplex)
    {
        std::fill (spectrumScratch.begin(), spectrumScratch.end(), 0.0f);

        for (int i = 0; i < kSpectrumFftSize; ++i)
        {
            const int idx = (spectrumWritePos + i) % kSpectrumFftSize;
            spectrumScratch[i] = source[idx];
        }

        spectrumWindow.multiplyWithWindowingTable (spectrumScratch.data(),
                                                   (size_t) kSpectrumFftSize);

        spectrumFft.performRealOnlyForwardTransform (spectrumScratch.data(), true);

        constexpr float normFactor = 4.0f / (float) kSpectrumFftSize;
        for (int bin = 0; bin < kSpectrumBins; ++bin)
        {
            const float re = spectrumScratch[2 * bin];
            const float im = spectrumScratch[2 * bin + 1];
            outputComplex[(size_t) (2 * bin)]     = re;
            outputComplex[(size_t) (2 * bin + 1)] = im;
            outputDb[bin] = juce::Decibels::gainToDecibels (std::hypot (re, im) * normFactor,
                                                            -120.0f);
        }
    };

    fftComplexOne (spectrumPreBuffer,    preSpectrumDb,    preComplexL);  // L
    fftComplexOne (spectrumPreBuffer_R,  preSpectrumDb_R,  preComplexR);  // R
    fftComplexOne (spectrumPostBuffer,   postSpectrumDb,   postComplexL); // L
    fftComplexOne (spectrumPostBuffer_R, postSpectrumDb_R, postComplexR); // R

    // Drive the active analysis from the same spectrum data. Selective
    // execution per PRINCIPLES.md section 9: only the active analysis runs.
    const int activeIndex = (int) *apvts.getRawParameterValue ("activeAnalysis");

    if (activeIndex != lastActiveAnalysisIndex)
    {
        frequencyResponse.reset();
        thdMeasurement   .reset();
        aliasingDetection.reset();
        imdMeasurement   .reset();
        impulseResponse  .reset();
        farinaIR         .reset();
        stereoAnalysis   .reset();
        phaseResponse    .reset();
        dynamicsCurve    .reset();
        mlsIR            .reset();
        stepResponse     .reset();
        lastActiveAnalysisIndex = activeIndex;
    }

    if (activeIndex == (int) AnalysisMode::FrequencyResponse)
    {
        frequencyResponse.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                                  preSpectrumDb_R.data(), postSpectrumDb_R.data());

        // Optional 2D sweep capture: when armed, drop the current FR
        // trace into the SweepCapture bucket corresponding to the
        // current sweepPosition APVTS value. Cheap (~kSpectrumBins
        // float copies per spectrum hop, ~47 Hz at 48 kHz).
        if (*apvts.getRawParameterValue ("sweepCaptureActive") > 0.5f)
        {
            const float position = *apvts.getRawParameterValue ("sweepPosition");
            sweepCapture.captureFrame (position,
                                       frequencyResponse.getResponseDb().data(),
                                       frequencyResponse.getNumBins());
        }
    }
    else if (activeIndex == (int) AnalysisMode::THDMeasurement)
        thdMeasurement.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                               preSpectrumDb_R.data(), postSpectrumDb_R.data());
    else if (activeIndex == (int) AnalysisMode::AliasingDetection)
        aliasingDetection.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                                  preSpectrumDb_R.data(), postSpectrumDb_R.data());
    else if (activeIndex == (int) AnalysisMode::IMDMeasurement)
        imdMeasurement.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                               preSpectrumDb_R.data(), postSpectrumDb_R.data());
    else if (activeIndex == (int) AnalysisMode::StereoImage)
        stereoAnalysis.update (preSpectrumDb  .data(), preSpectrumDb_R.data(),
                               postSpectrumDb .data(), postSpectrumDb_R.data(),
                               postComplexL.data(), postComplexR.data());
    else if (activeIndex == (int) AnalysisMode::ParameterSweep)
    {
        // Parameter Sweep runs the selected headline-metric analysis and,
        // while capture is armed, records its per-channel scalar bucketed
        // by sweepPosition. Switching metric clears the curve (THD% and
        // IMD% are different units).
        const int metric = (int) *apvts.getRawParameterValue ("sweepMetric");
        if (metric != lastSweepMetric)
        {
            sweepCurve.reset();
            sweepGrid .reset();
            lastSweepMetric = metric;
        }

        float vL = SweepCurve::kNoData;
        float vR = SweepCurve::kNoData;

        // Heatmap row: the metric's full per-harmonic / per-product
        // differential dB distribution for this frame, per channel.
        float rowL[SweepGrid::kMaxCols];
        float rowR[SweepGrid::kMaxCols];
        int   numCols = 0;

        if (metric == 0)   // THD%
        {
            thdMeasurement.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                                   preSpectrumDb_R.data(), postSpectrumDb_R.data());
            if (thdMeasurement.isValid (THDMeasurement::Channel::L))
                vL = thdMeasurement.getTotalThdPercent (THDMeasurement::Channel::L);
            if (thdMeasurement.isValid (THDMeasurement::Channel::R))
                vR = thdMeasurement.getTotalThdPercent (THDMeasurement::Channel::R);

            // Harmonics 2..16 - the distortion harmonics (1 is the
            // fundamental). 15 columns.
            numCols = 15;
            for (int c = 0; c < numCols; ++c)
            {
                rowL[c] = thdMeasurement.getHarmonicRatioDb (THDMeasurement::Source::Diff,
                                                             c + 2, THDMeasurement::Channel::L);
                rowR[c] = thdMeasurement.getHarmonicRatioDb (THDMeasurement::Source::Diff,
                                                             c + 2, THDMeasurement::Channel::R);
            }
        }
        else               // IMD%
        {
            imdMeasurement.update (preSpectrumDb  .data(), postSpectrumDb  .data(),
                                   preSpectrumDb_R.data(), postSpectrumDb_R.data());
            if (imdMeasurement.isValid (IMDMeasurement::Channel::L))
                vL = imdMeasurement.getTotalImdPercent (IMDMeasurement::Channel::L);
            if (imdMeasurement.isValid (IMDMeasurement::Channel::R))
                vR = imdMeasurement.getTotalImdPercent (IMDMeasurement::Channel::R);

            numCols = imdMeasurement.getNumProducts();
            for (int c = 0; c < numCols && c < SweepGrid::kMaxCols; ++c)
            {
                rowL[c] = imdMeasurement.getProductRatioDb (IMDMeasurement::Source::Diff,
                                                            c, IMDMeasurement::Channel::L);
                rowR[c] = imdMeasurement.getProductRatioDb (IMDMeasurement::Source::Diff,
                                                            c, IMDMeasurement::Channel::R);
            }
        }

        const bool  armed    = *apvts.getRawParameterValue ("sweepCaptureActive") > 0.5f;
        const float position = *apvts.getRawParameterValue ("sweepPosition");

        // A fresh sweep pass begins on any of: arming capture, the
        // transport starting, or the sweep position snapping backward
        // (a loop wrap or replay). Skip a brief warm-up past each
        // boundary so the settling transient (an FFT window straddling
        // silence, or the previous extreme) does not land in a bucket
        // and hijack the display's Y auto-range. The snap-back test
        // catches looped playback, where the transport reports no play
        // rising edge.
        //
        // Kept to the physical minimum - two full FFT-window refreshes -
        // so it eats as little of the start-of-sweep region as possible.
        // The window straddle means the extremes still cannot be read
        // cleanly from a ramped sweep regardless; the user holds the
        // automation briefly at each extreme to measure them (mode help).
        constexpr int   kSweepWarmupFrames =
            2 * (kSpectrumFftSize / kSpectrumHopSize);
        constexpr float kPassSnapBack = 0.15f;
        const bool armRising   = armed && ! sweepCaptureWasArmed;
        const bool playRising  = transportPlaying && ! sweepTransportWasPlaying;
        const bool snappedBack = (sweepLastPosition - position) > kPassSnapBack;
        if (armRising || playRising || snappedBack)
            sweepCaptureWarmup = kSweepWarmupFrames;

        sweepCaptureWasArmed     = armed;
        sweepTransportWasPlaying = transportPlaying;
        sweepLastPosition        = position;

        // Capture only while armed AND the transport is playing, so an
        // armed-but-stopped plugin does not dump idle readings into a
        // bucket.
        if (sweepCaptureWarmup > 0)
        {
            --sweepCaptureWarmup;
        }
        else if (armed && transportPlaying
                 && (vL != SweepCurve::kNoData || vR != SweepCurve::kNoData))
        {
            sweepCurve.captureFrame (position, vL, vR);
            sweepGrid .captureFrame (position, rowL, rowR, numCols);
        }
    }
    else if (activeIndex == (int) AnalysisMode::PhaseResponse)
    {
        phaseResponse.update (preSpectrumDb.data(),  preSpectrumDb_R.data(),
                              preComplexL.data(),    preComplexR.data(),
                              postComplexL.data(),   postComplexR.data());
    }

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

    // Scrub non-finite samples at the input boundary. A device under test
    // can emit NaN / Inf (an unstable filter at a sweep extreme, a
    // divide-by-zero, an uninitialised tail) - left unchecked it
    // propagates through every FFT to all bins and corrupts the whole
    // display (a juce::Path built with NaN coordinates rasterises as
    // blocky garbage). Scrubbing here protects every downstream analysis
    // and the pass-through output in one pass. Cheap: a handful of
    // channels x blockSize finite-checks.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
            if (! std::isfinite (d[n]))
                d[n] = 0.0f;
    }

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

    const int postChans = postBus.getNumChannels();
    const int preChans  = preActive ? preBus.getNumChannels() : 0;

    if (postChans > 0)
    {
        postEffectLevelDb.store (gainToDb (postBus.getRMSLevel  (0, 0, numSamples)),
                                 std::memory_order_relaxed);
        postEffectPeakDb .store (gainToDb (postBus.getMagnitude (0, 0, numSamples)),
                                 std::memory_order_relaxed);

        const int rIdx = postChans > 1 ? 1 : 0;
        postEffectLevelDb_R.store (gainToDb (postBus.getRMSLevel  (rIdx, 0, numSamples)),
                                   std::memory_order_relaxed);
        postEffectPeakDb_R .store (gainToDb (postBus.getMagnitude (rIdx, 0, numSamples)),
                                   std::memory_order_relaxed);
    }

    if (preActive)
    {
        preEffectLevelDb.store (gainToDb (preBus.getRMSLevel  (0, 0, numSamples)),
                                std::memory_order_relaxed);
        preEffectPeakDb .store (gainToDb (preBus.getMagnitude (0, 0, numSamples)),
                                std::memory_order_relaxed);

        const int rIdx = preChans > 1 ? 1 : 0;
        preEffectLevelDb_R.store (gainToDb (preBus.getRMSLevel  (rIdx, 0, numSamples)),
                                  std::memory_order_relaxed);
        preEffectPeakDb_R .store (gainToDb (preBus.getMagnitude (rIdx, 0, numSamples)),
                                  std::memory_order_relaxed);
    }
    else
    {
        preEffectLevelDb  .store (-100.0f, std::memory_order_relaxed);
        preEffectPeakDb   .store (-100.0f, std::memory_order_relaxed);
        preEffectLevelDb_R.store (-100.0f, std::memory_order_relaxed);
        preEffectPeakDb_R .store (-100.0f, std::memory_order_relaxed);
    }

    // Spectrum overlay: stream channel 0 of the (delay-compensated) pre and
    // post buses into the circular buffers, and run an FFT every kSpectrumHopSize
    // samples for 75% overlap. Also feed the ImpulseResponse analysis its
    // per-sample pre/post pair when its mode is active, since IR works on
    // raw time-domain samples (not the spectrum FFT output).
    const int activeIndexLocal = (int) *apvts.getRawParameterValue ("activeAnalysis");
    const bool irActive       = activeIndexLocal == (int) AnalysisMode::DirectImpulseIR;
    const bool farinaActive   = activeIndexLocal == (int) AnalysisMode::FarinaIR;
    const bool stereoActive   = activeIndexLocal == (int) AnalysisMode::StereoImage;
    const bool mlsActive      = activeIndexLocal == (int) AnalysisMode::MlsIR;
    const bool stepActive     = activeIndexLocal == (int) AnalysisMode::StepResponse;

    // Transport state for the Parameter Sweep capture gate. Assume the
    // transport is playing if the host does not report a playhead, so
    // capture still works on hosts that withhold transport info.
    transportPlaying = true;
    if (auto* ph = getPlayHead())
        if (auto posInfo = ph->getPosition())
            transportPlaying = posInfo->getIsPlaying();

    // Dynamics transfer curve: when the mode is active and the pre bus is
    // wired, bin this block's pre-effect RMS level and fold the matching
    // post-effect level into that bin's running mean. The per-block RMS
    // values were stored just above; a slow amplitude ramp through the
    // device walks the bins and the static transfer curve emerges.
    if (activeIndexLocal == (int) AnalysisMode::Dynamics
        && preActive && postChans > 0)
    {
        dynamicsCurve.captureFrame (
            preEffectLevelDb   .load (std::memory_order_relaxed),
            postEffectLevelDb  .load (std::memory_order_relaxed),
            preEffectLevelDb_R .load (std::memory_order_relaxed),
            postEffectLevelDb_R.load (std::memory_order_relaxed));
    }

    // Poll window / average params each block so UI changes take effect
    // promptly. Cheap; only invalidates state if values actually changed.
    impulseResponse.setWindowMs    ((int) *apvts.getRawParameterValue ("irWindowMs"));
    impulseResponse.setAverageGoal ((int) *apvts.getRawParameterValue ("irAverageCount"));

    farinaIR.setSweepParams (*apvts.getRawParameterValue ("farinaF0Hz"),
                             *apvts.getRawParameterValue ("farinaF1Hz"),
                             *apvts.getRawParameterValue ("farinaSweepSec"),
                             *apvts.getRawParameterValue ("farinaTailSec"));

    mlsIR.setParams ((int) *apvts.getRawParameterValue ("mlsIrOrder"),
                     *apvts.getRawParameterValue ("mlsIrTailSec"));
    stepResponse.setParams (*apvts.getRawParameterValue ("stepWindowMs"));

    if (postBus.getNumChannels() > 0)
    {
        const float* preCh0  = preActive ? preBus.getReadPointer (0) : nullptr;
        const float* postCh0 = postBus.getReadPointer (0);

        // Right-channel pointers; fall back to channel 0 when the bus is
        // mono (or sidechain is mono) so a mono signal through a stereo
        // plugin still feeds both L and R streams with identical samples.
        const float* preCh1  = (preActive && preBus.getNumChannels() > 1)
                                 ? preBus.getReadPointer (1) : preCh0;
        const float* postCh1 = (postBus.getNumChannels() > 1)
                                 ? postBus.getReadPointer (1) : postCh0;

        for (int n = 0; n < numSamples; ++n)
        {
            spectrumPostBuffer   [spectrumWritePos] = postCh0[n];
            spectrumPostBuffer_R [spectrumWritePos] = postCh1[n];
            spectrumPreBuffer    [spectrumWritePos] = preCh0 ? preCh0[n] : 0.0f;
            spectrumPreBuffer_R  [spectrumWritePos] = preCh1 ? preCh1[n] : 0.0f;

            if (++spectrumWritePos >= kSpectrumFftSize)
                spectrumWritePos = 0;

            if (++samplesSinceLastSpectrumFft >= kSpectrumHopSize)
            {
                runSpectrumFft();
                samplesSinceLastSpectrumFft = 0;
            }

            if (stereoActive)
            {
                const int gp = gonioWritePos.load (std::memory_order_relaxed);
                gonioPostL[(size_t) gp] = postCh0[n];
                gonioPostR[(size_t) gp] = postCh1[n];
                gonioPreL [(size_t) gp] = preCh0 ? preCh0[n] : 0.0f;
                gonioPreR [(size_t) gp] = preCh1 ? preCh1[n] : 0.0f;
                gonioWritePos.store ((gp + 1) & (kGonioBufferSize - 1),
                                     std::memory_order_relaxed);
            }

            if (irActive)
                impulseResponse.processSample (preCh0 ? preCh0[n] : 0.0f, postCh0[n],
                                               preCh1 ? preCh1[n] : 0.0f, postCh1[n]);

            if (farinaActive)
                farinaIR.processSample (preCh0 ? preCh0[n] : 0.0f, postCh0[n],
                                        preCh1 ? preCh1[n] : 0.0f, postCh1[n]);

            if (mlsActive)
                mlsIR.processSample (preCh0 ? preCh0[n] : 0.0f, postCh0[n],
                                     preCh1 ? preCh1[n] : 0.0f, postCh1[n]);

            if (stepActive)
                stepResponse.processSample (preCh0 ? preCh0[n] : 0.0f, postCh0[n],
                                            preCh1 ? preCh1[n] : 0.0f, postCh1[n]);
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
