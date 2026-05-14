/*
  ==============================================================================

    IMDDisplay.h
    Mode-specific display for IMDMeasurement. Same architecture as
    THDDisplay - per-product bar chart + Diff/Pre/Post view toggle + Hold/
    Freeze capture controls. The big top readout is the differential IMD%.

    Visible only when activeAnalysis is set to IMD Measurement.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/IMDMeasurement.h"

class IMDDisplay  : public juce::Component,
                    private juce::Timer,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit IMDDisplay (WTAnalyzerAudioProcessor& proc);
    ~IMDDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    struct DisplayFrame
    {
        bool  valid       = false;
        float imdPercent  = 0.0f;
        float f1Hz        = 0.0f;
        float f2Hz        = 0.0f;
        float preF1Db     = IMDMeasurement::kNoMeasurementDb;
        float preF2Db     = IMDMeasurement::kNoMeasurementDb;
        float postF1Db    = IMDMeasurement::kNoMeasurementDb;
        float postF2Db    = IMDMeasurement::kNoMeasurementDb;

        // [source: Diff=0, Pre=1, Post=2][product index]
        std::array<std::array<float, IMDMeasurement::kNumProducts>, 3> ratioDb {};
    };

    DisplayFrame sampleProcessor() const;
    static int   sourceIndex (IMDMeasurement::Source s) noexcept;

    void drawProductBars (juce::Graphics& g, juce::Rectangle<int> area);

    IMDMeasurement::Source currentViewSource() const noexcept;
    juce::Colour            currentViewColour() const noexcept;
    void                    syncToggleButtons();

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    DisplayFrame displayed;
    bool isHolding = false;
    bool isFrozen  = false;

    juce::TextButton diffButton   { "Diff"   };
    juce::TextButton preButton    { "Pre"    };
    juce::TextButton postButton   { "Post"   };
    juce::TextButton holdButton   { "Hold"   };
    juce::TextButton freezeButton { "Freeze" };
    juce::TextButton layoutButton;                  // "By Order" / "By Hz"

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> layoutAttachment;

    void updateLayoutButtonText();
    bool isHzLayout() const noexcept { return layoutButton.getToggleState(); }

    void drawProductBarsByOrder (juce::Graphics&, juce::Rectangle<int> plotArea,
                                 juce::Rectangle<int> labelGutterBottom,
                                 const std::array<float, IMDMeasurement::kNumProducts>& ratios,
                                 juce::Colour barColour);

    void drawProductBarsByHz    (juce::Graphics&, juce::Rectangle<int> plotArea,
                                 juce::Rectangle<int> labelGutterBottom,
                                 const std::array<float, IMDMeasurement::kNumProducts>& ratios,
                                 juce::Colour barColour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IMDDisplay)
};
