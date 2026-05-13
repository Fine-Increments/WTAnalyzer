/*
  ==============================================================================

    SpectrumDisplay.h
    Universal overlay component: log-frequency / dB spectrum plot with axis
    labels and gridlines. Reads dB magnitude arrays directly from the processor
    and refreshes itself at 30 Hz. Scales uniformly via setUiScale.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectrumDisplay  : public juce::Component,
                         private juce::Timer
{
public:
    explicit SpectrumDisplay (WTAnalyzerAudioProcessor& proc);
    ~SpectrumDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
