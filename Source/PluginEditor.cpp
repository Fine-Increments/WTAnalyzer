/*
  ==============================================================================

    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
WTAnalyzerAudioProcessorEditor::WTAnalyzerAudioProcessorEditor (WTAnalyzerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor       (p),
      spectrumDisplay      (p),
      cursorReadout        (spectrumDisplay),
      thdDisplay           (p),
      imdDisplay           (p),
      levelMetersPanel     (p),
      latencyPanel         (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (spectrumDisplay);
    addAndMakeVisible (cursorReadout);
    addChildComponent (thdDisplay);     // hidden by default; applyAnalysisMode shows it
    addChildComponent (imdDisplay);
    addAndMakeVisible (levelMetersPanel);
    addAndMakeVisible (latencyPanel);

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

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (kBaseWidth, kBaseHeight);

    // Pick up the current activeAnalysis value immediately and any subsequent
    // changes (from the ComboBox, host preset load, host automation). Cheap
    // poll - mode changes are infrequent so 10 Hz is plenty.
    applyAnalysisMode ((int) *audioProcessor.apvts.getRawParameterValue ("activeAnalysis"));
    startTimerHz (10);
}

WTAnalyzerAudioProcessorEditor::~WTAnalyzerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);  // detach before LookAndFeel member destructs
}

void WTAnalyzerAudioProcessorEditor::timerCallback()
{
    const int current = (int) *audioProcessor.apvts.getRawParameterValue ("activeAnalysis");
    if (current != lastAppliedAnalysisMode)
        applyAnalysisMode (current);
}

void WTAnalyzerAudioProcessorEditor::applyAnalysisMode (int modeIndex)
{
    lastAppliedAnalysisMode = modeIndex;

    using Mode = WTAnalyzerAudioProcessor::AnalysisMode;
    const bool wantsSpectrumPath = (modeIndex == (int) Mode::GenericOverlay
                                  || modeIndex == (int) Mode::FrequencyResponse
                                  || modeIndex == (int) Mode::AliasingDetection);
    const bool wantsThdPath      = (modeIndex == (int) Mode::THDMeasurement);
    const bool wantsImdPath      = (modeIndex == (int) Mode::IMDMeasurement);

    spectrumDisplay.setVisible (wantsSpectrumPath);
    cursorReadout  .setVisible (wantsSpectrumPath);
    thdDisplay     .setVisible (wantsThdPath);
    imdDisplay     .setVisible (wantsImdPath);

    // The alias-view toggle row lives inside SpectrumDisplay (it's tied to
    // a specific mode within the shared spectrum panel) but its visibility
    // is mode-driven from the editor.
    spectrumDisplay.setAliasingViewButtonsVisible (modeIndex == (int) Mode::AliasingDetection);

    // Per-mode input caption. Mention any non-standard inputs that yield
    // useful diagnostic value, not just the textbook test signal. Empty
    // string for modes that genuinely have no input assumption.
    switch ((Mode) modeIndex)
    {
        case Mode::GenericOverlay:
            captionText = {};
            break;
        case Mode::FrequencyResponse:
            captionText = "Sweep tone or broadband noise. "
                          "Wavetable sweeps also surface signal-character response.";
            break;
        case Mode::THDMeasurement:
            captionText = "Steady sine. "
                          "Differential view also works for saw, square, or wavetable input.";
            break;
        case Mode::AliasingDetection:
            captionText = "High-frequency sine sweep (4-20 kHz). "
                          "Saw or square sweeps fold more harmonics into the alias zone.";
            break;
        case Mode::IMDMeasurement:
            captionText = "Two pure sines. SMPTE (60 Hz + 7 kHz) or CCIF (19 + 20 kHz) "
                          "are the canonical tests; any two distinct tones work.";
            break;
    }

    repaint();
}

//==============================================================================
void WTAnalyzerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202225));

    // Title: drawn in the header row, with space reserved on the right for
    // the analysis selector ComboBox (positioned by resized()).
    auto bounds = getLocalBounds().reduced (sx (16));
    auto headerRow = bounds.removeFromTop (sx (24));

    const int selectorWidth = sx (200);
    headerRow.removeFromRight (selectorWidth + sx (8));

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText ("WTAnalyzer", headerRow, juce::Justification::centredLeft);

    if (captionText.isNotEmpty() && ! captionBounds.isEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (11.0f)));
        g.drawText (captionText, captionBounds, juce::Justification::centred, false);
    }
}

void WTAnalyzerAudioProcessorEditor::resized()
{
    const float s = scale();
    lookAndFeel.setUiScale (s);
    spectrumDisplay .setUiScale (s);
    cursorReadout   .setUiScale (s);
    thdDisplay      .setUiScale (s);
    imdDisplay      .setUiScale (s);
    levelMetersPanel.setUiScale (s);
    latencyPanel    .setUiScale (s);

    auto bounds = getLocalBounds().reduced (sx (16));

    // Header row: title (drawn by paint()) on the left, analysis selector on the right.
    auto headerRow = bounds.removeFromTop (sx (24));
    analysisSelector.setBounds (headerRow.removeFromRight (sx (200)));

    bounds.removeFromTop (sx (8));

    // Controls at the bottom; spectrum fills the rest.
    latencyPanel.setBounds (bounds.removeFromBottom (sx (30)));
    bounds.removeFromBottom (sx (12));

    const int metersHeight = sx (40) + sx (20) + sx (40);  // post + scale strip + pre
    levelMetersPanel.setBounds (bounds.removeFromBottom (metersHeight));

    // Readout strip between spectrum and meters. The cursor readout occupies
    // the left 220 sx (only visible when hovering the spectrum), and the
    // caption is drawn across the full strip width with centred justification
    // so it sits visually below the analysis panel rather than tucked into
    // the right edge. Overlap with the cursor readout is rare (only on
    // hover) and the cursor's short text doesn't obscure the middle of the
    // caption.
    auto readoutStrip = bounds.removeFromBottom (sx (18));
    captionBounds = readoutStrip;
    cursorReadout.setBounds (readoutStrip.removeFromLeft (sx (220)));
    bounds.removeFromBottom (sx (4));

    // Spectrum, THD and IMD share the same rect; visibility decides which is drawn.
    spectrumDisplay.setBounds (bounds);
    thdDisplay     .setBounds (bounds);
    imdDisplay     .setBounds (bounds);
}
