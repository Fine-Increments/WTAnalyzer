/*
  ==============================================================================

    SweepCurveDisplay.h
    The swept-result panel for THD and IMD modes - a Plugin Doctor style
    plot of the headline metric across the sweep axis. Shown in place of
    the live bar chart while Capture is armed.

    One header selector: the view (Line / Heatmap). The metric (THD% or
    IMD%) follows the active analysis mode - the editor sets it via
    setMetric().

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
#include "CSDIconButton.h"

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

    // The metric (THD% / IMD%) is set by the editor from the active
    // analysis mode - this display is shown inside both THD and IMD modes.
    void setMetric (bool isImd) noexcept;

    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class Metric { THD, IMD };
    enum class View   { Line, Heatmap, Surface };
    Metric currentMetric() const noexcept;
    View   currentView()   const noexcept;
    void   syncButtons();

    // Shows / hides the 3D camera-control cluster with the Surface view.
    void syncCameraButtons();

    void resetCamera()                   noexcept;
    void applyDolly (float factor)       noexcept;   // surface zoom in / out
    void applyPan   (float dx, float dy) noexcept;   // surface lateral / vertical slide

    // Reads the live per-channel metric value from the active sub-analysis.
    // Returns SweepCurve::kNoData for a channel with no valid measurement.
    void liveValues (float& outL, float& outR) const;

    void drawLine    (juce::Graphics& g, juce::Rectangle<int> area);
    void drawHeatmap (juce::Graphics& g, juce::Rectangle<int> area);
    void drawSurface (juce::Graphics& g, juce::Rectangle<int> area);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Metric is THD when false, IMD when true - set by the editor from
    // the active analysis mode (this panel serves both THD and IMD).
    bool metricIsImd = false;

    // 3D-surface orbit camera (radians / factor), used only by the
    // Surface view; drag orbits, scroll dollies, double-click resets.
    // camPanX / camPanY are a screen-space slide as a fraction of the plot.
    float camAzimuth   = -0.6f;
    float camElevation =  0.42f;
    float camDolly     =  1.0f;
    float camPanX      =  0.0f;
    float camPanY      =  0.0f;
    bool  dragging     = false;
    juce::Point<float> dragStart;
    float dragAz0 = 0.0f;
    float dragEl0 = 0.0f;

    // Last mouse position over the panel and whether the pointer is
    // inside it. Drives the cursor readout strip below the plot.
    juce::Point<int> cursorPos;
    bool             cursorInside = false;

    // Cursor readout text, recomputed each paint by the active view and
    // drawn in the strip below the plot.
    juce::String     hoverText;

    juce::TextButton lineButton    { "Line"    };
    juce::TextButton heatmapButton { "Heatmap" };
    juce::TextButton surfaceButton { "3D"      };

    // Surface camera controls, shown only in the 3D view: two zoom
    // buttons stacked, then a 4-way pan cross below them.
    CSDIconButton zoomInButton   { CSDIconButton::Icon::ZoomIn  };
    CSDIconButton zoomOutButton  { CSDIconButton::Icon::ZoomOut };
    CSDIconButton panUpButton    { CSDIconButton::Icon::Up      };
    CSDIconButton panDownButton  { CSDIconButton::Icon::Down    };
    CSDIconButton panLeftButton  { CSDIconButton::Icon::Left    };
    CSDIconButton panRightButton { CSDIconButton::Icon::Right   };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepCurveDisplay)
};
