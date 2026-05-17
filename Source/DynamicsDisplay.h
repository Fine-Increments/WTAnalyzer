/*
  ==============================================================================

    DynamicsDisplay.h
    Mode-specific display for the Dynamics analysis - the device's
    static input-vs-output transfer curve.

    X is the input (pre-effect) level in dB, Y the output (post-effect)
    level in dB, drawn per L/R channel. A 45-degree unity diagonal gives
    the reference: where the curve sits on the diagonal the device is
    pass-through; below it at high input is compression / limiting,
    below it at low input is expansion / gating.

    Both axes share the same dB range so the unity line is a true
    diagonal and the curve shape reads directly as a transfer function.

    A cursor readout strip below the plot reports the input / output dB
    under the mouse pointer, mirroring the spectrum modes.

    The data accumulates continuously while the mode is active; a Clear
    button in the panel header resets it. The shared editor-level
    SidechainNotice covers this panel when the sidechain isn't
    connected, so the display assumes pre is wired.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class DynamicsDisplay  : public juce::Component,
                         private juce::Timer
{
public:
    explicit DynamicsDisplay (WTAnalyzerAudioProcessor& proc);
    ~DynamicsDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    // The plot rectangle, derived deterministically from the component
    // bounds so paint() and the mouse handlers agree on the mapping.
    juce::Rectangle<int> plotBounds() const;

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Resets the accumulated transfer curve. Its own header button
    // rather than the shared sweep / Farina header pair - the Dynamics
    // curve has no Capture gesture, only a continuous accumulate.
    juce::TextButton clearButton { "Clear" };

    // Last mouse position over the panel and whether the pointer is
    // currently inside it. Drives the cursor readout strip.
    juce::Point<int> cursorPos;
    bool             cursorInside = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicsDisplay)
};
