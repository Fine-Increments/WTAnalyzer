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
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (preDelayLabel);
    preDelayLabel.setText ("Pre-Effect delay:", juce::dontSendNotification);
    preDelayLabel.setJustificationType (juce::Justification::centredLeft);
    preDelayLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);

    addAndMakeVisible (preDelayEditor);
    preDelayEditor.setInputRestrictions (6, "0123456789");
    preDelayEditor.setJustification (juce::Justification::centredRight);
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

    addAndMakeVisible (autoMeasureButton);
    autoMeasureButton.onClick = [this]
    {
        autoMeasureButton.setEnabled (false);
        autoMeasureButton.setButtonText ("...");
        audioProcessor.measureLatencyRequested.store (true, std::memory_order_release);
    };

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    setSize (kBaseWidth, kBaseHeight);

    startTimerHz (30);
}

WTAnalyzerAudioProcessorEditor::~WTAnalyzerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);  // detach before LookAndFeel member destructs
    stopTimer();
}

void WTAnalyzerAudioProcessorEditor::commitDelayFromEditor()
{
    const int requested = preDelayEditor.getText().getIntValue();
    const int clamped   = juce::jlimit (0, WTAnalyzerAudioProcessor::kMaxDelaySamples, requested);

    if (auto* param = audioProcessor.apvts.getParameter ("preDelaySamples"))
    {
        const auto range       = param->getNormalisableRange();
        const float normalized = range.convertTo0to1 ((float) clamped);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalized);
        param->endChangeGesture();
    }

    preDelayEditor.setText (juce::String (clamped), juce::dontSendNotification);
}

//==============================================================================
void WTAnalyzerAudioProcessorEditor::timerCallback()
{
    repaint();

    if (audioProcessor.measureLatencyCompleted.exchange (false, std::memory_order_acq_rel))
    {
        const int measured = audioProcessor.lastMeasuredLatencyOffset.load (std::memory_order_acquire);
        applyMeasuredLatency (measured);
        autoMeasureButton.setButtonText ("Auto");
        autoMeasureButton.setEnabled (true);
    }

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

//==============================================================================
void WTAnalyzerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202225));

    auto bounds = getLocalBounds().reduced (sx (16));

    // Header.
    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText ("WTAnalyzer",
                bounds.removeFromTop (sx (24)),
                juce::Justification::centredLeft);

    bounds.removeFromTop (sx (8));

    // Reserve the controls strip at the bottom; spectrum gets all remaining height.
    auto delayRow = bounds.removeFromBottom (sx (30));
    bounds.removeFromBottom (sx (12));
    auto preMeterRow  = bounds.removeFromBottom (sx (40));
    bounds.removeFromBottom (sx (4));
    auto postMeterRow = bounds.removeFromBottom (sx (40));
    bounds.removeFromBottom (sx (12));

    juce::ignoreUnused (delayRow);  // delay row controls are positioned in resized().

    // Spectrum plot fills whatever height is left.
    {
        auto spectrumArea = bounds;
        g.setColour (juce::Colour (0xff111213));
        g.fillRect (spectrumArea);

        // Reserve gutters for axis labels (scaled).
        const int dbGutter   = sx (34);
        const int freqGutter = sx (16);

        auto labelGutterBottom = spectrumArea.removeFromBottom (freqGutter);
        auto labelGutterLeft   = spectrumArea.removeFromLeft  (dbGutter);
        const auto plotArea    = spectrumArea;  // what remains after gutters

        const float sr = audioProcessor.currentSampleRate.load (std::memory_order_relaxed);
        constexpr float kMinFreq = 20.0f;
        const float     kMaxFreq = sr * 0.5f;
        const float     logMin   = std::log10 (kMinFreq);
        const float     logMax   = std::log10 (kMaxFreq);
        const float     logRange = logMax - logMin;

        constexpr float kMinDb = -80.0f;
        constexpr float kMaxDb = 6.0f;
        const float     dbRange = kMaxDb - kMinDb;

        auto freqToX = [&] (float freq) -> float
        {
            const float t = (std::log10 (freq) - logMin) / logRange;
            return (float) plotArea.getX() + t * (float) plotArea.getWidth();
        };

        auto dbToY = [&] (float db) -> float
        {
            const float t = (db - kMinDb) / dbRange;
            return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
        };

        // Grid lines.
        g.setColour (juce::Colour (0xff2a2d32));
        const std::array<float, 6> kFreqGrid { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f };
        for (float f : kFreqGrid)
        {
            if (f < kMinFreq || f > kMaxFreq) continue;
            const float x = freqToX (f);
            g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
        }
        const std::array<float, 5> kDbGrid { -60.0f, -40.0f, -20.0f, 0.0f, 6.0f };
        for (float db : kDbGrid)
        {
            const float y = dbToY (db);
            g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
        }

        // Axis labels.
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (10.0f)));

        // Frequency labels along the bottom (only "round" values that fit).
        struct FreqLabel { float hz; const char* text; };
        const std::array<FreqLabel, 8> kFreqLabels {{
            { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,  "100" },
            { 500.0f,   "500" }, { 1000.0f,  "1k"   }, { 5000.0f, "5k"  },
            { 10000.0f, "10k" }, { 20000.0f, "20k"  }
        }};
        for (auto label : kFreqLabels)
        {
            if (label.hz < kMinFreq || label.hz > kMaxFreq) continue;
            const float x = freqToX (label.hz);
            const int textWidth = sx (32);
            juce::Rectangle<int> r ((int) x - textWidth / 2,
                                    labelGutterBottom.getY(),
                                    textWidth,
                                    labelGutterBottom.getHeight());
            g.drawText (label.text, r, juce::Justification::centredTop, false);
        }

        // dB labels along the left.
        struct DbLabel { float db; const char* text; };
        const std::array<DbLabel, 5> kDbLabels {{
            { 0.0f,   "0"   }, { -20.0f, "-20" }, { -40.0f, "-40" },
            { -60.0f, "-60" }, { -80.0f, "-80" }
        }};
        for (auto label : kDbLabels)
        {
            const float y = dbToY (label.db);
            const int textHeight = sx (12);
            juce::Rectangle<int> r (labelGutterLeft.getX(),
                                    (int) y - textHeight / 2,
                                    labelGutterLeft.getWidth() - sx (4),
                                    textHeight);
            g.drawText (label.text, r, juce::Justification::centredRight, false);
        }

        // Spectrum traces.
        auto plotTrace = [&] (const std::array<float, WTAnalyzerAudioProcessor::kSpectrumBins>& spec,
                              juce::Colour colour)
        {
            const int   N            = WTAnalyzerAudioProcessor::kSpectrumBins;
            const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

            juce::Path path;
            bool       first = true;

            for (int bin = 1; bin < N; ++bin)
            {
                const float f = (float) bin * binFreqScale;
                if (f < kMinFreq) continue;
                if (f > kMaxFreq) break;

                const float clampedDb = juce::jlimit (kMinDb, kMaxDb, spec[bin]);
                const float x = freqToX (f);
                const float y = dbToY (clampedDb);

                if (first) { path.startNewSubPath (x, y); first = false; }
                else       { path.lineTo          (x, y); }
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (sf (1.2f)));
        };

        plotTrace (audioProcessor.preSpectrumDb,  juce::Colour (0xffe0a050));  // pre = amber
        plotTrace (audioProcessor.postSpectrumDb, juce::Colour (0xff5cd4e8));  // post = cyan
    }

    // Meters.
    auto drawMeter = [&] (juce::Rectangle<int> row, const juce::String& label,
                          float db, bool active)
    {
        auto labelArea = row.removeFromLeft (sx (110));
        g.setColour (active ? juce::Colours::whitesmoke : juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (14.0f)));
        g.drawText (label, labelArea, juce::Justification::centredLeft);

        auto meter = row.reduced (sx (4), sx (6));
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
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("(not routed)"),
                    meter.reduced (sx (6), 0),
                    juce::Justification::centredRight);
    };

    drawMeter (postMeterRow, "Post-Effect",
               audioProcessor.postEffectLevelDb.load (std::memory_order_relaxed),
               true);

    drawMeter (preMeterRow, "Pre-Effect",
               audioProcessor.preEffectLevelDb.load (std::memory_order_relaxed),
               audioProcessor.preBusActive.load (std::memory_order_relaxed));
}

void WTAnalyzerAudioProcessorEditor::resized()
{
    const float s = scale();
    lookAndFeel.setUiScale (s);

    // Update directly-set component fonts to track the new scale.
    preDelayLabel .setFont (juce::FontOptions (sf (13.0f)));
    preDelayEditor.setFont (juce::FontOptions (sf (13.0f)));
    preDelaySuffix.setFont (juce::FontOptions (sf (13.0f)));

    // Mirror paint()'s top-then-bottom layout walk.
    auto bounds = getLocalBounds().reduced (sx (16));
    bounds.removeFromTop    (sx (24));   // header
    bounds.removeFromTop    (sx (8));    // gap

    auto delayRow = bounds.removeFromBottom (sx (30));
    bounds.removeFromBottom (sx (12));
    bounds.removeFromBottom (sx (40));   // pre meter
    bounds.removeFromBottom (sx (4));
    bounds.removeFromBottom (sx (40));   // post meter
    bounds.removeFromBottom (sx (12));
    // bounds now holds the spectrum area; not needed for layout.

    preDelayLabel    .setBounds (delayRow.removeFromLeft (sx (130)));
    preDelayEditor   .setBounds (delayRow.removeFromLeft (sx (90)).reduced (0, sx (3)));
    delayRow.removeFromLeft (sx (6));
    preDelaySuffix   .setBounds (delayRow.removeFromLeft (sx (60)));
    delayRow.removeFromLeft (sx (12));
    autoMeasureButton.setBounds (delayRow.removeFromLeft (sx (70)).reduced (0, sx (3)));
}
