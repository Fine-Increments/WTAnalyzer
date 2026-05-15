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
#include "CursorReadout.h"
#include "THDDisplay.h"
#include "IMDDisplay.h"
#include "ImpulseDisplay.h"
#include "FarinaDisplay.h"
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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyAnalysisMode (int modeIndex);

    float scale() const noexcept
    {
        return juce::jmin ((float) getWidth()  / (float) kBaseWidth,
                           (float) getHeight() / (float) kBaseHeight);
    }

    int   sx (int   v) const noexcept { return juce::roundToInt ((float) v * scale()); }
    float sf (float v) const noexcept { return v * scale(); }

    WTAnalyzerAudioProcessor& audioProcessor;
    WTLookAndFeel lookAndFeel;

    // Spectrum-based display path (Generic Overlay, Frequency Response).
    SpectrumDisplay  spectrumDisplay;
    CursorReadout    cursorReadout;

    // THD-mode display path.
    THDDisplay       thdDisplay;

    // IMD-mode display path.
    IMDDisplay       imdDisplay;

    // Impulse Response display path. First time-domain panel.
    ImpulseDisplay   impulseDisplay;

    // Farina IR display path - sister panel, same time-domain IR plot.
    FarinaDisplay    farinaDisplay;

    LevelMetersPanel levelMetersPanel;
    LatencyPanel     latencyPanel;

    int lastAppliedAnalysisMode = -1;   // forces an initial visibility update

    juce::ComboBox analysisSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analysisAttachment;

    // Sidecar JSON loader. Lives in the header alongside the analysis
    // selector. Currently transient (path not persisted across sessions);
    // future iteration can add persistence via the plugin state tree.
    juce::TextButton sidecarButton;
    void chooseSidecarFile();
    void updateSidecarButtonText();

    // Help button - opens a detailed mode-specific instructions popup
    // for whichever analysis is currently selected. Replaces the prior
    // inline-caption approach; see feedback-ui-no-instructions memory.
    juce::TextButton helpButton;
    void openHelpDialog();
    static juce::String getModeHelpText (int modeIndex);
    static juce::String getModeName (int modeIndex);

    // Capture / Clear pair lives in the header for consistency across
    // modes; the two pairs (FR sweep recorder vs Farina IR one-shot
    // trigger) share the same header slot, with visibility decided by
    // applyAnalysisMode based on which mode is currently active. They
    // do conceptually different things but the visual location stays
    // the same so the user doesn't have to hunt for action buttons.
    juce::TextButton sweepCaptureButton  { "Capture" };
    juce::TextButton sweepClearButton    { "Clear" };
    juce::TextButton farinaCaptureButton { "Capture" };
    juce::TextButton farinaClearButton   { "Clear" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sweepCaptureAttachment;

    // L / R / Diff toggle row - shared across every mode that uses the
    // stereo display convention (PLANNING.md 8.5.1). L and R are
    // independent on/off; at least one must stay on (auto-flip-back
    // enforced in the click handler). Diff is an additive overlay.
    // Lives in the readout strip below the spectrum, alongside the
    // cursor x/y readout, and is hidden in modes that don't participate
    // in this convention yet.
    juce::TextButton stereoLButton    { "L" };
    juce::TextButton stereoRButton    { "R" };
    juce::TextButton stereoDiffButton { "Diff" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoLAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoRAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoDiffAttachment;

    // Inline stereo-imbalance readout. Sits in the readout strip between
    // the cursor readout (left) and the L / R / Diff toggles (right).
    // Format is mode-specific: spectrum-based modes show "max diff at Hz",
    // bar modes show numeric L vs R deltas, IR modes show max sample diff.
    // Replaces the planned popup dashboard - keeping it inline avoids
    // having to run every analysis concurrently to populate cross-mode
    // numbers (only the active mode's analysis runs per spectrum hop).
    juce::Label imbalanceReadout;
    void updateImbalanceReadout();
    juce::String computeImbalanceText() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessorEditor)
};
