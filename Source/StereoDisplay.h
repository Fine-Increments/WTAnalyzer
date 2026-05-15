/*
  ==============================================================================

    StereoDisplay.h
    Mode-specific display for the Stereo Image analysis.

    Renders per-frequency stereo divergence as a bipolar centred-on-zero
    plot: a green centre line is "L and R agree", the trace lifts upward
    (lime) where the right channel is louder and downward (mint) where the
    left channel is louder. X is log frequency, Y is signed dB - relabelled
    "dB R" above zero and "dB L" below.

    A Pre / Post / Diff selector chooses which divergence to plot:
      - Diff (default): the device-added stereo divergence.
      - Pre / Post:     the raw input / output stereo image.

    Pre and Diff need the sidechain (pre-effect) input wired; when it
    isn't, the panel shows an explanatory message instead of a meaningless
    flat trace.

    This is the home for stereo-specific analysis. Future sub-views
    (spectral phase correlation, goniometer) will live here too.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/StereoAnalysis.h"

class StereoDisplay  : public juce::Component,
                       private juce::Timer,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit StereoDisplay (WTAnalyzerAudioProcessor& proc);
    ~StereoDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    StereoAnalysis::View currentView() const noexcept;
    void syncViewButtons();

    void drawDivergence (juce::Graphics& g, juce::Rectangle<int> plotArea);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Pre / Post / Diff view selector (radio group). Diff is the default
    // and canonical view (device-added divergence).
    juce::TextButton diffButton { "Diff" };
    juce::TextButton preButton  { "Pre"  };
    juce::TextButton postButton { "Post" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoDisplay)
};
