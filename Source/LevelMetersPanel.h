/*
  ==============================================================================

    LevelMetersPanel.h
    Pre/Post horizontal level meters with routing-state indication.
    Reads the level atomics from the processor, refreshes at 30 Hz,
    scales uniformly via setUiScale.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LevelMetersPanel  : public juce::Component,
                          private juce::Timer
{
public:
    explicit LevelMetersPanel (WTAnalyzerAudioProcessor& proc);
    ~LevelMetersPanel() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    // Draws one stereo meter row: channel label on the left, L / R glyph
    // indicators in a small gutter outside the bar's left edge, then the
    // bar itself split in half vertically (L on top, R on bottom). The
    // gradient is `colour` -> white inside each half so a single per-channel
    // colour is all that needs to be specified for the whole meter family.
    void drawMeter (juce::Graphics& g, juce::Rectangle<int> row,
                    const juce::String& label,
                    juce::Colour lColour, juce::Colour rColour,
                    float lDb, float rDb,
                    bool lActive, bool rActive);

    // Renders a `base -> white` gradient fill across one sub-bar based on
    // the supplied dB level. Fully anchored across the bar's width so the
    // colour at any horizontal position is independent of how far the fill
    // currently extends.
    void drawMeterHalf (juce::Graphics& g, juce::Rectangle<int> bar,
                        juce::Colour colour, float db, bool active);

    // Labelled dB scale drawn between the post and pre meters; tick marks
    // line up vertically with the level positions in both bars.
    void drawLevelScale (juce::Graphics& g, juce::Rectangle<int> row);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Peak/RMS mode toggle. Lives in the left portion of the scale strip;
    // attached to the meterUseRms APVTS parameter so the choice persists
    // and is host-automatable.
    juce::TextButton meterModeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> meterModeAttachment;

    void updateMeterModeButtonText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMetersPanel)
};
