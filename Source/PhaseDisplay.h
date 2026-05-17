/*
  ==============================================================================

    PhaseDisplay.h
    Mode-specific display for the Phase Response analysis - the phase side
    of the transfer function, companion to the magnitude in Frequency
    Response mode.

    A Phase / Group Delay selector picks the sub-view:

      - Phase (default): the device's phase response with its best-fit
        linear-phase (bulk-latency) component removed, drawn as a wrapped
        +/-180 degree curve over log frequency.
      - Group Delay: the negated slope of the unwrapped phase, in
        milliseconds, over log frequency.

    The shared editor-level SidechainNotice covers this panel when the
    sidechain isn't connected, so the display assumes pre is wired.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PhaseDisplay  : public juce::Component,
                      private juce::Timer,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PhaseDisplay (WTAnalyzerAudioProcessor& proc);
    ~PhaseDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class View { Phase, GroupDelay };
    View currentView() const noexcept;
    void syncViewButtons();

    void drawCurve (juce::Graphics& g, juce::Rectangle<int> plotArea);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Last mouse position over the panel and whether the pointer is
    // inside it. Drives the cursor readout strip below the plot.
    juce::Point<int> cursorPos;
    bool             cursorInside = false;

    // Cursor readout text, recomputed each paint and drawn in the strip
    // below the plot.
    juce::String     hoverText;

    juce::TextButton phaseButton      { "Phase" };
    juce::TextButton groupDelayButton { "Group Delay" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseDisplay)
};
