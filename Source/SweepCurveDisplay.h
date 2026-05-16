/*
  ==============================================================================

    SweepCurveDisplay.h
    Mode-specific display for the Parameter Sweep analysis - a Plugin
    Doctor style 1D X-Y plot.

    X is the swept parameter (the `sweepPosition` APVTS lane, 0..1, that
    the user automates from the DAW alongside whatever they sweep in the
    source plugin). Y is a headline measurement metric. A metric selector
    in the header picks which: THD% or IMD%.

    While sweep capture is armed (the shared header Capture button) the
    processor records the metric per L/R channel into SweepCurve; this
    component plots the captured curve plus a live dot at the current
    sweep position so the user can see where they are mid-sweep.

    The shared editor-level SidechainNotice covers this panel when the
    sidechain isn't connected, so the display assumes pre is wired.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SweepCurveDisplay  : public juce::Component,
                           private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit SweepCurveDisplay (WTAnalyzerAudioProcessor& proc);
    ~SweepCurveDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class Metric { THD, IMD };
    Metric currentMetric() const noexcept;
    void   syncMetricButtons();

    // Reads the live per-channel metric value from the active sub-analysis.
    // Returns SweepCurve::kNoData for a channel with no valid measurement.
    void liveValues (float& outL, float& outR) const;

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::TextButton thdButton { "THD%" };
    juce::TextButton imdButton { "IMD%" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepCurveDisplay)
};
