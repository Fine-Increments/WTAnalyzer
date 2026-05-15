/*
  ==============================================================================

    ImpulseDisplay.h
    Time-domain plot of the captured impulse response.

    Layout (mirrors THD / IMD displays so the chrome stays consistent):
      - Header band: averaging-progress readout on the left ("X / Y averaged"),
        window-length readout on the right ("250 ms" etc.).
      - Bottom band: Clear button on the left, Window and Averages controls
        on the right (sliders backed by their APVTS params).
      - Plot below: time on X (ms), linear amplitude on Y. Symmetric Y axis
        scaled to peak of captured data.

    Visible only when activeAnalysis is set to Impulse Response.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/ImpulseResponse.h"

class ImpulseDisplay  : public juce::Component,
                        private juce::Timer
{
public:
    explicit ImpulseDisplay (WTAnalyzerAudioProcessor& proc);
    ~ImpulseDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> plotArea);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::TextButton clearButton  { "Clear" };
    juce::TextButton exportButton { "Export..." };
    juce::Slider     windowSlider;
    juce::Slider     averagesSlider;
    juce::Label      windowLabel    { {}, "Window" };
    juce::Label      averagesLabel  { {}, "Averages" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> averagesAttachment;

    void exportIRToWav();
    std::shared_ptr<juce::FileChooser> exportChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImpulseDisplay)
};
