/*
  ==============================================================================

    StepResponseDisplay.h
    UI for StepResponse.

      - Header: status readout on the left, the derived metrics (rise time,
        overshoot) on the right.
      - Bottom of the header: the capture-window slider (APVTS-backed).
      - Plot: the captured step response - linear time on X with t = 0 (the
        step edge) marked, linear amplitude on Y.

    Visible only when activeAnalysis is Step Response.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class StepResponseDisplay  : public juce::Component,
                             private juce::Timer
{
public:
    explicit StepResponseDisplay (WTAnalyzerAudioProcessor& proc);
    ~StepResponseDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> plotArea);
    juce::String statusText() const;

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::Slider windowSlider;
    juce::Label  windowLabel { {}, "Window" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepResponseDisplay)
};
