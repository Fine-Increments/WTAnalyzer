/*
  ==============================================================================

    THDDisplay.h
    Mode-specific display for THDMeasurement. Big THD% readout at the top,
    fundamental frequency just below, then a per-harmonic bar chart showing
    each harmonic's level relative to the fundamental (dB ratio scale).

    Visible only when activeAnalysis is set to THD Measurement; PluginEditor
    handles the panel-swap visibility.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class THDDisplay  : public juce::Component,
                    private juce::Timer
{
public:
    explicit THDDisplay (WTAnalyzerAudioProcessor& proc);
    ~THDDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawHarmonicBars (juce::Graphics& g, juce::Rectangle<int> area);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (THDDisplay)
};
