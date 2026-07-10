/*
  ==============================================================================

    MlsDisplay.h
    UI for MlsIR. A capture-and-correlate workflow, presented like FarinaDisplay:

      - Header: status readout on the left, order/period summary on the right,
        Export button on the far right; Order and Tail sliders below.
      - View-selector band: Waveform / CSD Heatmap / CSD 3D (the shared
        `irView` parameter, same as the other IR modes).
      - Plot: the recovered impulse response, or the CSD sub-view.

    Visible only when activeAnalysis is MLS IR.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CSDView.h"

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
    void syncViewButtons();
    void exportIRToWav();

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Waveform / CSD Heatmap / CSD 3D selector, backed by the shared
    // `irView` APVTS parameter.
    juce::TextButton waveformButton   { "Waveform" };
    juce::TextButton csdHeatmapButton { "Heatmap"  };
    juce::TextButton csd3DButton      { "3D"       };
    CSDView          csdView;

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
