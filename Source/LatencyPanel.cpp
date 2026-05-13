/*
  ==============================================================================

    LatencyPanel.cpp

  ==============================================================================
*/

#include "LatencyPanel.h"

LatencyPanel::LatencyPanel (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    addAndMakeVisible (preDelayLabel);
    preDelayLabel.setText ("Pre-Effect delay:", juce::dontSendNotification);
    preDelayLabel.setJustificationType (juce::Justification::centredLeft);
    preDelayLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);

    addAndMakeVisible (preDelayEditor);
    preDelayEditor.setInputRestrictions (6, "0123456789");
    preDelayEditor.setJustification (juce::Justification::centredRight);
    preDelayEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111213));
    preDelayEditor.setColour (juce::TextEditor::textColourId,       juce::Colours::whitesmoke);
    preDelayEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff3a3d42));
    preDelayEditor.setText (juce::String ((int) *processor.apvts.getRawParameterValue ("preDelaySamples")),
                            juce::dontSendNotification);
    preDelayEditor.onReturnKey = [this] { commitDelayFromEditor(); };
    preDelayEditor.onFocusLost = [this] { commitDelayFromEditor(); };

    addAndMakeVisible (preDelaySuffix);
    preDelaySuffix.setText ("samples", juce::dontSendNotification);
    preDelaySuffix.setJustificationType (juce::Justification::centredLeft);
    preDelaySuffix.setColour (juce::Label::textColourId, juce::Colours::grey);

    addAndMakeVisible (autoMeasureButton);
    autoMeasureButton.onClick = [this]
    {
        autoMeasureButton.setEnabled (false);
        autoMeasureButton.setButtonText ("...");
        processor.measureLatencyRequested.store (true, std::memory_order_release);
    };

    startTimerHz (30);
}

LatencyPanel::~LatencyPanel()
{
    stopTimer();
}

void LatencyPanel::setUiScale (float newScale)
{
    uiScale = newScale;
    preDelayLabel .setFont (juce::FontOptions (sf (13.0f)));
    preDelayEditor.setFont (juce::FontOptions (sf (13.0f)));
    preDelaySuffix.setFont (juce::FontOptions (sf (13.0f)));
    // No resized() call here. The editor calls setUiScale before setBounds,
    // and setBounds triggers our resized() naturally with the up-to-date scale.
}

void LatencyPanel::commitDelayFromEditor()
{
    const int requested = preDelayEditor.getText().getIntValue();
    const int clamped   = juce::jlimit (0, WTAnalyzerAudioProcessor::kMaxDelaySamples, requested);

    if (auto* param = processor.apvts.getParameter ("preDelaySamples"))
    {
        const auto range       = param->getNormalisableRange();
        const float normalized = range.convertTo0to1 ((float) clamped);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalized);
        param->endChangeGesture();
    }

    preDelayEditor.setText (juce::String (clamped), juce::dontSendNotification);
}

void LatencyPanel::applyMeasuredLatency (int samples)
{
    const int clamped = juce::jlimit (0, WTAnalyzerAudioProcessor::kMaxDelaySamples, samples);

    if (auto* param = processor.apvts.getParameter ("preDelaySamples"))
    {
        const auto range       = param->getNormalisableRange();
        const float normalized = range.convertTo0to1 ((float) clamped);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalized);
        param->endChangeGesture();
    }
}

void LatencyPanel::timerCallback()
{
    if (processor.measureLatencyCompleted.exchange (false, std::memory_order_acq_rel))
    {
        const int measured = processor.lastMeasuredLatencyOffset.load (std::memory_order_acquire);
        applyMeasuredLatency (measured);
        autoMeasureButton.setButtonText ("Auto");
        autoMeasureButton.setEnabled (true);
    }

    if (! preDelayEditor.hasKeyboardFocus (false))
    {
        const int current   = (int) *processor.apvts.getRawParameterValue ("preDelaySamples");
        const int displayed = preDelayEditor.getText().getIntValue();
        if (current != displayed)
            preDelayEditor.setText (juce::String (current), juce::dontSendNotification);
    }
}

void LatencyPanel::resized()
{
    auto row = getLocalBounds();
    preDelayLabel    .setBounds (row.removeFromLeft (sx (130)));
    preDelayEditor   .setBounds (row.removeFromLeft (sx (90)).reduced (0, sx (3)));
    row.removeFromLeft (sx (6));
    preDelaySuffix   .setBounds (row.removeFromLeft (sx (60)));
    row.removeFromLeft (sx (12));
    autoMeasureButton.setBounds (row.removeFromLeft (sx (70)).reduced (0, sx (3)));
}
