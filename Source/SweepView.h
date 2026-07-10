/*
  ==============================================================================

    SweepView.h
    Shared sweep-capture display, embedded by the Frequency Response,
    Aliasing Detection and Phase Response modes.

    All three of those modes produce one per-frequency curve per analysis
    frame; while Capture is armed the processor buckets that curve by the
    `sweepPosition` parameter into the shared SweepCapture grid (frequency
    bins x position buckets). SweepView renders that grid three ways,
    following the `sweepView` APVTS parameter:

      - Line:    the captured per-position curves overlaid, position-graded.
      - Heatmap: the grid as a log-frequency x sweep-position colour map.
      - 3D:      the grid as an orbitable surface of stacked curves.

    The host display sets the value range (dB for FR / Aliasing, degrees
    for Phase) via setValueRange() so the colour ramp and surface height
    scale correctly for the mode.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CSDIconButton.h"

class SweepView  : public juce::Component,
                   private juce::Timer
{
public:
    explicit SweepView (WTAnalyzerAudioProcessor& proc);
    ~SweepView() override;

    void setUiScale (float newScale) noexcept;

    // Value axis for the active mode: lo / hi bound the colour ramp and the
    // 3D surface height, unit captions the 3D value axis. FR / Aliasing
    // pass a dB range; Phase passes degrees.
    void setValueRange (float lo, float hi, const juce::String& unit);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class Style { Line, Heatmap, Surface };
    Style currentStyle() const noexcept;
    void  syncButtons();

    // Shows / hides the 3D camera-control cluster with the Surface view.
    void syncCameraButtons();

    void resetCamera()                  noexcept;
    void applyDolly (float factor)      noexcept;   // surface zoom in / out
    void applyPan   (float dx, float dy) noexcept;  // surface lateral / vertical slide

    float valueToNorm (float v) const noexcept;   // -> 0..1 across [valueLo, valueHi]

    void drawLineOverlay (juce::Graphics& g, juce::Rectangle<int> plotArea);
    void drawHeatmap     (juce::Graphics& g, juce::Rectangle<int> plotArea);
    void drawSurface     (juce::Graphics& g, juce::Rectangle<int> area);

    // Line / Heatmap / 3D selector, backed by the shared `sweepView`
    // parameter (the same one SweepCurveDisplay uses for THD / IMD).
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

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    float valueLo = -60.0f;
    float valueHi =  12.0f;
    juce::String valueUnit { "dB" };

    // 3D camera (radians / factor), mirroring CSDView's waterfall camera.
    // camPanX / camPanY are a screen-space slide as a fraction of the plot.
    float camAzimuth   = -0.6f;
    float camElevation =  0.42f;
    float camDolly     =  1.0f;
    float camPanX      =  0.0f;
    float camPanY      =  0.0f;

    bool  dragging = false;
    juce::Point<float> dragStart;
    float dragAz0 = 0.0f;
    float dragEl0 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepView)
};
