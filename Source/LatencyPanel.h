/*
  ==============================================================================

    LatencyPanel.h
    Pre-Effect delay control row: label + numeric text editor + suffix + Auto
    button. Owns its own polling timer that picks up Auto measurement results
    from the processor and reflects external APVTS changes into the text field.
    Scales uniformly via setUiScale.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LatencyPanel  : public juce::Component,
                      private juce::Timer
{
public:
    explicit LatencyPanel (WTAnalyzerAudioProcessor& proc);
    ~LatencyPanel() override;

    void setUiScale (float newScale);

    void resized() override;

private:
    void timerCallback() override;
    void commitDelayFromEditor();
    void applyMeasuredLatency (int samples);

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::Label      preDelayLabel;
    juce::TextEditor preDelayEditor;
    juce::Label      preDelaySuffix;
    juce::TextButton autoMeasureButton { "Auto" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatencyPanel)
};
