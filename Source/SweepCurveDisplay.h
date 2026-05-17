/*
  ==============================================================================

    SweepCurveDisplay.h
    Mode-specific display for the Parameter Sweep analysis - a Plugin
    Doctor style 1D X-Y plot.

    Two header selectors: a metric (THD% / IMD%) and a view (Line /
    Heatmap).

      - Line: a 1D X-Y plot. X is the swept parameter (the `sweepPosition`
        APVTS lane, 0..1), Y is the headline metric scalar, drawn per
        L/R channel with a live dot at the current position.
      - Heatmap: the metric's full per-harmonic / per-product
        differential-dB distribution. X is the harmonic / product, Y is
        the sweep position, colour is level - the 2D parameter-sweep view.

    While sweep capture is armed (the shared header Capture button) the
    processor records both the scalar (SweepCurve) and the full row
    (SweepGrid) per bucket, so switching view needs no re-capture.

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
    enum class View   { Line, Heatmap };
    Metric currentMetric() const noexcept;
    View   currentView()   const noexcept;
    void   syncButtons();

    // Reads the live per-channel metric value from the active sub-analysis.
    // Returns SweepCurve::kNoData for a channel with no valid measurement.
    void liveValues (float& outL, float& outR) const;

    void drawLine    (juce::Graphics& g, juce::Rectangle<int> area);
    void drawHeatmap (juce::Graphics& g, juce::Rectangle<int> area);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    juce::TextButton thdButton     { "THD%"    };
    juce::TextButton imdButton     { "IMD%"    };
    juce::TextButton lineButton    { "Line"    };
    juce::TextButton heatmapButton { "Heatmap" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepCurveDisplay)
};
