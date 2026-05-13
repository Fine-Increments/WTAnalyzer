/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
WTAnalyzerAudioProcessorEditor::WTAnalyzerAudioProcessorEditor (WTAnalyzerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    addAndMakeVisible (preDelayLabel);
    preDelayLabel.setText ("Pre-Effect delay:", juce::dontSendNotification);
    preDelayLabel.setJustificationType (juce::Justification::centredLeft);
    preDelayLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);
    preDelayLabel.setFont (juce::FontOptions (13.0f));

    addAndMakeVisible (preDelayEditor);
    preDelayEditor.setInputRestrictions (6, "0123456789");
    preDelayEditor.setJustification (juce::Justification::centredRight);
    preDelayEditor.setFont (juce::FontOptions (13.0f));
    preDelayEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111213));
    preDelayEditor.setColour (juce::TextEditor::textColourId,       juce::Colours::whitesmoke);
    preDelayEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff3a3d42));
    preDelayEditor.setText (juce::String ((int) *audioProcessor.apvts.getRawParameterValue ("preDelaySamples")),
                            juce::dontSendNotification);
    preDelayEditor.onReturnKey = [this] { commitDelayFromEditor(); };
    preDelayEditor.onFocusLost = [this] { commitDelayFromEditor(); };

    addAndMakeVisible (preDelaySuffix);
    preDelaySuffix.setText ("samples", juce::dontSendNotification);
    preDelaySuffix.setJustificationType (juce::Justification::centredLeft);
    preDelaySuffix.setColour (juce::Label::textColourId, juce::Colours::grey);
    preDelaySuffix.setFont (juce::FontOptions (13.0f));

    addAndMakeVisible (autoMeasureButton);
    autoMeasureButton.onClick = [this]
    {
        autoMeasureButton.setEnabled (false);
        autoMeasureButton.setButtonText ("...");
        audioProcessor.measureLatencyRequested.store (true, std::memory_order_release);
    };

    setSize (700, 490);
    startTimerHz (30);
}

void WTAnalyzerAudioProcessorEditor::commitDelayFromEditor()
{
    const int requested = preDelayEditor.getText().getIntValue();
    const int clamped   = juce::jlimit (0, WTAnalyzerAudioProcessor::kMaxDelaySamples, requested);

    if (auto* param = audioProcessor.apvts.getParameter ("preDelaySamples"))
    {
        const auto range      = param->getNormalisableRange();
        const float normalized = range.convertTo0to1 ((float) clamped);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalized);
        param->endChangeGesture();
    }

    preDelayEditor.setText (juce::String (clamped), juce::dontSendNotification);
}

WTAnalyzerAudioProcessorEditor::~WTAnalyzerAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void WTAnalyzerAudioProcessorEditor::timerCallback()
{
    repaint();

    // Pick up the result of an Auto measurement, if one just completed.
    if (audioProcessor.measureLatencyCompleted.exchange (false, std::memory_order_acq_rel))
    {
        const int measured = audioProcessor.lastMeasuredLatencyOffset.load (std::memory_order_acquire);
        applyMeasuredLatency (measured);
        autoMeasureButton.setButtonText ("Auto");
        autoMeasureButton.setEnabled (true);
    }

    // Reflect external parameter changes (host automation, preset load) back
    // into the editor field. Skip while the user is mid-edit to avoid stomping
    // on their typing.
    if (! preDelayEditor.hasKeyboardFocus (false))
    {
        const int current   = (int) *audioProcessor.apvts.getRawParameterValue ("preDelaySamples");
        const int displayed = preDelayEditor.getText().getIntValue();
        if (current != displayed)
            preDelayEditor.setText (juce::String (current), juce::dontSendNotification);
    }
}

void WTAnalyzerAudioProcessorEditor::applyMeasuredLatency (int samples)
{
    const int clamped = juce::jlimit (0, WTAnalyzerAudioProcessor::kMaxDelaySamples, samples);

    if (auto* param = audioProcessor.apvts.getParameter ("preDelaySamples"))
    {
        const auto range       = param->getNormalisableRange();
        const float normalized = range.convertTo0to1 ((float) clamped);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalized);
        param->endChangeGesture();
    }
}

void WTAnalyzerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202225));

    auto bounds = getLocalBounds().reduced (16);

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("WTAnalyzer",
                bounds.removeFromTop (24),
                juce::Justification::centredLeft);

    bounds.removeFromTop (8);

    // Spectrum overlay: log frequency on X, dB magnitude on Y, pre + post traces overlaid.
    auto spectrumArea = bounds.removeFromTop (280);
    {
        g.setColour (juce::Colour (0xff111213));
        g.fillRect (spectrumArea);

        const float sr = audioProcessor.currentSampleRate.load (std::memory_order_relaxed);
        constexpr float kMinFreq = 20.0f;
        const float     kMaxFreq = sr * 0.5f;
        const float     logMin   = std::log10 (kMinFreq);
        const float     logMax   = std::log10 (kMaxFreq);
        const float     logRange = logMax - logMin;

        constexpr float kMinDb = -80.0f;
        constexpr float kMaxDb = 6.0f;
        const float     dbRange = kMaxDb - kMinDb;

        // Grid lines: decade frequencies and standard dB levels.
        g.setColour (juce::Colour (0xff2a2d32));
        for (float freq : { 100.0f, 1000.0f, 10000.0f })
        {
            const float tx = (std::log10 (freq) - logMin) / logRange;
            const float x  = (float) spectrumArea.getX() + tx * (float) spectrumArea.getWidth();
            g.drawLine (x, (float) spectrumArea.getY(), x, (float) spectrumArea.getBottom(), 1.0f);
        }
        for (float db : { -60.0f, -40.0f, -20.0f, 0.0f })
        {
            const float ty = (db - kMinDb) / dbRange;
            const float y  = (float) spectrumArea.getY() + (1.0f - ty) * (float) spectrumArea.getHeight();
            g.drawLine ((float) spectrumArea.getX(), y, (float) spectrumArea.getRight(), y, 1.0f);
        }

        auto plotTrace = [&] (const std::array<float, WTAnalyzerAudioProcessor::kSpectrumBins>& spec,
                              juce::Colour colour)
        {
            const int   N             = WTAnalyzerAudioProcessor::kSpectrumBins;
            const float binFreqScale  = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

            juce::Path path;
            bool       first = true;

            for (int bin = 1; bin < N; ++bin)
            {
                const float f = (float) bin * binFreqScale;
                if (f < kMinFreq) continue;
                if (f > kMaxFreq) break;

                const float tx = (std::log10 (f) - logMin) / logRange;
                const float ty = (juce::jlimit (kMinDb, kMaxDb, spec[bin]) - kMinDb) / dbRange;

                const float x = (float) spectrumArea.getX() + tx * (float) spectrumArea.getWidth();
                const float y = (float) spectrumArea.getY() + (1.0f - ty) * (float) spectrumArea.getHeight();

                if (first) { path.startNewSubPath (x, y); first = false; }
                else       { path.lineTo          (x, y); }
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        };

        plotTrace (audioProcessor.preSpectrumDb,  juce::Colour (0xffe0a050));  // amber
        plotTrace (audioProcessor.postSpectrumDb, juce::Colour (0xff5cd4e8));  // cyan
    }

    bounds.removeFromTop (12);

    auto drawMeter = [&] (juce::Rectangle<int> row, const juce::String& label,
                          float db, bool active)
    {
        auto labelArea = row.removeFromLeft (110);
        g.setColour (active ? juce::Colours::whitesmoke : juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (label, labelArea, juce::Justification::centredLeft);

        auto meter = row.reduced (4, 6);
        g.setColour (juce::Colour (0xff111213));
        g.fillRect (meter);

        if (active)
        {
            const float clamped = juce::jlimit (-60.0f, 0.0f, db);
            const float ratio   = (clamped + 60.0f) / 60.0f;
            auto fill = meter.withWidth (juce::roundToInt ((float) meter.getWidth() * ratio));
            g.setColour (juce::Colours::limegreen);
            g.fillRect (fill);
        }

        g.setColour (juce::Colours::whitesmoke);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("(not routed)"),
                    meter.reduced (6, 0),
                    juce::Justification::centredRight);
    };

    drawMeter (bounds.removeFromTop (40), "Post-Effect",
               audioProcessor.postEffectLevelDb.load (std::memory_order_relaxed),
               true);

    bounds.removeFromTop (4);

    drawMeter (bounds.removeFromTop (40), "Pre-Effect",
               audioProcessor.preEffectLevelDb.load (std::memory_order_relaxed),
               audioProcessor.preBusActive.load (std::memory_order_relaxed));
}

void WTAnalyzerAudioProcessorEditor::resized()
{
    // Mirror the y-walk used in paint() so the delay row lines up with the
    // reserved space (header + spectrum + 2 meters + their gaps).
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (24);   // header
    bounds.removeFromTop (8);    // gap
    bounds.removeFromTop (280);  // spectrum
    bounds.removeFromTop (12);   // gap
    bounds.removeFromTop (40);   // post-effect meter
    bounds.removeFromTop (4);    // gap
    bounds.removeFromTop (40);   // pre-effect meter
    bounds.removeFromTop (12);   // gap

    auto delayRow = bounds.removeFromTop (30);
    preDelayLabel    .setBounds (delayRow.removeFromLeft (130));
    preDelayEditor   .setBounds (delayRow.removeFromLeft (90).reduced (0, 3));
    delayRow.removeFromLeft (6);
    preDelaySuffix   .setBounds (delayRow.removeFromLeft (60));
    delayRow.removeFromLeft (12);
    autoMeasureButton.setBounds (delayRow.removeFromLeft (70).reduced (0, 3));
}
