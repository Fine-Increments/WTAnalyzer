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
      - Correlation (shipped): per-frequency phase correlation of the
        post signal's L and R channels. X is log frequency, Y is the
        coherence value from +1 (in phase) through 0 (decorrelated) to
        -1 (anti-phase / mono-fold cancellation).
      - Goniometer (shipped): time-domain L-vs-R XY scope (Lissajous).
        M axis vertical, S axis horizontal, L and R on the 45-degree
        diagonals. A header toggle picks Pre / Post (overlaid input
        and output clouds) or Difference (post minus aligned pre - the
        stereo image the device added).

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

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    enum class SubView { Divergence, Correlation, Goniometer };
    SubView currentSubView() const noexcept;

    // Goniometer rendering mode: overlay the pre / post stereo clouds,
    // or scope the per-channel difference (post - aligned pre).
    enum class GonioMode { PrePost, Difference };
    GonioMode currentGonioMode() const noexcept;

    void    syncViewButtons();

    void drawDivergence (juce::Graphics& g, juce::Rectangle<int> plotArea);
    void drawCorrelation (juce::Graphics& g, juce::Rectangle<int> plotArea);
    void drawGoniometer (juce::Graphics& g, juce::Rectangle<int> plotArea);

    WTAnalyzerAudioProcessor& processor;
    float uiScale = 1.0f;

    // Sub-view selector (radio group). Divergence is the default and the
    // only one implemented so far.
    juce::TextButton divergenceButton  { "Divergence"  };
    juce::TextButton correlationButton { "Correlation" };
    juce::TextButton goniometerButton  { "Goniometer"  };

    // Goniometer-only mode toggle, shown at the right of the header band
    // when the Goniometer sub-view is active.
    juce::TextButton gonioPrePostButton { "Pre / Post" };
    juce::TextButton gonioDiffButton    { "Difference" };

    // Last mouse position over the panel and whether the pointer is
    // inside it. The freq-axis sub-views (Divergence, Correlation)
    // turn this into the cursor readout strip below the plot.
    juce::Point<int> cursorPos;
    bool             cursorInside = false;

    // Cursor readout text, recomputed each paint by the active sub-view
    // and drawn in the strip below the plot.
    juce::String     hoverText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoDisplay)
};
