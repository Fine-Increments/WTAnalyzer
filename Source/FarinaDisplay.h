/*
  ==============================================================================

    FarinaDisplay.h
    UI for FarinaIR. Layout mirrors ImpulseDisplay closely - same
    time-domain IR plot at the bottom - but the controls reflect that
    Farina is a one-shot capture-and-deconvolve workflow:

      - Header: status readout ("Idle", "Waiting for sweep", "Capturing
        X / Y samples", "Processing", "IR ready") on the left; sweep
        parameters summary on the right.
      - Bottom row: Capture button + Clear button on the left; f0 / f1 /
        Sweep / Tail sliders on the right (all backed by APVTS so the
        DAW can automate them and the sidecar reader can drive them).
      - Plot: linear time on X (ms / s), linear amplitude on Y, scaled
        to the IR peak. Same min/max-bars-per-pixel rendering as
        ImpulseDisplay for long IRs.

    Visible only when activeAnalysis is set to Farina IR.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/FarinaIR.h"

class FarinaDisplay  : public juce::Component,
                       private juce::Timer
{
public:
    explicit FarinaDisplay (WTAnalyzerAudioProcessor& proc);
    ~FarinaDisplay() override;

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

    juce::TextButton captureButton { "Capture" };
    juce::TextButton clearButton   { "Clear"   };

    juce::Slider f0Slider;
    juce::Slider f1Slider;
    juce::Slider sweepSlider;
    juce::Slider tailSlider;

    juce::Label  f0Label    { {}, "f0" };
    juce::Label  f1Label    { {}, "f1" };
    juce::Label  sweepLabel { {}, "Sweep" };
    juce::Label  tailLabel  { {}, "Tail" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> f0Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> f1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sweepAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tailAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FarinaDisplay)
};
