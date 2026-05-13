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
      levelMetersPanel     (p),
      latencyPanel         (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (spectrumDisplay);
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
}

WTAnalyzerAudioProcessorEditor::~WTAnalyzerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);  // detach before LookAndFeel member destructs
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
}

void WTAnalyzerAudioProcessorEditor::resized()
{
    const float s = scale();
    lookAndFeel.setUiScale (s);
    spectrumDisplay .setUiScale (s);
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

    const int metersHeight = sx (40) + sx (4) + sx (40);  // post + gap + pre
    levelMetersPanel.setBounds (bounds.removeFromBottom (metersHeight));
    bounds.removeFromBottom (sx (12));

    spectrumDisplay.setBounds (bounds);
}
