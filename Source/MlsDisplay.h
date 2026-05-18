/*
  ==============================================================================

    MlsDisplay.h
    UI for MlsIR. A capture-and-correlate workflow, presented like FarinaDisplay:

      - Header: status readout on the left, sweep/order summary on the right,
        Export button on the far right.
      - Bottom of the header: Order and Tail sliders (APVTS-backed).
      - Plot: the recovered impulse response - linear time on X, linear
        amplitude on Y, scaled to the IR peak.

    Visible only when activeAnalysis is MLS IR.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MlsDisplay  : public juce::Component,
                    private juce::Timer
{
public:
    explicit MlsDisplay (WTAnalyzerAudioProcessor& proc);
    ~MlsDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> plotArea);
    juce::String statusText() const;
    void exportIRToWav();

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::Slider orderSlider;
    juce::Slider tailSlider;
    juce::Label  orderLabel { {}, "Order" };
    juce::Label  tailLabel  { {}, "Tail" };

    juce::TextButton exportButton { "Export" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> orderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tailAttachment;

    std::shared_ptr<juce::FileChooser> exportChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MlsDisplay)
};
