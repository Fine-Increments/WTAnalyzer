/*
  ==============================================================================

    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Colors.h"

//==============================================================================
// Modal popup that shows the detailed instructions for whichever analysis
// mode was active when Help was clicked. Body is a scrollable, read-only
// TextEditor so long mode descriptions don't truncate.
namespace
{
    class HelpContentComponent  : public juce::Component
    {
    public:
        HelpContentComponent (const juce::String& body)
        {
            editor.setMultiLine (true, true);
            editor.setReadOnly (true);
            editor.setScrollbarsShown (true);
            editor.setCaretVisible (false);
            editor.setJustification (juce::Justification::topLeft);
            editor.setFont (juce::FontOptions (13.0f));
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff181a1d));
            editor.setColour (juce::TextEditor::textColourId,       juce::Colour (0xffd8d8d8));
            editor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff2a2d32));
            editor.setText (body, juce::dontSendNotification);
            editor.moveCaretToTop (false);
            addAndMakeVisible (editor);
            setSize (560, 440);
        }

        void resized() override
        {
            editor.setBounds (getLocalBounds().reduced (12));
        }

    private:
        juce::TextEditor editor;
    };
}

//==============================================================================
WTAnalyzerAudioProcessorEditor::WTAnalyzerAudioProcessorEditor (WTAnalyzerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor       (p),
      spectrumDisplay      (p),
      cursorReadout        (spectrumDisplay),
      thdDisplay           (p),
      imdDisplay           (p),
      impulseDisplay       (p),
      farinaDisplay        (p),
      stereoDisplay        (p),
      sweepCurveDisplay    (p),
      phaseDisplay         (p),
      dynamicsDisplay      (p),
      levelMetersPanel     (p),
      latencyPanel         (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (spectrumDisplay);
    addAndMakeVisible (cursorReadout);
    addChildComponent (imbalanceReadout);
    addChildComponent (thdDisplay);     // hidden by default; applyAnalysisMode shows it
    addChildComponent (imdDisplay);
    addChildComponent (impulseDisplay);
    addChildComponent (farinaDisplay);
    addChildComponent (stereoDisplay);
    addChildComponent (sweepCurveDisplay);
    addChildComponent (phaseDisplay);
    addChildComponent (dynamicsDisplay);
    addAndMakeVisible (levelMetersPanel);
    addAndMakeVisible (latencyPanel);

    // Sidechain notice is added last so it sits on top of every display
    // panel. Hidden until the timer sees the sidechain go inactive.
    addChildComponent (sidechainNotice);

    // Analysis-mode selector. The ComboBox is populated from the parameter's
    // own choice list, so adding an analysis to the APVTS layout in
    // PluginProcessor automatically extends this dropdown.
    addAndMakeVisible (analysisSelector);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
            audioProcessor.apvts.getParameter ("activeAnalysis")))
    {
        analysisSelector.addItemList (choice->choices, 1);
    }
    analysisAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.apvts, "activeAnalysis", analysisSelector);

    addAndMakeVisible (sidecarButton);
    sidecarButton.onClick = [this] { chooseSidecarFile(); };
    updateSidecarButtonText();

    audioProcessor.sidecar.onContextChanged = [this]
    {
        updateSidecarButtonText();
        repaint();
    };

    addAndMakeVisible (helpButton);
    helpButton.setButtonText ("?");
    helpButton.onClick = [this] { openHelpDialog(); };

    // Sweep capture controls. Visibility is mode-driven below in
    // applyAnalysisMode (visible only in FR mode). Engaged-state
    // styling matches Hold/Freeze elsewhere.
    const juce::Colour sweepEngagedFill (0xffcfd2d6);
    addChildComponent (sweepCaptureButton);
    sweepCaptureButton.setClickingTogglesState (true);
    sweepCaptureButton.setColour (juce::TextButton::buttonOnColourId, sweepEngagedFill);
    sweepCaptureButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    sweepCaptureAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, "sweepCaptureActive", sweepCaptureButton);

    addChildComponent (sweepClearButton);
    sweepClearButton.onClick = [this]
    {
        // The Capture / Clear pair is shared by the FR 2D heatmap and the
        // Parameter Sweep curve; only one is meaningful per mode, so Clear
        // wipes both - harmless for the inactive one.
        audioProcessor.sweepCapture.reset();
        audioProcessor.sweepCurve.reset();
        audioProcessor.sweepGrid.reset();
        repaint();
    };

    // Farina IR pair. Capture is momentary - it just arms the
    // one-shot deconvolution trigger inside FarinaIR. Clear wipes
    // the captured IR + state. Both share the same header slot as
    // the FR sweep buttons; visibility swap is mode-driven below.
    addChildComponent (farinaCaptureButton);
    farinaCaptureButton.onClick = [this]
    {
        audioProcessor.farinaIR.requestCapture();
        repaint();
    };

    addChildComponent (farinaClearButton);
    farinaClearButton.onClick = [this]
    {
        audioProcessor.farinaIR.reset();
        repaint();
    };

    // L / R / Diff toggle row. Same engaged-fill styling as Hold / Freeze /
    // Capture so the toggle state is unambiguous. Visibility is mode-driven
    // (set in applyAnalysisMode).
    //
    // Toggle semantics:
    //   - Diff ON: exclusive view. L and R both turn off. Display shows a
    //     bipolar (R - L) trace centered at zero, sign-coloured by which
    //     channel is louder per bin/sample.
    //   - Diff OFF: L and R are independent on/off toggles with the
    //     at-least-one rule (turning off the last one auto-flips it back).
    //   - Clicking L or R while Diff is on takes us back to that channel's
    //     L/R view: Diff turns off, the clicked channel turns on, the other
    //     stays off so the user sees a focused single-channel view.
    const juce::Colour stereoEngagedFill (0xffcfd2d6);
    auto styleToggle = [&] (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, stereoEngagedFill);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
        addChildComponent (b);
    };
    styleToggle (stereoLButton);
    styleToggle (stereoRButton);
    styleToggle (stereoDiffButton);

    stereoLAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, "showChannelL",    stereoLButton);
    stereoRAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, "showChannelR",    stereoRButton);
    stereoDiffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, "showChannelDiff", stereoDiffButton);

    // Repaint everything that could be showing stereo data so a click
    // takes effect immediately without waiting for the 30 Hz repaint timer.
    auto repaintStereoConsumers = [this]
    {
        spectrumDisplay.repaint();
        thdDisplay     .repaint();
        imdDisplay     .repaint();
        impulseDisplay .repaint();
        farinaDisplay  .repaint();
    };

    // L clicked: post-click state is whatever the toggle now reports.
    stereoLButton.onClick = [this, repaintStereoConsumers]
    {
        const bool lOn = stereoLButton.getToggleState();
        const bool dOn = stereoDiffButton.getToggleState();

        if (lOn && dOn)
        {
            // User just turned L on while Diff was active - leave Diff exclusive
            // view, switch to L-only. R stays off.
            stereoDiffButton.setToggleState (false, juce::sendNotification);
            stereoRButton   .setToggleState (false, juce::sendNotification);
        }
        else if (! lOn && ! dOn)
        {
            // Turning L off with Diff off - enforce at-least-one of L/R.
            const bool rOn = stereoRButton.getToggleState();
            if (! rOn)
                stereoLButton.setToggleState (true, juce::sendNotification);
        }
        repaintStereoConsumers();
    };

    stereoRButton.onClick = [this, repaintStereoConsumers]
    {
        const bool rOn = stereoRButton.getToggleState();
        const bool dOn = stereoDiffButton.getToggleState();

        if (rOn && dOn)
        {
            stereoDiffButton.setToggleState (false, juce::sendNotification);
            stereoLButton   .setToggleState (false, juce::sendNotification);
        }
        else if (! rOn && ! dOn)
        {
            const bool lOn = stereoLButton.getToggleState();
            if (! lOn)
                stereoRButton.setToggleState (true, juce::sendNotification);
        }
        repaintStereoConsumers();
    };

    stereoDiffButton.onClick = [this, repaintStereoConsumers]
    {
        const bool dOn = stereoDiffButton.getToggleState();
        if (dOn)
        {
            // Entering Diff exclusive view - turn off L and R.
            stereoLButton.setToggleState (false, juce::sendNotification);
            stereoRButton.setToggleState (false, juce::sendNotification);
        }
        else
        {
            // Leaving Diff - restore the default L+R view so the user
            // isn't stuck with an empty display.
            stereoLButton.setToggleState (true, juce::sendNotification);
            stereoRButton.setToggleState (true, juce::sendNotification);
        }
        repaintStereoConsumers();
    };

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (kBaseWidth, kBaseHeight);

    // Pick up the current activeAnalysis value immediately and any subsequent
    // changes (from the ComboBox, host preset load, host automation). Cheap
    // poll - mode changes are infrequent so 10 Hz is plenty.
    applyAnalysisMode ((int) *audioProcessor.apvts.getRawParameterValue ("activeAnalysis"));
    startTimerHz (30);
}

WTAnalyzerAudioProcessorEditor::~WTAnalyzerAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.sidecar.onContextChanged = nullptr;
    setLookAndFeel (nullptr);  // detach before LookAndFeel member destructs
}

void WTAnalyzerAudioProcessorEditor::chooseSidecarFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select wavetable.json sidecar",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.json");

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile())
                audioProcessor.sidecar.setPath (file);
        });
}

void WTAnalyzerAudioProcessorEditor::updateSidecarButtonText()
{
    const auto& ctx = audioProcessor.sidecar.getContext();
    sidecarButton.setButtonText (ctx.valid
        ? juce::String ("Sidecar: ") + ctx.sourceFile.getFileNameWithoutExtension()
        : juce::String ("Load Sidecar"));
}

void WTAnalyzerAudioProcessorEditor::openHelpDialog()
{
    const int modeIndex = lastAppliedAnalysisMode;
    auto* content = new HelpContentComponent (getModeHelpText (modeIndex));

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle              = "WTAnalyzer - " + getModeName (modeIndex);
    options.dialogBackgroundColour   = juce::Colour (0xff202225);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar        = true;
    options.resizable                = true;
    options.launchAsync();
}

juce::String WTAnalyzerAudioProcessorEditor::getModeName (int modeIndex)
{
    using Mode = WTAnalyzerAudioProcessor::AnalysisMode;
    switch ((Mode) modeIndex)
    {
        case Mode::FrequencyResponse: return "Frequency Response";
        case Mode::THDMeasurement:    return "THD Measurement";
        case Mode::AliasingDetection: return "Aliasing Detection";
        case Mode::IMDMeasurement:    return "IMD Measurement";
        case Mode::DirectImpulseIR:   return "Direct Impulse IR";
        case Mode::FarinaIR:          return "Farina IR";
        case Mode::StereoImage:       return "Stereo Image";
        case Mode::ParameterSweep:    return "Parameter Sweep";
        case Mode::PhaseResponse:     return "Phase Response";
        case Mode::Dynamics:          return "Dynamics";
    }
    return "Analysis";
}

juce::String WTAnalyzerAudioProcessorEditor::getModeHelpText (int modeIndex)
{
    using Mode = WTAnalyzerAudioProcessor::AnalysisMode;
    switch ((Mode) modeIndex)
    {
        case Mode::FrequencyResponse:
            return
                "Frequency Response\n"
                "==================\n\n"
                "What it measures\n"
                "  The per-bin transfer function of the device under test: post_dB minus\n"
                "  pre_dB at every FFT bin. Equivalent to post / pre in the linear\n"
                "  domain - i.e. the device's gain at each frequency.\n\n"
                "Input\n"
                "  Best with a broadband signal that excites the entire audible spectrum:\n"
                "    - Linear or log sine sweep, 20 Hz - 20 kHz, 10+ seconds.\n"
                "    - Pink noise (equal energy per octave - good for EQ-style devices).\n"
                "    - White noise (equal energy per Hz - good for full-spectrum coverage).\n"
                "  Wavetable sweeps also work and surface signal-character response when\n"
                "  characterising how an effect reacts to different spectral shapes.\n\n"
                "How to read it\n"
                "  Green trace = the device's gain at each frequency, drawn on top of the\n"
                "  pre/post spectra. Flat at 0 dB means transparent. Peaks or dips reveal\n"
                "  the filter character. Slope shows tilt or shelving.\n\n"
                "  Bins where the pre signal is too quiet to measure cleanly are flagged\n"
                "  as 'no measurement' and break the trace path. This prevents misleading\n"
                "  spikes where pre is essentially silent and any post energy would\n"
                "  divide to infinity.\n\n"
                "2D Sweep Capture (signal-character axis)\n"
                "  The Capture and Clear buttons in the plugin header (left of the\n"
                "  Load Sidecar button) drive a 2D recorder. While Capture is on,\n"
                "  every FFT frame's frequency response is bucketed into a 2D grid\n"
                "  indexed by the 'Sweep Position' APVTS parameter (0..1,\n"
                "  DAW-automatable). Route the same DAW automation lane to both\n"
                "  WTSynth's WT Pos and WTAnalyzer's Sweep Position; as the wavetable\n"
                "  morphs, the analyzer accumulates a heatmap of how the device responds\n"
                "  at each spectral shape.\n\n"
                "  Visual style: smooth heatmap (continuous X = log frequency, Y =\n"
                "  sweep position 0..1). Colourmap is bipolar around 0 dB (the\n"
                "  unity-gain anchor): black at -60 dB (heavy cut) -> green at 0 dB\n"
                "  (transparent device) -> red at +12 dB (positive gain). 'No\n"
                "  measurement' bins stay at the plot-area background colour so\n"
                "  empty regions are visually distinct from measured-zero regions.\n"
                "  A thin whitesmoke horizontal line marks the current sweepPosition\n"
                "  ('you are here').\n\n"
                "  Clear wipes the heatmap. Toggle Capture off to freeze the heatmap\n"
                "  while continuing to view the live trace.\n\n"
                "Stereo (L / R / Diff)\n"
                "  FR is now per-channel. Chartreuse trace = L (master) FR,\n"
                "  green trace = R FR. The L / R / Diff toggle row at the\n"
                "  right end of the cursor-readout strip controls visibility.\n"
                "  L and R are independent on/off (at least one must stay on);\n"
                "  Diff is an additive whitesmoke overlay showing FR_R(f) -\n"
                "  FR_L(f). A flat zero Diff line means the device is\n"
                "  symmetric across channels; deviations reveal where L and R\n"
                "  diverge in gain. Mono signals (channel 1 sources from\n"
                "  channel 0) overlap pixel-for-pixel - L and R sit on top of\n"
                "  each other and you see only the chartreuse master trace.\n"
                "  In 2D heatmap mode (Capture on) the toggles will eventually\n"
                "  pick which single channel's heatmap to render at full\n"
                "  resolution; that L/R/Diff-aware heatmap variant is not\n"
                "  wired yet (today the heatmap captures L only).\n\n"
                "Tips\n"
                "  - Time-align pre and post before measuring. Use the Latency panel to\n"
                "    measure the device's latency and apply that delay to pre.\n"
                "  - A 10+ second sweep gives the smoothest result with the most coverage.\n"
                "    Very short sweeps leave the upper end under-resolved.\n"
                "  - For 2D sweep capture, a slow WT Pos automation (5-15 seconds across\n"
                "    the full 0..1 range) gives enough frames per position bucket for a\n"
                "    smooth heatmap.\n";

        case Mode::THDMeasurement:
            return
                "THD Measurement\n"
                "===============\n\n"
                "What it measures\n"
                "  Total Harmonic Distortion: how much harmonic energy the device adds\n"
                "  beyond what's in the input. Reported as a single percentage at the\n"
                "  top and as a per-harmonic bar chart underneath (h1 = fundamental,\n"
                "  h2 = second harmonic, etc., up to h16).\n\n"
                "  The differential model (post^2 - pre^2 per harmonic, then RMS over\n"
                "  h2..hN divided by pre's fundamental amplitude) means the readout is\n"
                "  strictly what the device added - the synth's own residue is subtracted\n"
                "  out. THD% is always the differential value regardless of which view\n"
                "  you select.\n\n"
                "Input\n"
                "  A steady sine tone. Any audible-range frequency works; 1 kHz is the\n"
                "  textbook reference. WTSynth + the harmonics.py script playing a single\n"
                "  harmonic is the easiest way.\n\n"
                "  Differential mode also accepts non-sine inputs (saws, squares, custom\n"
                "  wavetables) - the math correctly isolates added energy regardless of\n"
                "  what's in pre. The bars then show how the device shapes each existing\n"
                "  harmonic.\n\n"
                "Views (Diff / Pre / Post)\n"
                "  Diff: Added energy per harmonic relative to pre's fundamental. The\n"
                "        canonical THD view - shows only what the device introduced.\n"
                "  Pre:  Pre's own harmonics relative to its own fundamental. Sanity-check\n"
                "        the test signal - a clean sine should have everything at the\n"
                "        floor except h1 at 0 dB.\n"
                "  Post: Post's harmonics relative to its own fundamental. Classical THD\n"
                "        view - what the device produces overall.\n\n"
                "Hold / Freeze\n"
                "  Hold: per-bar peak-hold across frames. Useful for catching transient\n"
                "        distortion that flashes briefly. Toggling off resumes live; the\n"
                "        held values stay until you toggle back on, which re-arms peak\n"
                "        accumulation from the next live value.\n"
                "  Freeze: pauses the display so you can study the bars without them\n"
                "        moving. Audio thread keeps running underneath.\n\n"
                "Tips\n"
                "  - Minimum fundamental level is -30 dB FS. Below that the whole panel\n"
                "    shows 'play a single sine tone' rather than reporting garbage.\n"
                "  - For very tonal sources, h1 stays at 0 dB (the reference) and only\n"
                "    h2+ tells you anything diagnostic.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode. The framework (Sweep Position\n"
                "  parameter and header Capture / Clear buttons) is built but the\n"
                "  Capture / Clear buttons are hidden outside Frequency Response.\n\n"
                "  Visual style when added: tile grid (discrete X = harmonic index\n"
                "  h1..h16 as chunky equal-width columns, Y = sweep position 0..1,\n"
                "  colour = dB ratio). Colourmap is monotonic 'more is worse' since\n"
                "  THD harmonics are always below the fundamental: black at -100 dB\n"
                "  (clean) -> green at -40 dB (audible) -> red at -10 dB (severe).\n\n"
                "  Typical use will be: route a DAW automation lane to both WTSynth's\n"
                "  WT Pos and WTAnalyzer's Sweep Position, then read the heatmap as\n"
                "  'which signal characters provoke the most distortion at each\n"
                "  harmonic.'\n\n"
                "Stereo (L / R / Diff)\n"
                "  Per-channel THD is live. Each harmonic slot in the bar\n"
                "  chart subdivides into paired sub-bars: L (master colour\n"
                "  for the current view - chartreuse / red-orange / periwinkle)\n"
                "  and R (the lighter same-family sibling). The L / R / Diff\n"
                "  toggle row in the readout strip controls visibility: L and\n"
                "  R are independent on/off (at least one must stay on); Diff\n"
                "  adds a third whitesmoke sub-bar per harmonic showing\n"
                "  THD_R_dB[h] - THD_L_dB[h]. The big header readout shows L\n"
                "  THD% on top and R THD% below in their respective channel\n"
                "  colours. Hold peak-holds each channel independently.\n";

        case Mode::AliasingDetection:
            return
                "Aliasing Detection\n"
                "==================\n\n"
                "What it measures\n"
                "  Off-grid spectral energy the device under test introduces - i.e.,\n"
                "  energy at frequencies that aren't integer multiples of the test tone\n"
                "  and weren't already present in pre. This isolates aliasing artefacts\n"
                "  produced by nonlinear stages that aren't oversampled enough for the\n"
                "  input they're seeing.\n\n"
                "Input\n"
                "  A high-frequency sine sweep, typically 4 - 20 kHz. The higher the test\n"
                "  fundamental, the closer its harmonics get to Nyquist, the more\n"
                "  aliasing the device will produce if it's prone.\n\n"
                "  Saw or square sweeps fold MORE harmonics into the alias zone at any\n"
                "  given fundamental - they're an aggressive stress test.\n\n"
                "Views (Composite / Pre / Post)\n"
                "  Composite (default): pre spectrum in amber + green differential trace\n"
                "                       showing only the device-added off-grid energy.\n"
                "                       For a transparent device the green is empty and\n"
                "                       composite looks identical to Pre view.\n"
                "  Pre:  the raw pre spectrum alone (amber) - sanity-check the test\n"
                "        signal before judging the device.\n"
                "  Post: the raw post spectrum alone (cyan) - what's coming out of the\n"
                "        device, with no decomposition.\n\n"
                "Hold / Clear\n"
                "  Hold: peak-keep the green differential trace across frames. A sweep\n"
                "        sweeps the alias content across the spectrum over time, so peak\n"
                "        hold accumulates the full picture across one pass.\n"
                "  Clear: wipe the held peak. Useful between test runs - lets you sweep\n"
                "        again from a clean slate.\n\n"
                "Tips\n"
                "  - Compare two effects head-to-head: load a clean reference effect, run\n"
                "    a sweep, observe the differential. Swap to the device under test,\n"
                "    Clear, repeat. The relative green levels show which device aliases\n"
                "    more.\n"
                "  - WTSynth's own interpolation produces residual aliasing visible in\n"
                "    Pre view. The differential subtracts this so the green is strictly\n"
                "    the device's contribution.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode. The Hold/Clear inside the\n"
                "  alias panel are a different feature (peak-hold across a single\n"
                "  sweep at fixed parameter).\n\n"
                "  Visual style when added: smooth heatmap (continuous X = log\n"
                "  frequency, Y = sweep position 0..1, colour = alias residue dB FS).\n"
                "  Colourmap is monotonic 'more is worse' since alias values are\n"
                "  always at or below 0 dB FS: black at -100 dB FS (clean) -> green\n"
                "  at -50 dB FS (moderate alias) -> red at -20 dB FS or higher\n"
                "  (severe alias). 'No measurement' bins stay at the plot-area\n"
                "  background.\n\n"
                "  Typical use will be: route DAW automation to both WTSynth's WT\n"
                "  Pos and WTAnalyzer's Sweep Position to see where in the audible\n"
                "  band aliasing emerges as the input character morphs.\n\n"
                "Stereo (L / R / Diff)\n"
                "  Per-channel alias residue is live. Composite view shows the\n"
                "  pre spectrum plus a per-channel alias trace: chartreuse (L,\n"
                "  master) and green (R), with R drawn underneath and L on top.\n"
                "  Diff (whitesmoke overlay) is alias_R(f) - alias_L(f). The\n"
                "  L / R / Diff toggles in the cursor-readout strip control\n"
                "  trace visibility (L and R independent on/off, at least one\n"
                "  stays on). The peak-residue HUD in the top-right reports\n"
                "  L and R on separate lines so you can see which channel is\n"
                "  worse at a glance. Diff is useful for catching channel-\n"
                "  specific oversampling bugs in stereo distortion plugins.\n";

        case Mode::IMDMeasurement:
            return
                "IMD Measurement\n"
                "===============\n\n"
                "What it measures\n"
                "  Intermodulation Distortion: the products created when two simultaneous\n"
                "  tones pass through a nonlinear stage. Companion to THD - real audio\n"
                "  is multi-tonal, and IMD captures the perceptually offensive distortion\n"
                "  that THD misses.\n\n"
                "  Reports a differential IMD% at top and a per-product bar chart below.\n"
                "  Twelve products covering orders 2 through 4: f1+f2, f1-f2, 2f1+f2,\n"
                "  2f1-f2, f1+2f2, f1-2f2, 3f1+f2, 3f1-f2, 2f1+2f2, 2f1-2f2, f1+3f2,\n"
                "  f1-3f2. Convention: f1 is the lower frequency, f2 the higher.\n\n"
                "Input\n"
                "  Two pure sines played simultaneously. Canonical pairs:\n"
                "    - SMPTE: 60 Hz + 7 kHz, amplitude ratio 4:1 (bass-heavy nonlinearity).\n"
                "    - CCIF:  19 + 20 kHz, equal amplitude (upper-band products).\n"
                "    - DIN:   250 Hz + 8 kHz, equal amplitude.\n"
                "  Any two distinct tones at least 100 Hz apart work. The two_tone.py\n"
                "  script in the scripts folder generates these as WTSynth wavetables.\n\n"
                "Views (Diff / Pre / Post)\n"
                "  Diff: added-energy per product. The canonical IMD view.\n"
                "  Pre:  pre's energy at each product position. For a clean test signal\n"
                "        the bars are at the noise floor.\n"
                "  Post: post's energy at each product position. Shows what the device\n"
                "        produces overall.\n\n"
                "Layout (By Order / By Hz)\n"
                "  By Order: bars at fixed equal-width slots, ordered by |m|+|n|. Easy\n"
                "            to scan: order-2 on the left, order-3 in the middle,\n"
                "            order-4 on the right. Engineer's mental model.\n"
                "  By Hz:    bars positioned at their actual product frequency on a log\n"
                "            axis. Shows where in the audible band the products land -\n"
                "            crucial for SMPTE-style tests where products cluster near f2.\n\n"
                "Hold / Freeze\n"
                "  Same semantics as THD - per-bar peak-hold + display freeze.\n\n"
                "Tips\n"
                "  - WTSynth's mipmap engine attenuates high wavetable harmonics at high\n"
                "    playback pitches. Keep harmonic numbers low and increase playback\n"
                "    pitch to reach high Hz pairs.\n"
                "  - Symmetric clippers (Saturator Analog Clip / Soft Sine) produce\n"
                "    primarily odd-order products. Asymmetric stages (tube biases) show\n"
                "    strong even-order. Cross-check by toggling between modes.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode.\n\n"
                "  Visual style when added: tile grid (discrete X = product index\n"
                "  with formula labels - f1+f2, f1-f2, 2f1+f2, ... - in 12 chunky\n"
                "  equal-width columns, ordered as in By Order layout. Y = sweep\n"
                "  position 0..1, colour = dB ratio). Colourmap is monotonic 'more\n"
                "  is worse': black at -100 dB (clean) -> green at -40 dB (audible)\n"
                "  -> red at -10 dB (severe).\n\n"
                "  Typical use will be: automate one source-side parameter (e.g.\n"
                "  drive amount) alongside WTAnalyzer's Sweep Position to see which\n"
                "  input conditions trigger the worst intermodulation per product.\n\n"
                "Stereo (L / R / Diff)\n"
                "  Per-channel IMD is live. Each product slot in the bar chart\n"
                "  subdivides into paired sub-bars: L (master colour for the\n"
                "  current view) and R (lighter sibling). The L / R / Diff\n"
                "  toggle row in the readout strip controls visibility - L and\n"
                "  R independent on/off (at least one stays on), Diff adds a\n"
                "  whitesmoke sub-bar per product showing IMD_R_dB[p] -\n"
                "  IMD_L_dB[p]. Header readout stacks L IMD% over R IMD% in\n"
                "  their respective colours. Works the same in both By Order\n"
                "  and By Hz layouts.\n";

        case Mode::DirectImpulseIR:
            return
                "Direct Impulse IR\n"
                "=================\n\n"
                "What it measures\n"
                "  The impulse response of the device under test - its complete linear\n"
                "  characterisation in the time domain. This mode obtains the IR via\n"
                "  direct impulse capture: a single-sample impulse is fed in, the post\n"
                "  output IS the impulse response, captures across multiple impulses are\n"
                "  averaged for SNR.\n\n"
                "Input\n"
                "  A periodic impulse train. The impulse.py script generates this as a\n"
                "  WTSynth wavetable - each cycle contains a single 0.95 amplitude sample\n"
                "  surrounded by silence.\n\n"
                "Critical constraint\n"
                "  WTSynth plays the wavetable ONCE PER PLAYBACK CYCLE, so the impulse\n"
                "  rate equals the played MIDI note frequency. At MIDI A2 (110 Hz) you\n"
                "  get 110 impulses per second - period 9 ms. If your IR window is\n"
                "  longer than that period, successive impulse responses overlap and\n"
                "  the average becomes garbage.\n\n"
                "  Rule of thumb: impulse period > IR window. For a 250 ms window,\n"
                "  playback fundamental should be no more than ~4 Hz. For longer reverb\n"
                "  tails this gets impractical with WTSynth - use Farina IR instead.\n\n"
                "Controls\n"
                "  Window: capture length in milliseconds, 50 ms to 120 s. Set to cover\n"
                "          the device's tail length.\n"
                "  Averages: number of impulses to average. Higher = lower noise floor\n"
                "            in the captured IR but longer total test duration.\n"
                "  Clear: wipe the running average. Press between tests.\n"
                "  Export...: save the averaged IR as a 32-bit float stereo WAV at\n"
                "             the current sample rate. The file is drop-in compatible\n"
                "             with any convolution reverb that accepts WAV IRs. Mono\n"
                "             IRs (single-channel capture) are duplicated to both\n"
                "             WAV channels for host compatibility.\n\n"
                "How to read it\n"
                "  Linear time on X (ms or s for longer windows), linear amplitude on Y\n"
                "  centred at zero. The peak at t=0 is the device's instantaneous\n"
                "  response; everything after is the tail / ringing / reverb.\n\n"
                "Tips\n"
                "  - For short-tail effects (EQs, simple distortions), a 20-30 ms window\n"
                "    works at most playback pitches.\n"
                "  - For long-tail reverbs, set the window high and the playback pitch\n"
                "    very low (or switch to Farina IR which doesn't have this problem).\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode.\n\n"
                "  Visual style when added: waterfall plot (smooth, continuous X =\n"
                "  time within the IR in ms, Y = sweep position 0..1, colour =\n"
                "  signed IR amplitude). Colourmap is bipolar around zero since IR\n"
                "  samples swing both positive and negative: deep blue at -1.0\n"
                "  (large negative peak) -> black at 0 (silence) -> warm red at\n"
                "  +1.0 (large positive peak). The Y axis runs bottom = position 0\n"
                "  to top = position 1, so each horizontal stripe is one position's\n"
                "  full IR.\n\n"
                "  Typical use will be: route DAW automation to both a source-side\n"
                "  parameter and WTAnalyzer's Sweep Position, then watch the IR\n"
                "  morph across the sweep - useful for time-varying effects\n"
                "  (modulation, dynamics with input-dependent behaviour, etc.).\n\n"
                "CSD views\n"
                "  The Waveform / Heatmap / 3D selector above the plot switches\n"
                "  between the time-domain IR and its Cumulative Spectral Decay.\n"
                "  CSD slides a short window along the captured IR and FFTs each\n"
                "  position, revealing how energy at each frequency decays over\n"
                "  time - a resonance lingers as a ridge while everything else\n"
                "  drops away. Heatmap draws it as log-frequency X, time Y,\n"
                "  colour = level; 3D draws the classic receding waterfall of\n"
                "  spectra. The L / R toggle picks which channel's CSD is\n"
                "  shown. CSD spans the whole captured IR, so a shorter Window\n"
                "  gives finer detail on the early decay.\n"
                "  Interaction - Heatmap: drag to zoom (drag in the time or\n"
                "  frequency gutter to zoom that axis only), double-click to\n"
                "  reset. 3D: drag to orbit the view; the magnifier\n"
                "  buttons (or scroll wheel) zoom; the arrow cross pans\n"
                "  the view laterally and vertically; double-click resets\n"
                "  the camera.\n\n"
                "Stereo (L / R / Diff)\n"
                "  Per-channel IR is live. Each channel runs its own trigger\n"
                "  detector, capture state machine, and incremental averager.\n"
                "  The waveform plot overlays both traces with R drawn first\n"
                "  (chartreuse), L on top (chartreuse-master) per the stereo\n"
                "  convention - mono signals overlap pixel-for-pixel and read\n"
                "  as pure master. The header readout shows separate L and R\n"
                "  'captures averaged' counts so you can see if one channel\n"
                "  is mis-triggering. Diff (whitesmoke overlay) shows IR_R(t)\n"
                "  - IR_L(t) per sample - useful for stereo reverbs and any\n"
                "  device with channel-dependent processing.\n";

        case Mode::FarinaIR:
            return
                "Farina IR\n"
                "=========\n\n"
                "What it measures\n"
                "  Same output as Direct Impulse IR - a time-domain impulse response of\n"
                "  the device - but acquired via Farina log-sweep deconvolution rather\n"
                "  than direct impulse capture. The device is fed a known log sine\n"
                "  sweep; the captured post output is deconvolved against the\n"
                "  mathematically-generated inverse-sweep filter; the result is the IR.\n\n"
                "Why use this instead of Direct Impulse IR\n"
                "  - A sweep delivers far more total test energy than a single impulse,\n"
                "    so SNR is enormous without needing to average.\n"
                "  - The sweep distributes excitation across the entire spectrum, so the\n"
                "    device is properly tested at every frequency.\n"
                "  - The wavetable model can't deliver clean discrete impulses, but it\n"
                "    can deliver clean log sweeps. For WTSynth-driven testing this is\n"
                "    the practical IR measurement path.\n\n"
                "Input\n"
                "  A log sine sweep from f0 to f1 over the configured duration. The\n"
                "  chirp.py script in the scripts folder produces these as WTSynth\n"
                "  wavetables - set Type=Log (Farina), Start and End to match your\n"
                "  intended f0 / f1.\n\n"
                "  CRITICAL: the parameters configured in the Farina panel must match\n"
                "  the parameters of the actual sweep you play. The deconvolution math\n"
                "  uses these values to construct the inverse filter; mismatched\n"
                "  parameters give a garbage IR.\n\n"
                "Controls\n"
                "  f0:    sweep start frequency in Hz.    (in the Farina panel)\n"
                "  f1:    sweep end frequency in Hz.      (in the Farina panel)\n"
                "  Sweep: sweep duration in seconds.      (in the Farina panel)\n"
                "  Tail:  additional capture time after the sweep ends; equals the\n"
                "         length of the resulting IR. Set to cover the device's\n"
                "         decay tail. (in the Farina panel)\n"
                "  Capture: arms the trigger. The audio thread watches pre for the\n"
                "           sweep onset, then records for sweep + tail seconds. When\n"
                "           the recording completes, the message thread deconvolves\n"
                "           the post against the inverse sweep and the IR appears in\n"
                "           the plot.  (in the plugin header, left of the Load\n"
                "           Sidecar button)\n"
                "  Clear: wipes the captured IR. Click before re-arming a new\n"
                "         capture.  (in the plugin header, left of the Load Sidecar\n"
                "         button)\n"
                "  Export...: save the deconvolved IR as a 32-bit float stereo WAV\n"
                "             at the current sample rate. The file is drop-in\n"
                "             compatible with any convolution reverb that accepts\n"
                "             WAV IRs. Mono IRs (single-channel capture) are\n"
                "             duplicated to both WAV channels. (top-right of the\n"
                "             Farina panel, only effective after Status reads 'IR\n"
                "             ready')\n\n"
                "Status\n"
                "  Idle:    no capture in progress; click Capture to arm.\n"
                "  Waiting: pre threshold not yet crossed.\n"
                "  Capturing X / Y: recording, showing progress in samples.\n"
                "  Processing: FFT deconvolution running.\n"
                "  IR ready: result is in the plot.\n\n"
                "Tips\n"
                "  - The first Capture click after entering Farina mode (or changing\n"
                "    sweep/tail params beyond previous capacity) allocates buffers and\n"
                "    builds the FFT. This takes a fraction of a second; subsequent\n"
                "    captures with the same params are instant.\n"
                "  - Standard log sweeps used in measurement are 10-30 seconds covering\n"
                "    20 Hz to 20 kHz. For very long reverbs increase Tail.\n"
                "  - The IR plot's Y axis auto-scales to peak; for short-tailed effects\n"
                "    the peak at t=0 dominates and the tail looks small; zoom or use\n"
                "    Clear + adjust parameters if you want to see the tail in detail.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode. The Capture / Clear buttons\n"
                "  in the header here trigger the Farina deconvolution itself - they\n"
                "  are NOT the 2D sweep capture controls (those are FR-only today).\n\n"
                "  Visual style when added: waterfall plot, identical to Direct\n"
                "  Impulse IR's planned 2D view. Smooth heatmap with X = time\n"
                "  within the IR (ms), Y = sweep position 0..1, colour = signed IR\n"
                "  amplitude (bipolar: blue for large negatives, black at zero,\n"
                "  red for large positives). Each horizontal stripe is one\n"
                "  position's full IR.\n\n"
                "  You'd run multiple Farina captures across automated source-side\n"
                "  parameter values to map how the IR shape changes with signal\n"
                "  character.\n\n"
                "CSD views\n"
                "  The Waveform / Heatmap / 3D selector above the plot switches\n"
                "  between the time-domain IR and its Cumulative Spectral Decay.\n"
                "  CSD slides a short window along the deconvolved IR and FFTs\n"
                "  each position, revealing how energy at each frequency decays\n"
                "  over time - a resonance lingers as a ridge while everything\n"
                "  else drops away. Heatmap draws it as log-frequency X, time Y,\n"
                "  colour = level; 3D draws the classic receding waterfall of\n"
                "  spectra. The L / R toggle picks which channel's CSD is\n"
                "  shown. CSD spans the whole captured IR; a shorter Tail\n"
                "  concentrates the slices on the early decay.\n"
                "  Interaction - Heatmap: drag to zoom (drag in the time or\n"
                "  frequency gutter to zoom that axis only), double-click to\n"
                "  reset. 3D: drag to orbit the view; the magnifier\n"
                "  buttons (or scroll wheel) zoom; the arrow cross pans\n"
                "  the view laterally and vertically; double-click resets\n"
                "  the camera.\n\n"
                "Stereo (L / R / Diff)\n"
                "  Per-channel Farina IR is live. Each channel runs its own\n"
                "  trigger and capture; both channels deconvolve against the\n"
                "  same mathematically-generated inverse-sweep filter (the\n"
                "  filter is parameter-driven, not signal-driven, so it is\n"
                "  identical for L and R). Resulting IRs overlay with R\n"
                "  drawn first, L on top. The L / R / Diff toggle row in\n"
                "  the readout strip controls visibility - L and R\n"
                "  independent on/off (at least one stays on), Diff is a\n"
                "  whitesmoke overlay of IR_R(t) - IR_L(t) per sample.\n"
                "  Diff exposes channel-specific reverb decay or modulation\n"
                "  behaviour.\n";

        case Mode::StereoImage:
            return
                "Stereo Image\n"
                "============\n\n"
                "What it measures\n"
                "  The home for stereo-specific analysis. A view selector picks\n"
                "  the visualisation: Divergence, Correlation, and\n"
                "  Goniometer - all shipped.\n\n"
                "Input\n"
                "  Any broadband signal - pink noise, a sweep, or wavetable content\n"
                "  that excites the spectrum. The richer the input's frequency\n"
                "  coverage, the more of the divergence curve is measurable.\n\n"
                "Divergence view\n"
                "  Per-frequency device-added stereo divergence: how much the\n"
                "  device under test made the right and left channels differ.\n"
                "  For each channel the device's effect is FR_x = post_x - pre_x;\n"
                "  the plotted value is |FR_R - FR_L| (how much the channels were\n"
                "  decorrelated) signed toward the channel the device acted on\n"
                "  MORE. A cut or a boost on the right channel both read upward\n"
                "  (R) - boost-vs-cut polarity is deliberately ignored, only\n"
                "  'how much, and to which channel' matters.\n\n"
                "How to read it\n"
                "  The green centre line is zero divergence - the device left the\n"
                "  stereo image untouched. The trace lifts UP into lime where the\n"
                "  device acted on the right channel, DOWN into mint where it\n"
                "  acted on the left. The Y axis is bipolar dB ('dB R' above, 'dB\n"
                "  L' below). A stereo-transparent device reads a flat green line\n"
                "  no matter how stereo the input already was - the FR-difference\n"
                "  cancels the input's own stereo content.\n\n"
                "  This is a level-divergence meter, not phase correlation - it\n"
                "  shows where and how much a device skews the stereo balance (a\n"
                "  mid-side EQ move, a one-channel boost, a frequency-dependent\n"
                "  widener). Phase correlation is the Correlation view.\n\n"
                "Correlation view\n"
                "  Per-frequency phase correlation of the post signal's left and\n"
                "  right channels, from their cross-spectrum. The Y axis runs +1\n"
                "  (the channels are in phase at that frequency - mono-safe),\n"
                "  through 0 (decorrelated - wide but mono-fold loses energy),\n"
                "  down to -1 (anti-phase - those frequencies cancel on a mono\n"
                "  sum). Cross and auto spectra are averaged over a longer window\n"
                "  than Divergence so the estimate is stable. Bins with no post\n"
                "  signal break the trace. Unlike Divergence this reads the post\n"
                "  signal itself, not a pre/post difference. A 'Broadband'\n"
                "  readout in the corner sums the cross / auto spectra across\n"
                "  the band into one energy-weighted figure - it tracks the\n"
                "  classic phase meter (loud frequencies dominate).\n\n"
                "Goniometer view\n"
                "  A time-domain L-vs-R XY scope (Lissajous). Each sample\n"
                "  pair is projected onto rotated mid / side axes: the M\n"
                "  (mono) axis is vertical, the S (side) axis horizontal,\n"
                "  and the L and R channel axes lie on the 45-degree\n"
                "  diagonals. A mono signal collapses to a vertical line on\n"
                "  M; a hard-panned signal rides its diagonal; an anti-phase\n"
                "  pair spreads horizontally along S (a mono-sum cancellation\n"
                "  warning). The cloud shows ~43 ms of signal, brightening\n"
                "  where samples pile up.\n"
                "  Two modes (toggle at the right of the header):\n"
                "    Pre / Post  - overlays the input cloud (pre colour) and\n"
                "      the output cloud (post colour); the device's effect on\n"
                "      the stereo image is the visible delta between them.\n"
                "    Difference  - scopes (post - aligned pre) per channel:\n"
                "      the stereo image of the signal the device ADDED, the\n"
                "      time-domain analog of the Divergence view. A stereo-\n"
                "      transparent device collapses this cloud to the origin.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode. When added, the heatmap X\n"
                "  axis would be log frequency, Y the sweep position, colour the\n"
                "  signed divergence (bipolar - one channel's colour for each\n"
                "  direction).\n\n"
                "Stereo (L / R / Diff)\n"
                "  This entire mode IS the stereo analysis - every view is\n"
                "  inherently a stereo comparison, so the shared L / R / Diff\n"
                "  toggle row is hidden here. The panel's own selector picks the\n"
                "  visualisation instead.\n";

        case Mode::ParameterSweep:
            return
                "Parameter Sweep\n"
                "===============\n\n"
                "What it measures\n"
                "  A headline metric versus a swept parameter - the Plugin\n"
                "  Doctor style precision plot. Two header selectors: the\n"
                "  metric (THD% or IMD%) and the view (Line or Heatmap).\n"
                "  Line is a 1D X-Y curve - X the sweep position, Y the\n"
                "  metric scalar. Heatmap shows the metric's full per-\n"
                "  harmonic / per-product distribution: X the harmonic or\n"
                "  product, Y the sweep position, colour the level.\n\n"
                "Input\n"
                "  THD% expects a clean sine; IMD% expects a two-tone signal -\n"
                "  the same inputs those modes need. The device parameter you\n"
                "  want on the X axis is swept by the source/host.\n\n"
                "How to drive it\n"
                "  Automate WTAnalyzer's Sweep Position parameter (0..1) from\n"
                "  the DAW, and route the SAME automation to the parameter you\n"
                "  want to characterise - the source plugin's WT Pos, an\n"
                "  effect's drive or cutoff, etc. Arm Capture; the curve fills\n"
                "  in as the automation plays. Capture records only while the\n"
                "  transport is playing; Clear starts a fresh capture.\n"
                "  Because X is just the 0..1 lane, 'THD% vs frequency' and\n"
                "  'THD% vs drive' are the same capture - the difference is\n"
                "  only what you routed the automation to.\n\n"
                "Shaping the sweep\n"
                "  Use a slow ramp - 40 to 60 seconds end to end. A fast\n"
                "  sweep lands only about one measurement frame per bucket\n"
                "  and the curve looks jagged; a slow ramp gives each bucket\n"
                "  many frames to average into a clean curve.\n"
                "  Hold the automation still for about a second at each end,\n"
                "  before and after the ramp. Each measurement is a ~170 ms\n"
                "  window average, so the minimum and maximum values read\n"
                "  correctly only while the parameter sits still - the holds\n"
                "  give the capture a settled signal at each extreme.\n\n"
                "How to read it\n"
                "  Line view: green (L) and lime (R) traces are the captured\n"
                "  curve per channel; a faint vertical line marks the current\n"
                "  sweep position with live dots; the Y axis auto-ranges in\n"
                "  percent. Heatmap view: each column is one harmonic (THD)\n"
                "  or product (IMD), each row a sweep position (0 at the\n"
                "  bottom), colour the differential level - so you see which\n"
                "  harmonics grow where across the sweep. The capture feeds\n"
                "  both views at once, so the Line / Heatmap toggle switches\n"
                "  freely with no re-capture.\n\n"
                "  Switching the metric clears the capture - THD% and IMD%\n"
                "  are different units.\n\n"
                "Stereo (L / R / Diff)\n"
                "  The Line view draws both channels. The Heatmap shows the\n"
                "  L channel (a heatmap cannot overlay L and R). The shared\n"
                "  L / R / Diff toggle row is hidden here.\n";

        case Mode::PhaseResponse:
            return
                "Phase Response\n"
                "==============\n\n"
                "What it measures\n"
                "  The phase side of the transfer function - the companion to\n"
                "  the magnitude shown in Frequency Response mode. A header\n"
                "  selector picks the sub-view: Phase or Group Delay.\n\n"
                "Input\n"
                "  Any broadband signal - pink noise, a sweep, multitone.\n"
                "  Phase is only measurable where the input has energy; the\n"
                "  richer the frequency coverage, the more of the curve is\n"
                "  filled in.\n\n"
                "Phase view\n"
                "  Per-frequency phase difference between post and pre,\n"
                "  formed from the cross-spectrum and drawn as a wrapped\n"
                "  +/-180 degree curve over log frequency. The bulk\n"
                "  linear-phase component - the straight ramp that any pure\n"
                "  delay adds - is removed automatically by a best-fit line,\n"
                "  so the curve shows the device's actual phase distortion\n"
                "  regardless of how much latency sits in the path. You do\n"
                "  NOT need to null latency first for this view.\n\n"
                "Group Delay view\n"
                "  The negated slope of the unwrapped phase, in milliseconds,\n"
                "  over log frequency. Unlike the Phase view this is NOT\n"
                "  detrended - it shows true delay, so a flat trace away from\n"
                "  zero means a constant delay, and bumps mean\n"
                "  frequency-dependent time smearing. Running the latency\n"
                "  auto-measure first re-centres it near zero.\n\n"
                "Phase vs Group Delay\n"
                "  The two views are one measurement in two forms: group\n"
                "  delay is the slope of the phase curve, recast from degrees\n"
                "  into time. Use the Phase view to see the raw angular\n"
                "  distortion shape; use Group Delay to see how much each\n"
                "  frequency region is delayed in time - the view that tells\n"
                "  you whether the device smears transients. Group Delay\n"
                "  normally looks busier than Phase: a derivative amplifies\n"
                "  fast variation (and noise), so a gently wavy phase curve\n"
                "  can have a lively group-delay curve. That is expected,\n"
                "  not a fault.\n\n"
                "How to read it\n"
                "  Green (L) and lime (R) traces are the per-channel curve.\n"
                "  A linear-phase device reads a flat line in both views; a\n"
                "  minimum-phase EQ shows phase swing around its corners and\n"
                "  a group-delay bump there; an all-pass shows phase motion\n"
                "  with no magnitude change in FR mode.\n\n"
                "2D Sweep Capture\n"
                "  Not yet implemented for this mode. When added, the heatmap\n"
                "  X axis would be log frequency, Y the sweep position,\n"
                "  colour the phase or group delay (bipolar).\n\n"
                "Stereo (L / R)\n"
                "  The shared L / R toggles pick which channels draw, the\n"
                "  same as the other 1D-trace modes; group delay auto-ranges\n"
                "  to the visible channels. There is no Diff toggle - phase\n"
                "  here is per-channel device behaviour, not a stereo\n"
                "  difference. The inline readout shows per-channel RMS\n"
                "  phase deviation.\n";

        case Mode::Dynamics:
            return
                "Dynamics\n"
                "========\n\n"
                "What it measures\n"
                "  The device's static transfer curve: input level (X) versus\n"
                "  output level (Y), both in dB. Each processed block drops\n"
                "  one point - its pre-effect RMS level paired with its\n"
                "  post-effect RMS level - into the curve. This is the shape\n"
                "  that reveals compression, expansion, gating and limiting.\n\n"
                "Input\n"
                "  A slow amplitude ramp through the device. Start near\n"
                "  silence and rise smoothly to full level (or the reverse)\n"
                "  over several seconds, so every input-level bin gets\n"
                "  visited. A sine tone whose amplitude is automated works\n"
                "  well; sustained tones give the cleanest RMS reading. Sweep\n"
                "  slowly - the curve fills in as the ramp walks the input\n"
                "  axis, and each bin averages every block that lands in it.\n\n"
                "  Keep all device parameters fixed - only the input\n"
                "  amplitude should change. Sweeping a device control (a\n"
                "  saturator's drive, a compressor's threshold) at the same\n"
                "  time overlays several different transfer curves onto one\n"
                "  plot, and each parameter change shows up as a step or\n"
                "  spike in the accumulated trace.\n\n"
                "How to read it\n"
                "  The faint diagonal is the unity line - output equals\n"
                "  input. Where the curve sits ON the diagonal the device is\n"
                "  pass-through at that level. Curve BELOW the diagonal at\n"
                "  high input = downward compression or limiting (a knee\n"
                "  then a shallow slope). Curve below the diagonal at LOW\n"
                "  input = expansion or gating (output collapses as input\n"
                "  drops below the threshold). A straight line parallel to\n"
                "  the diagonal but offset is plain make-up or trim gain.\n\n"
                "Controls\n"
                "  Clear: wipes the accumulated curve. The curve builds up\n"
                "         continuously while the mode is active - there is no\n"
                "         arm step - so Clear before re-running a ramp if you\n"
                "         want a fresh trace. (in the panel header)\n\n"
                "2D Sweep Capture\n"
                "  Not applicable in this mode. Dynamics already IS a sweep -\n"
                "  the input-level axis is its swept dimension - so there is\n"
                "  no second parameter to bucket against. The Sweep Position\n"
                "  parameter has no effect here and the Capture / Clear sweep\n"
                "  header buttons are hidden; the panel's own Clear button\n"
                "  resets the curve.\n\n"
                "Stereo (L / R)\n"
                "  The shared L / R toggles pick which channels draw, the\n"
                "  same as the other 1D-trace modes. L is mint, R is lime.\n"
                "  Each channel bins its own pre/post level pair, so a\n"
                "  device that compresses the two channels differently\n"
                "  shows two distinct curves. There is no Diff toggle -\n"
                "  the transfer curve is per-channel device behaviour, not\n"
                "  a stereo difference.\n";
    }
    return "No help available for this mode.";
}


void WTAnalyzerAudioProcessorEditor::timerCallback()
{
    const int current = (int) *audioProcessor.apvts.getRawParameterValue ("activeAnalysis");
    if (current != lastAppliedAnalysisMode)
        applyAnalysisMode (current);

    updateImbalanceReadout();

    // Surface the sidechain-not-connected notice over whatever panel is
    // active. WTAnalyzer can't do its pre-vs-post job without it, so the
    // alert is mode-independent.
    sidechainNotice.setVisible (! audioProcessor.preBusActive.load (std::memory_order_relaxed));
}

void WTAnalyzerAudioProcessorEditor::updateImbalanceReadout()
{
    if (! imbalanceReadout.isVisible()) return;

    // In Diff mode the trace itself already shows the asymmetry visually,
    // but the numerical summary stays useful for screenshots / reports
    // and for the L+R views where the diff isn't drawn.
    const auto c = computeImbalanceContent();
    imbalanceReadout.setContent (c.metric, c.lText, c.lColour, c.rText, c.rColour);
}

WTAnalyzerAudioProcessorEditor::ImbalanceContent
WTAnalyzerAudioProcessorEditor::computeImbalanceContent() const
{
    using Mode = WTAnalyzerAudioProcessor::AnalysisMode;
    const int mode = (int) *audioProcessor.apvts.getRawParameterValue ("activeAnalysis");

    // Stable summary metrics per mode: a single scalar per channel that
    // changes slowly even with complex signals. Avoids the "max diff at Hz"
    // pattern which flickered between bin peaks frame-to-frame on
    // broadband content. The user reads L and R side-by-side and notices
    // divergence at a glance; the Diff button is for drilling into the
    // frequency / harmonic distribution of that divergence.

    auto db1   = [] (float v) -> juce::String { return juce::String (v, 1) + " dB"; };
    auto pct3  = [] (float v) -> juce::String { return juce::String (v, 3) + "%";  };
    auto val4  = [] (float v) -> juce::String { return juce::String (v, 4);        };

    // Mean dB across valid bins of a sparse-with-sentinel array.
    auto sparseMeanDb = [&] (const float* arr, int count, float sentinelFloor) -> float
    {
        float sum = 0.0f;
        int   n = 0;
        for (int bin = 1; bin < count; ++bin)
        {
            const float v = arr[bin];
            if (v <= sentinelFloor + 0.5f) continue;
            sum += v;
            ++n;
        }
        return n > 0 ? sum / (float) n : sentinelFloor;
    };

    // Peak dB across valid bins of a sparse-with-sentinel array.
    auto sparsePeakDb = [] (const float* arr, int count, float sentinelFloor) -> float
    {
        float peak = sentinelFloor;
        for (int bin = 1; bin < count; ++bin)
        {
            const float v = arr[bin];
            if (v <= sentinelFloor + 0.5f) continue;
            if (v > peak) peak = v;
        }
        return peak;
    };

    // RMS amplitude of a captured IR buffer. Strided iteration so long
    // windows (up to 120 s = 5.76M samples at 48 kHz) don't burn the
    // message thread; RMS of a strided sample is a faithful approximation
    // since IR amplitudes are smooth over many adjacent samples.
    auto irRms = [] (const std::vector<float>& buf, int n) -> float
    {
        if (n <= 0) return 0.0f;
        const int stride = juce::jmax (1, n / 10000);
        double sumSq = 0.0;
        int    count = 0;
        for (int i = 0; i < n; i += stride)
        {
            const double s = (double) buf[(size_t) i];
            sumSq += s * s;
            ++count;
        }
        return count > 0 ? (float) std::sqrt (sumSq / (double) count) : 0.0f;
    };

    // Every analysis-output metric uses the analysis colours.
    const juce::Colour anL = WTColors::analysis;
    const juce::Colour anR = WTColors::analysis_R;

    ImbalanceContent c;

    switch (mode)
    {
        case (int) Mode::FrequencyResponse:
        {
            const auto& fl = audioProcessor.frequencyResponse.getResponseDb();
            const auto& fr = audioProcessor.frequencyResponse.getResponseDb_R();
            const float l = sparseMeanDb (fl.data(), (int) fl.size(),
                                          FrequencyResponse::kNoMeasurementDb);
            const float r = sparseMeanDb (fr.data(), (int) fr.size(),
                                          FrequencyResponse::kNoMeasurementDb);
            c.metric = "Avg gain across spectrum";
            if (l <= FrequencyResponse::kNoMeasurementDb + 1.0f
                && r <= FrequencyResponse::kNoMeasurementDb + 1.0f)
            {
                c.lText = "no measurement";
            }
            else
            {
                c.lText = "L " + db1 (l);   c.lColour = anL;
                c.rText = "R " + db1 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::AliasingDetection:
        {
            const auto& pl = audioProcessor.aliasingDetection.getPeakDifferentialDb();
            const auto& pr = audioProcessor.aliasingDetection.getPeakDifferentialDb_R();
            const float l = sparsePeakDb (pl.data(), (int) pl.size(),
                                          AliasingDetection::kNoMeasurementDb);
            const float r = sparsePeakDb (pr.data(), (int) pr.size(),
                                          AliasingDetection::kNoMeasurementDb);
            c.metric = "Peak alias residue";
            if (l <= AliasingDetection::kNoMeasurementDb + 1.0f
                && r <= AliasingDetection::kNoMeasurementDb + 1.0f)
            {
                c.lText = "no residue detected";
            }
            else
            {
                c.lText = "L " + db1 (l);   c.lColour = anL;
                c.rText = "R " + db1 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::THDMeasurement:
        {
            const auto& thd = audioProcessor.thdMeasurement;
            const bool vL = thd.isValid (THDMeasurement::Channel::L);
            const bool vR = thd.isValid (THDMeasurement::Channel::R);
            c.metric = "Total THD";
            if (! vL && ! vR)
            {
                c.lText = "no signal";
            }
            else
            {
                const float l = thd.getTotalThdPercent (THDMeasurement::Channel::L);
                const float r = thd.getTotalThdPercent (THDMeasurement::Channel::R);
                c.lText = "L " + pct3 (l);   c.lColour = anL;
                c.rText = "R " + pct3 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::IMDMeasurement:
        {
            const auto& imd = audioProcessor.imdMeasurement;
            const bool vL = imd.isValid (IMDMeasurement::Channel::L);
            const bool vR = imd.isValid (IMDMeasurement::Channel::R);
            c.metric = "Total IMD";
            if (! vL && ! vR)
            {
                c.lText = "no signal";
            }
            else
            {
                const float l = imd.getTotalImdPercent (IMDMeasurement::Channel::L);
                const float r = imd.getTotalImdPercent (IMDMeasurement::Channel::R);
                c.lText = "L " + pct3 (l);   c.lColour = anL;
                c.rText = "R " + pct3 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::DirectImpulseIR:
        {
            const auto& ir = audioProcessor.impulseResponse;
            const int nL = ir.getDisplayLength (ImpulseResponse::Channel::L);
            const int nR = ir.getDisplayLength (ImpulseResponse::Channel::R);
            c.metric = "Captured IR RMS level";
            if (nL <= 0 && nR <= 0)
            {
                c.lText = "no capture";
            }
            else
            {
                const float l = irRms (ir.getAveragedBuffer (ImpulseResponse::Channel::L), nL);
                const float r = irRms (ir.getAveragedBuffer (ImpulseResponse::Channel::R), nR);
                c.lText = "L " + val4 (l);   c.lColour = anL;
                c.rText = "R " + val4 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::FarinaIR:
        {
            const auto& f = audioProcessor.farinaIR;
            const int nL = f.getIRLength (FarinaIR::Channel::L);
            const int nR = f.getIRLength (FarinaIR::Channel::R);
            c.metric = "Captured IR RMS level";
            if (nL <= 0 && nR <= 0)
            {
                c.lText = "no capture";
            }
            else
            {
                const float l = irRms (f.getIR (FarinaIR::Channel::L), nL);
                const float r = irRms (f.getIR (FarinaIR::Channel::R), nR);
                c.lText = "L " + val4 (l);   c.lColour = anL;
                c.rText = "R " + val4 (r);   c.rColour = anR;
            }
            break;
        }

        case (int) Mode::PhaseResponse:
        {
            // RMS of the detrended phase across the band, per channel - a
            // stable single-number summary of how much phase distortion
            // each channel carries. Sentinel bins are skipped.
            auto rmsDeg = [] (const std::vector<float>& arr) -> float
            {
                double sumSq = 0.0;
                int    n     = 0;
                for (int bin = 1; bin < (int) arr.size(); ++bin)
                {
                    const float v = arr[(size_t) bin];
                    if (v <= -1.0e8f) continue;          // kNoMeasurement
                    sumSq += (double) v * (double) v;
                    ++n;
                }
                return n > 0 ? (float) std::sqrt (sumSq / (double) n) : -1.0f;
            };

            const float l = rmsDeg (audioProcessor.phaseResponse.getPhaseDegrees (
                                        PhaseResponse::Channel::L));
            const float r = rmsDeg (audioProcessor.phaseResponse.getPhaseDegrees (
                                        PhaseResponse::Channel::R));
            c.metric = "RMS phase deviation";
            if (l < 0.0f && r < 0.0f)
            {
                c.lText = "no measurement";
            }
            else
            {
                c.lText = "L " + juce::String (juce::jmax (0.0f, l), 1) + " deg";
                c.lColour = anL;
                c.rText = "R " + juce::String (juce::jmax (0.0f, r), 1) + " deg";
                c.rColour = anR;
            }
            break;
        }

        case (int) Mode::Dynamics:
        {
            // Peak gain change across the captured transfer curve: the
            // bin whose output-minus-input deviation is largest in
            // magnitude, sign preserved. Negative reads as the deepest
            // compression / gating; positive as net make-up gain.
            auto peakGainChange = [&] (bool rightChannel) -> float
            {
                const auto& dc = audioProcessor.dynamicsCurve;
                float peak = 0.0f;
                bool  any  = false;
                for (int b = 0; b < dc.getNumBins(); ++b)
                {
                    const float out = rightChannel ? dc.getOutputDbR (b)
                                                   : dc.getOutputDbL (b);
                    if (out == DynamicsCurve::kNoData) continue;
                    const float change = out - DynamicsCurve::binInputDb (b);
                    if (! any || std::abs (change) > std::abs (peak))
                        peak = change;
                    any = true;
                }
                return any ? peak : DynamicsCurve::kNoData;
            };

            const float l = peakGainChange (false);
            const float r = peakGainChange (true);
            c.metric = "Peak gain change";
            if (l == DynamicsCurve::kNoData && r == DynamicsCurve::kNoData)
            {
                c.lText = "no measurement";
            }
            else
            {
                if (l != DynamicsCurve::kNoData)
                { c.lText = "L " + db1 (l);   c.lColour = anL; }
                if (r != DynamicsCurve::kNoData)
                { c.rText = "R " + db1 (r);   c.rColour = anR; }
            }
            break;
        }
    }

    return c;
}

void WTAnalyzerAudioProcessorEditor::applyAnalysisMode (int modeIndex)
{
    lastAppliedAnalysisMode = modeIndex;

    using Mode = WTAnalyzerAudioProcessor::AnalysisMode;
    const bool wantsSpectrumPath = (modeIndex == (int) Mode::FrequencyResponse
                                  || modeIndex == (int) Mode::AliasingDetection);
    const bool wantsThdPath      = (modeIndex == (int) Mode::THDMeasurement);
    const bool wantsImdPath      = (modeIndex == (int) Mode::IMDMeasurement);
    const bool wantsIrPath       = (modeIndex == (int) Mode::DirectImpulseIR);
    const bool wantsFarinaPath   = (modeIndex == (int) Mode::FarinaIR);
    const bool wantsStereoPath   = (modeIndex == (int) Mode::StereoImage);
    const bool wantsSweepPath    = (modeIndex == (int) Mode::ParameterSweep);
    const bool wantsPhasePath    = (modeIndex == (int) Mode::PhaseResponse);
    const bool wantsDynamicsPath = (modeIndex == (int) Mode::Dynamics);

    spectrumDisplay  .setVisible (wantsSpectrumPath);
    cursorReadout    .setVisible (wantsSpectrumPath);
    thdDisplay       .setVisible (wantsThdPath);
    imdDisplay       .setVisible (wantsImdPath);
    impulseDisplay   .setVisible (wantsIrPath);
    farinaDisplay    .setVisible (wantsFarinaPath);
    stereoDisplay    .setVisible (wantsStereoPath);
    sweepCurveDisplay.setVisible (wantsSweepPath);
    phaseDisplay     .setVisible (wantsPhasePath);
    dynamicsDisplay  .setVisible (wantsDynamicsPath);

    // The alias-view toggle row lives inside SpectrumDisplay (it's tied to
    // a specific mode within the shared spectrum panel) but its visibility
    // is mode-driven from the editor.
    spectrumDisplay.setAliasingViewButtonsVisible (modeIndex == (int) Mode::AliasingDetection);

    // Capture / Clear in the header swap between the sweep recorder and
    // the Farina IR one-shot trigger based on the active mode. The sweep
    // recorder pair serves both the FR 2D heatmap and the Parameter
    // Sweep curve - same `sweepCaptureActive` arm gesture for both.
    const bool isFarinaMode = (modeIndex == (int) Mode::FarinaIR);
    const bool wantsSweepButtons = (modeIndex == (int) Mode::FrequencyResponse)
                                 || wantsSweepPath;
    sweepCaptureButton .setVisible (wantsSweepButtons);
    sweepClearButton   .setVisible (wantsSweepButtons);
    farinaCaptureButton.setVisible (isFarinaMode);
    farinaClearButton  .setVisible (isFarinaMode);

    // L / R channel toggles: visible in every mode that shows per-channel
    // data. The Diff toggle is restricted to the bar / waveform modes
    // (THD, IMD, IR, Farina) where per-harmonic / per-product / per-sample
    // R-L difference is genuinely mode-specific. The 1D-trace modes
    // (FR, Aliasing, Phase Response, Dynamics) have L / R only -
    // dedicated stereo-difference analysis lives in the Stereo Image mode.
    const bool wantsStereoToggles = wantsSpectrumPath || wantsThdPath
                                  || wantsImdPath || wantsIrPath || wantsFarinaPath
                                  || wantsPhasePath || wantsDynamicsPath;
    const bool wantsDiffToggle    = wantsThdPath || wantsImdPath
                                  || wantsIrPath || wantsFarinaPath;

    // Entering an L/R-only mode while Diff was left engaged from a bar /
    // waveform mode would leave L and R both off (Diff is exclusive),
    // blanking the display. Restore the L+R view first.
    if ((wantsSpectrumPath || wantsPhasePath || wantsDynamicsPath)
        && stereoDiffButton.getToggleState())
        stereoDiffButton.setToggleState (false, juce::sendNotification);

    stereoLButton    .setVisible (wantsStereoToggles);
    stereoRButton    .setVisible (wantsStereoToggles);
    stereoDiffButton .setVisible (wantsDiffToggle);
    imbalanceReadout .setVisible (wantsStereoToggles);

    repaint();
}

//==============================================================================
void WTAnalyzerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202225));

    // Title: drawn in the header row, with space reserved on the right
    // for the sweep capture/clear buttons, sidecar button, analysis
    // selector, and help button (all positioned by resized()).
    auto bounds = getLocalBounds().reduced (sx (16));
    auto headerRow = bounds.removeFromTop (sx (24));

    headerRow.removeFromRight (sx (62) + sx (4) + sx (50) + sx (8) + sx (130) + sx (8) + sx (200) + sx (8) + sx (30) + sx (8));

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText ("WTAnalyzer", headerRow, juce::Justification::centredLeft);
}

void WTAnalyzerAudioProcessorEditor::resized()
{
    const float s = scale();
    lookAndFeel.setUiScale (s);
    spectrumDisplay .setUiScale (s);
    cursorReadout   .setUiScale (s);
    thdDisplay      .setUiScale (s);
    imdDisplay      .setUiScale (s);
    impulseDisplay  .setUiScale (s);
    farinaDisplay   .setUiScale (s);
    stereoDisplay   .setUiScale (s);
    sweepCurveDisplay.setUiScale (s);
    phaseDisplay    .setUiScale (s);
    dynamicsDisplay .setUiScale (s);
    sidechainNotice .setUiScale (s);
    levelMetersPanel.setUiScale (s);
    latencyPanel    .setUiScale (s);

    auto bounds = getLocalBounds().reduced (sx (16));

    // Header row: title (drawn by paint()) on the left, then right-to-left:
    // help, mode selector, sidecar, clear, capture. Capture / Clear are
    // mode-driven (visible only in FR mode) so they leave a gap in other
    // modes - the rest of the header layout stays put.
    auto headerRow = bounds.removeFromTop (sx (24));
    helpButton      .setBounds (headerRow.removeFromRight (sx (30)));
    headerRow.removeFromRight (sx (8));
    analysisSelector.setBounds (headerRow.removeFromRight (sx (200)));
    headerRow.removeFromRight (sx (8));
    sidecarButton   .setBounds (headerRow.removeFromRight (sx (130)));
    headerRow.removeFromRight (sx (8));
    // FR and Farina Capture / Clear pairs share the same physical slot;
    // only one mode's pair is visible at a time so the overlap is fine.
    {
        auto clearRect   = headerRow.removeFromRight (sx (50));
        headerRow.removeFromRight (sx (4));
        auto captureRect = headerRow.removeFromRight (sx (62));

        sweepClearButton    .setBounds (clearRect);
        sweepCaptureButton  .setBounds (captureRect);
        farinaClearButton   .setBounds (clearRect);
        farinaCaptureButton .setBounds (captureRect);
    }

    bounds.removeFromTop (sx (8));

    // Controls at the bottom; spectrum fills the rest.
    latencyPanel.setBounds (bounds.removeFromBottom (sx (30)));
    bounds.removeFromBottom (sx (12));

    const int metersHeight = sx (40) + sx (20) + sx (40);  // post + scale strip + pre
    levelMetersPanel.setBounds (bounds.removeFromBottom (metersHeight));

    // Readout strip between spectrum and meters: cursor readout on the
    // left, inline stereo-imbalance readout in the middle, L / R / Diff
    // stereo toggles on the right (per the feedback-stereo-lr-diff-
    // convention memory - lives in the same row as the cursor x/y readout,
    // not inside the plot area).
    auto readoutStrip = bounds.removeFromBottom (sx (18));
    cursorReadout.setBounds (readoutStrip.removeFromLeft (sx (220)));

    {
        auto stereoRow = readoutStrip.removeFromRight (sx (30) + sx (4) + sx (30) + sx (4) + sx (40));
        stereoDiffButton.setBounds (stereoRow.removeFromRight (sx (40)));
        stereoRow.removeFromRight (sx (4));
        stereoRButton.setBounds (stereoRow.removeFromRight (sx (30)));
        stereoRow.removeFromRight (sx (4));
        stereoLButton.setBounds (stereoRow.removeFromRight (sx (30)));
    }

    // What's left of readoutStrip is the middle gap between cursor and
    // toggles. The imbalance readout fills it with a small left pad.
    readoutStrip.removeFromLeft (sx (8));
    readoutStrip.removeFromRight (sx (8));
    imbalanceReadout.setBounds (readoutStrip);
    imbalanceReadout.setUiScale (s);

    bounds.removeFromBottom (sx (4));

    // All mode panels share the same rect; visibility decides which is drawn.
    spectrumDisplay  .setBounds (bounds);
    thdDisplay       .setBounds (bounds);
    imdDisplay       .setBounds (bounds);
    impulseDisplay   .setBounds (bounds);
    farinaDisplay    .setBounds (bounds);
    stereoDisplay    .setBounds (bounds);
    sweepCurveDisplay.setBounds (bounds);
    phaseDisplay     .setBounds (bounds);
    dynamicsDisplay  .setBounds (bounds);

    // The sidechain notice covers the whole display rect.
    sidechainNotice.setBounds (bounds);
}
