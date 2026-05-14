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
    void resized() override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> row,
                    const juce::String& label, juce::Colour labelColour,
                    float db, bool active);

    // Labelled dB scale drawn between the post and pre meters; tick marks
    // line up vertically with the level positions in both bars.
    void drawLevelScale (juce::Graphics& g, juce::Rectangle<int> row);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Peak/RMS mode toggle. Lives in the left portion of the scale strip;
    // attached to the meterUseRms APVTS parameter so the choice persists
    // and is host-automatable.
    juce::TextButton meterModeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> meterModeAttachment;

    void updateMeterModeButtonText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMetersPanel)
};
