/*
  ==============================================================================

    StereoDisplay.h
    Mode-specific display for the Stereo Image analysis - the home for all
    stereo-specific visualisation.

    A Divergence / Correlation / Goniometer selector picks the sub-view:

      - Divergence (default, shipped): per-frequency device-added stereo
        divergence as a bipolar centred-on-zero plot. A green centre line
        is "L and R agree"; the trace lifts upward (lime) where the device
        acted on the right channel, downward (mint) where it acted on the
        left. X is log frequency, Y is signed dB ("dB R" above, "dB L"
        below).
      - Correlation (planned): per-frequency phase correlation.
      - Goniometer (planned): time-domain L-vs-R XY scope.

    The shared editor-level SidechainNotice covers this panel when the
    sidechain isn't connected, so the display itself assumes pre is wired.

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

    enum class SubView { Divergence, Correlation, Goniometer };
    SubView currentSubView() const noexcept;
    void    syncViewButtons();

    void drawDivergence (juce::Graphics& g, juce::Rectangle<int> plotArea);
    void drawPlaceholder (juce::Graphics& g, juce::Rectangle<int> plotArea,
                          const juce::String& title);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Sub-view selector (radio group). Divergence is the default and the
    // only one implemented so far.
    juce::TextButton divergenceButton  { "Divergence"  };
    juce::TextButton correlationButton { "Correlation" };
    juce::TextButton goniometerButton  { "Goniometer"  };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoDisplay)
};
