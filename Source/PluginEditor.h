/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// LookAndFeel that scales JUCE-owned fonts (TextButton, Slider, etc.) by a
// runtime-adjustable factor so the whole UI grows uniformly when the window
// is resized. Components whose fonts we set directly (Label, TextEditor) have
// their fonts updated in resized() instead.
class WTLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    void setUiScale (float newScale) noexcept { uiScale = newScale; }

    juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
    {
        return juce::Font (juce::FontOptions (13.0f * uiScale));
    }

private:
    float uiScale = 1.0f;
};

//==============================================================================
class WTAnalyzerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    // The layout in paint()/resized() is authored at this size. Every pixel and
    // font value runs through scale() so the same layout description renders
    // identically at any window size between the min/max bounds.
    static constexpr int kBaseWidth  = 700;
    static constexpr int kBaseHeight = 490;
    static constexpr int kMinWidth   = 560;
    static constexpr int kMinHeight  = 392;
    static constexpr int kMaxWidth   = 2800;
    static constexpr int kMaxHeight  = 1960;

    WTAnalyzerAudioProcessorEditor (WTAnalyzerAudioProcessor&);
    ~WTAnalyzerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void commitDelayFromEditor();
    void applyMeasuredLatency (int samples);

    // Uniform scale factor derived from the smaller of width/height ratios
    // against the design baseline. Used everywhere in paint()/resized().
    float scale() const noexcept
    {
        return juce::jmin ((float) getWidth()  / (float) kBaseWidth,
                           (float) getHeight() / (float) kBaseHeight);
    }

    // Convenience wrappers so call sites stay short.
    int   sx (int   baseValue) const noexcept { return juce::roundToInt ((float) baseValue * scale()); }
    float sf (float baseValue) const noexcept { return baseValue * scale(); }

    WTAnalyzerAudioProcessor& audioProcessor;

    WTLookAndFeel lookAndFeel;

    juce::Label      preDelayLabel;
    juce::TextEditor preDelayEditor;
    juce::Label      preDelaySuffix;
    juce::TextButton autoMeasureButton { "Auto" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessorEditor)
};
