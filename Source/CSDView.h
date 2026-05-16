/*
  ==============================================================================

    CSDView.h
    Shared Cumulative Spectral Decay display, embedded by both IR modes
    (Direct Impulse IR and Farina IR) as the CSD Heatmap and CSD 3D
    sub-views.

    The host IR display pushes its captured IR in via updateSource() on
    each timer tick; CSDView recomputes the CSD grid only when the IR
    actually changes (tracked by a generation counter), renders the
    chosen style into a cached image, and blits that image on paint.

    Style (heatmap vs 3D waterfall) follows the shared `irView` APVTS
    parameter; the displayed channel follows the shared L / R toggles.

    Interaction:
      - Heatmap: drag-to-zoom. Drag in the plot zooms both axes; drag in
        the time (left) or frequency (bottom) gutter zooms that axis
        only. Double-click resets.
      - Waterfall: drag to orbit the 3D view (azimuth / elevation),
        scroll to dolly in/out. Double-click resets the camera.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/CSD.h"

//==============================================================================
// Small icon button for the waterfall's camera controls: a magnifying
// glass (zoom in / out) or a directional arrow (pan up/down/left/right).
class CSDIconButton  : public juce::Button
{
public:
    enum class Icon { ZoomIn, ZoomOut, Up, Down, Left, Right };

    explicit CSDIconButton (Icon i) : juce::Button ({}), icon (i) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const float corner = b.getWidth() * 0.18f;
        g.setColour (juce::Colour (down ? 0xff3c4049 : over ? 0xff2d3038 : 0xff202329));
        g.fillRoundedRectangle (b, corner);
        g.setColour (juce::Colour (0xff44484f));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

        g.setColour (juce::Colours::whitesmoke.withAlpha (0.85f));
        const auto  c = b.getCentre();
        const float s = juce::jmin (b.getWidth(), b.getHeight());

        if (icon == Icon::ZoomIn || icon == Icon::ZoomOut)
        {
            const float r = s * 0.22f;
            const juce::Point<float> gc (c.x - s * 0.06f, c.y - s * 0.06f);
            g.drawEllipse (gc.x - r, gc.y - r, r * 2.0f, r * 2.0f, s * 0.08f);
            const float h = r * 0.70710678f;
            g.drawLine (gc.x + h, gc.y + h,
                        gc.x + h + s * 0.20f, gc.y + h + s * 0.20f, s * 0.10f);
            g.drawLine (gc.x - r * 0.5f, gc.y, gc.x + r * 0.5f, gc.y, s * 0.08f);
            if (icon == Icon::ZoomIn)
                g.drawLine (gc.x, gc.y - r * 0.5f, gc.x, gc.y + r * 0.5f, s * 0.08f);
        }
        else
        {
            const float a = s * 0.26f;
            juce::Path p;
            if      (icon == Icon::Up)    p.addTriangle (c.x, c.y - a, c.x - a, c.y + a * 0.7f, c.x + a, c.y + a * 0.7f);
            else if (icon == Icon::Down)  p.addTriangle (c.x, c.y + a, c.x - a, c.y - a * 0.7f, c.x + a, c.y - a * 0.7f);
            else if (icon == Icon::Left)  p.addTriangle (c.x - a, c.y, c.x + a * 0.7f, c.y - a, c.x + a * 0.7f, c.y + a);
            else                          p.addTriangle (c.x + a, c.y, c.x - a * 0.7f, c.y - a, c.x - a * 0.7f, c.y + a);
            g.fillPath (p);
        }
    }

private:
    Icon icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CSDIconButton)
};

//==============================================================================
class CSDView  : public juce::Component
{
public:
    explicit CSDView (WTAnalyzerAudioProcessor& proc);

    void setUiScale (float newScale) noexcept;

    // Pushed by the host IR display each timer tick while a CSD view is
    // active. `generation` must change whenever the IR changes so the
    // grid is recomputed only on a fresh capture.
    void updateSource (const float* irL, int nL,
                       const float* irR, int nR,
                       double sampleRate, int generation);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

private:
    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class Style { Heatmap, Waterfall };
    Style currentStyle()   const noexcept;
    int   currentChannel() const noexcept;   // 0 = L, 1 = R

    // Plot rect of the heatmap (component bounds minus the axis gutters).
    juce::Rectangle<int> heatmapPlotArea() const noexcept;
    // Effective heatmap view ranges, resolving the "full range" sentinels.
    void heatmapFreqRange (float& loHz, float& hiHz) const noexcept;

    void renderImage();
    void renderHeatmap   (int channel);
    void renderWaterfall (int channel);

    void resetZoom();
    void resetCamera();
    void applyDolly (float factor);          // waterfall zoom in / out
    void applyPan   (float dx, float dy);    // waterfall lateral / vertical slide

    // Waterfall camera controls, shown only in the 3D view: two zoom
    // buttons stacked, then a 4-way pan cross below them.
    CSDIconButton zoomInButton    { CSDIconButton::Icon::ZoomIn  };
    CSDIconButton zoomOutButton   { CSDIconButton::Icon::ZoomOut };
    CSDIconButton panUpButton     { CSDIconButton::Icon::Up      };
    CSDIconButton panDownButton   { CSDIconButton::Icon::Down    };
    CSDIconButton panLeftButton   { CSDIconButton::Icon::Left    };
    CSDIconButton panRightButton  { CSDIconButton::Icon::Right   };

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    CSD csd;
    juce::Image cachedImage;

    int  lastGeneration = -1;
    int  lastChannel    = -1;
    int  lastStyleIdx   = -1;
    bool haveData       = false;

    // Heatmap zoom. Frequency is stored in Hz; (0, 0) means "full range"
    // since the full top depends on the sample rate. Time is a 0..1
    // fraction of the captured span.
    float zoomFreqLoHz = 0.0f;
    float zoomFreqHiHz = 0.0f;
    float zoomTimeLo   = 0.0f;
    float zoomTimeHi   = 1.0f;

    // Waterfall camera: orbit angles (radians), dolly (zoom) factor, and
    // a screen-space pan offset as a fraction of the plot size.
    float camAzimuth   = -0.6f;
    float camElevation =  0.42f;
    float camDolly     =  1.0f;
    float camPanX      =  0.0f;
    float camPanY      =  0.0f;

    // Drag state.
    enum class DragZone { None, Plot, FreqGutter, TimeGutter, Orbit };
    DragZone dragZone = DragZone::None;
    bool dragging = false;
    juce::Point<float> dragStart;
    juce::Point<float> dragCurrent;
    float dragAz0 = 0.0f;
    float dragEl0 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CSDView)
};
