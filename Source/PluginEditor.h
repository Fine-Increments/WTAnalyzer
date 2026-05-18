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
#include "StereoDisplay.h"
#include "SweepCurveDisplay.h"
#include "PhaseDisplay.h"
#include "DynamicsDisplay.h"
#include "MlsDisplay.h"
#include "StepResponseDisplay.h"
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
    WTLookAndFeel()
    {
        // Every toggle button (view selectors, on/off actions) reads as
        // engaged via a light fill with dark text - a clear, consistent
        // active state across all modes. The stock dark-on-dark on-state
        // was nearly indistinguishable from the inactive buttons.
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffcfd2d6));
        setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    }

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
// Full-panel notice shown over the active display whenever the sidechain
// (pre-effect) input is not connected. WTAnalyzer's entire premise is a
// pre-vs-post comparison, so without the sidechain no mode can do its job -
// this overlay tells the user consistently, in every mode, rather than
// each panel quietly drawing a meaningless flat result.
class SidechainNotice  : public juce::Component
{
public:
    SidechainNotice()
    {
        setOpaque (true);
        // Swallow clicks so stray presses don't reach the hidden panel
        // controls underneath.
        setInterceptsMouseClicks (true, false);
    }

    void setUiScale (float s) noexcept { uiScale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111213));

        auto area = getLocalBounds().reduced (juce::roundToInt (24.0f * uiScale));

        g.setColour (juce::Colours::whitesmoke);
        g.setFont (juce::FontOptions (16.0f * uiScale));
        auto headline = area.removeFromTop (juce::roundToInt (28.0f * uiScale));
        g.drawText ("Sidechain not connected", headline,
                    juce::Justification::centred, false);

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (12.0f * uiScale));
        g.drawFittedText (
            "Route the dry (pre-effect) signal into WTAnalyzer's sidechain input. "
            "Every analysis mode compares the pre-effect and post-effect signals, "
            "so the sidechain must be wired for any measurement to work.",
            area, juce::Justification::centredTop, 3);
    }

private:
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainNotice)
};

//==============================================================================
// Small inline stereo-imbalance readout. Shows a grey metric label followed
// by an L value and an R value, each tinted with its channel colour. Painted
// via AttributedString so the two values can carry different colours within
// a single component (a plain juce::Label is single-colour only).
class ImbalanceReadout  : public juce::Component
{
public:
    ImbalanceReadout() { setInterceptsMouseClicks (false, false); }

    void setUiScale (float s) noexcept { uiScale = s; repaint(); }

    void setContent (juce::String metricLabel,
                     juce::String lText, juce::Colour lColour,
                     juce::String rText, juce::Colour rColour)
    {
        metric   = std::move (metricLabel);
        leftStr  = std::move (lText);   leftColour  = lColour;
        rightStr = std::move (rText);   rightColour = rColour;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Font font (juce::FontOptions (11.0f * uiScale));

        juce::AttributedString as;
        as.setJustification (juce::Justification::centredLeft);
        if (metric.isNotEmpty())
            as.append (metric + "   ", font, juce::Colours::grey);
        if (leftStr.isNotEmpty())
            as.append (leftStr + "    ", font, leftColour);
        if (rightStr.isNotEmpty())
            as.append (rightStr, font, rightColour);

        as.draw (g, getLocalBounds().toFloat());
    }

private:
    float uiScale = 1.0f;
    juce::String metric, leftStr, rightStr;
    juce::Colour leftColour  { juce::Colours::grey };
    juce::Colour rightColour { juce::Colours::grey };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImbalanceReadout)
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

    // Spectrum-based display path (Frequency Response, Aliasing Detection).
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

    // Stereo Image display path - per-frequency stereo divergence.
    StereoDisplay    stereoDisplay;

    // Parameter Sweep display path - Plugin Doctor style 1D X-Y curve.
    SweepCurveDisplay sweepCurveDisplay;

    // Phase Response display path - phase + group delay over log frequency.
    PhaseDisplay     phaseDisplay;

    // Dynamics display path - the device's input-vs-output transfer curve.
    DynamicsDisplay  dynamicsDisplay;

    // MLS IR display path - sibling time-domain IR plot, MLS-acquired.
    MlsDisplay       mlsDisplay;

    // Step Response display path - the captured step waveform plus the
    // derived rise-time / overshoot metrics.
    StepResponseDisplay stepResponseDisplay;

    // Shown over whichever panel is active when the sidechain input is
    // not connected. Visibility is driven from the timer.
    SidechainNotice  sidechainNotice;

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
    juce::TextButton mlsCaptureButton    { "Capture" };
    juce::TextButton mlsClearButton      { "Clear" };
    juce::TextButton stepCaptureButton   { "Capture" };
    juce::TextButton stepClearButton     { "Clear" };
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
    // Shows a mode-specific metric label plus per-channel L / R values,
    // each tinted with its channel colour. Replaces the planned popup
    // dashboard - keeping it inline avoids having to run every analysis
    // concurrently to populate cross-mode numbers (only the active
    // mode's analysis runs per spectrum hop).
    ImbalanceReadout imbalanceReadout;

    // Structured per-mode imbalance summary: a metric label plus L / R
    // value strings and their channel colours.
    struct ImbalanceContent
    {
        juce::String metric;
        juce::String lText;
        juce::String rText;
        juce::Colour lColour { juce::Colours::grey };
        juce::Colour rColour { juce::Colours::grey };
    };

    void updateImbalanceReadout();
    ImbalanceContent computeImbalanceContent() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessorEditor)
};
