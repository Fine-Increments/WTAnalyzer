/*
  ==============================================================================

    PluginEditor.h
    Composes the panel components (SpectrumDisplay, LevelMetersPanel,
    LatencyPanel) and the analysis-mode selector. Manages the responsive
    scale factor and propagates it to every panel and the LookAndFeel.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SpectrumDisplay.h"
#include "LevelMetersPanel.h"
#include "LatencyPanel.h"

//==============================================================================
// LookAndFeel that scales JUCE-owned fonts (TextButton, ComboBox, etc.) by a
// runtime-adjustable factor so the whole UI grows uniformly when the window
// is resized. Components whose fonts we set directly (Label, TextEditor) have
// their fonts updated by their owning panels' setUiScale().
class WTLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    void setUiScale (float newScale) noexcept { uiScale = newScale; }

    juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override
    {
        return juce::Font (juce::FontOptions (13.0f * uiScale));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (13.0f * uiScale));
    }

private:
    float uiScale = 1.0f;
};

//==============================================================================
class WTAnalyzerAudioProcessorEditor  : public juce::AudioProcessorEditor
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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    float scale() const noexcept
    {
        return juce::jmin ((float) getWidth()  / (float) kBaseWidth,
                           (float) getHeight() / (float) kBaseHeight);
    }

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * scale()); }
    float sf (float v) const noexcept { return v * scale(); }

    WTAnalyzerAudioProcessor& audioProcessor;
    WTLookAndFeel lookAndFeel;

    SpectrumDisplay  spectrumDisplay;
    LevelMetersPanel levelMetersPanel;
    LatencyPanel     latencyPanel;

    juce::ComboBox analysisSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analysisAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessorEditor)
};
