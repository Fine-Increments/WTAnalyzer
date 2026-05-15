/*
  ==============================================================================

    THDDisplay.h
    Mode-specific display for THDMeasurement.

    Layout:
      - Header band split into two halves.
          Left half:  big differential THD% (top), Hold + Freeze buttons.
          Right half: fundamental subline (top), view-toggle buttons
                      (Differential / Pre / Post).
      - Bar chart fills the rest of the panel.

    The THD% number is always the differential measurement (added harmonic
    energy in post relative to pre's fundamental). Pre and Post views are
    visualisations of each signal's own harmonic content.

    Hold accumulates per-harmonic peak values and the peak THD% across
    spectrum frames. Toggling Hold off then on re-arms the peak with the
    next live frame. Freeze pauses the rendered values entirely. Both
    states are local to the display - the underlying measurement keeps
    running, so leaving the panel and returning resumes live values
    immediately.

    Visible only when activeAnalysis is set to THD Measurement; PluginEditor
    handles the panel-swap visibility.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analyses/THDMeasurement.h"

class THDDisplay  : public juce::Component,
                    private juce::Timer,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit THDDisplay (WTAnalyzerAudioProcessor& proc);
    ~THDDisplay() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * uiScale); }
    float sf (float v) const noexcept { return v * uiScale; }

    // Snapshot of every value the bar chart + readouts need, so the rendering
    // path is decoupled from the audio-side THDMeasurement. timerCallback
    // refreshes / peak-holds this; paint() only reads it.
    //
    // Stereo: every per-channel field is stored once for L (master, suffix
    // omitted) and once for R. Diff bars are computed from the held L and
    // R arrays at paint time.
    struct ChannelFrame
    {
        bool  valid             = false;
        float thdPercent        = 0.0f;
        float fundamentalHz     = 0.0f;
        float preFundamentalDb  = THDMeasurement::kNoMeasurementDb;
        float postFundamentalDb = THDMeasurement::kNoMeasurementDb;
        int   numValidHarmonics = 0;

        // Indexed by Source enum value (Diff=0, Pre=1, Post=2), then harmonic
        // index (0 = h1 fundamental, 1 = h2, ...). Value is the displayed
        // ratio (per-source-fundamental ratio for Pre/Post, added-energy
        // ratio for Diff).
        std::array<std::array<float, THDMeasurement::kMaxHarmonics>, 3> ratioDb {};
    };

    struct DisplayFrame
    {
        ChannelFrame L;
        ChannelFrame R;

        // Either channel valid is sufficient for "show me the bars".
        bool anyValid() const noexcept { return L.valid || R.valid; }
    };

    DisplayFrame sampleProcessor() const;
    static int   sourceIndex (THDMeasurement::Source s) noexcept;

    void drawHarmonicBars (juce::Graphics& g, juce::Rectangle<int> area);

    THDMeasurement::Source currentViewSource() const noexcept;
    // Returns the L and R bar colours appropriate for the currently
    // selected source view (Diff -> analysis pair, Pre -> preEffect pair,
    // Post -> postEffect pair).
    struct ViewColours { juce::Colour L; juce::Colour R; };
    ViewColours              currentViewColours() const noexcept;
    void                     syncToggleButtons();

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (THDDisplay)
};
