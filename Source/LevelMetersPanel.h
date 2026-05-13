/*
  ==============================================================================

    LevelMetersPanel.h
    Pre/Post horizontal level meters with routing-state indication.
    Reads the level atomics from the processor, refreshes at 30 Hz,
    scales uniformly via setUiScale.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LevelMetersPanel  : public juce::Component,
                          private juce::Timer
{
public:
    explicit LevelMetersPanel (WTAnalyzerAudioProcessor& proc);
    ~LevelMetersPanel() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> row,
                    const juce::String& label, float db, bool active);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMetersPanel)
};
