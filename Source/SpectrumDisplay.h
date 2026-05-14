/*
  ==============================================================================

    SpectrumDisplay.h
    Universal overlay component: log-frequency / dB spectrum plot with axis
    labels and gridlines. Reads dB magnitude arrays directly from the processor
    and refreshes itself at 30 Hz. Scales uniformly via setUiScale.

    Mouse interaction (zone-dependent):
      - Drag in the dB axis gutter (left side):   zooms dB axis only.
      - Drag in the frequency axis gutter (bottom): zooms frequency axis only.
      - Drag in the plot area:                     zooms both axes.
      - In all cases: drag up = zoom in, drag down = zoom out, focal point
        is the cursor position at drag start.
      - Double-click resets the axis(es) for the zone clicked.

    The display is shared across every analysis that visualises spectrum
    data, so the zoom behaviour applies universally.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectrumDisplay  : public juce::Component,
                         private juce::Timer
{
public:
    explicit SpectrumDisplay (WTAnalyzerAudioProcessor& proc);
    ~SpectrumDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;

    // Hover state, read by the CursorReadout panel below the spectrum. Only
    // valid when isHoverActive() is true (cursor is inside the plot area,
    // not the gutters and not outside the component).
    bool  isHoverActive() const noexcept { return hoverActive; }
    float getHoverFreq()  const noexcept { return hoverFreq; }
    float getHoverDb()    const noexcept { return hoverDb; }

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    // Plot area is the spectrum minus the dB gutter on the left and the
    // frequency gutter on the bottom. Computed identically in paint() and
    // in the mouse handlers, so a single helper keeps the two in sync.
    juce::Rectangle<int> getPlotArea() const noexcept;

    // Which region of the component the mouse interacts with. Set in
    // mouseDown and read by mouseDrag / mouseDoubleClick.
    enum class DragZone { None, DbAxis, FreqAxis, Plot };
    DragZone zoneFromPoint (juce::Point<int> p) const noexcept;

    void resetDbView()   noexcept;
    void resetFreqView() noexcept;
    void resetView()     noexcept;
    void clampViewToLimits() noexcept;

    // Recomputes hover freq/dB from a screen position. Sets hoverActive
    // only when the position is inside the plot area (both axes have
    // meaning); otherwise clears it.
    void updateHoverFromPoint (juce::Point<int> p) noexcept;

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Default view bounds - what double-click restores to. Tuned so the
    // labelled +12 dB gridline sits inside the plot area with headroom above.
    static constexpr float kDefaultMinDb   = -80.0f;
    static constexpr float kDefaultMaxDb   = 18.0f;
    static constexpr float kDefaultMinFreq = 20.0f;

    // Hard zoom limits. Going beyond these is rejected by clampViewToLimits.
    static constexpr float kHardMinDb       = -120.0f;
    static constexpr float kHardMaxDb       = 48.0f;
    static constexpr float kHardMinFreq     = 5.0f;
    static constexpr float kMinDbRange      = 6.0f;     // fully zoomed-in floor for dB
    static constexpr float kMinLogFreqRange = 0.1f;     // ~1/3 octave fully zoomed-in floor

    // Current view bounds; updated by mouse drags and double-click reset.
    float viewMinDb   = kDefaultMinDb;
    float viewMaxDb   = kDefaultMaxDb;
    float viewMinFreq = kDefaultMinFreq;
    float viewMaxFreq = 24000.0f;       // overwritten with Nyquist once sample rate is known

    // Snapshot of the view at the moment mouseDown fired; mouseDrag computes
    // zoom factor against this baseline so it remains stable across the drag.
    DragZone         dragZone = DragZone::None;
    juce::Point<int> dragStartPos;
    float dragStartMinDb   = 0.0f;
    float dragStartMaxDb   = 0.0f;
    float dragStartMinFreq = 0.0f;
    float dragStartMaxFreq = 0.0f;

    // Hover state for the cursor-position readout (drawn by an external panel).
    bool  hoverActive = false;
    float hoverFreq   = 0.0f;
    float hoverDb     = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
