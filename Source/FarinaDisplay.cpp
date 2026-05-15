/*
  ==============================================================================

    FarinaDisplay.cpp

  ==============================================================================
*/

#include "FarinaDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>
#include <limits>

FarinaDisplay::FarinaDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    addAndMakeVisible (captureButton);
    captureButton.onClick = [this]
    {
        processor.farinaIR.requestCapture();
        repaint();
    };

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        processor.farinaIR.reset();
        repaint();
    };

    auto setupSlider = [this] (juce::Slider& s, juce::Label& label,
                               const juce::String& suffix)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, sx (60), sx (18));
        s.setTextValueSuffix (suffix);

        addAndMakeVisible (label);
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredRight);
    };

    setupSlider (f0Slider,    f0Label,    " Hz");
    setupSlider (f1Slider,    f1Label,    " Hz");
    setupSlider (sweepSlider, sweepLabel, " s");
    setupSlider (tailSlider,  tailLabel,  " s");

    f0Attachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaF0Hz",     f0Slider);
    f1Attachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaF1Hz",     f1Slider);
    sweepAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaSweepSec", sweepSlider);
    tailAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaTailSec",  tailSlider);

    startTimerHz (10);   // poll FarinaIR state + nudge deconvolution
}

FarinaDisplay::~FarinaDisplay()
{
    stopTimer();
}

void FarinaDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void FarinaDisplay::timerCallback()
{
    // Run any pending deconvolution. tryProcessCapture() is a no-op if
    // the audio thread hasn't finished a capture yet, so this is cheap.
    processor.farinaIR.tryProcessCapture();
    repaint();
}

juce::String FarinaDisplay::statusText() const
{
    const auto& f = processor.farinaIR;
    switch (f.getState())
    {
        case FarinaIR::State::Idle:           return "Idle - click Capture to arm";
        case FarinaIR::State::Armed:          return "Waiting for sweep onset";
        case FarinaIR::State::Capturing:
        {
            const int prog = f.getCaptureProgress();
            const int len  = f.getCaptureLength();
            return "Capturing " + juce::String (prog) + " / " + juce::String (len);
        }
        case FarinaIR::State::ReadyToProcess: return "Processing...";
        case FarinaIR::State::IRReady:        return "IR ready";
    }
    return {};
}

void FarinaDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (88));
    auto topRow = header.removeFromTop (sx (28));
    auto botRow = header;

    juce::ignoreUnused (topRow);   // status text drawn directly in paint()

    // Bottom row: capture + clear buttons on the left, four slider rows
    // stacked on the right.
    const int buttonW = sx (70);
    const int buttonH = sx (22);
    const int buttonSpacing = sx (6);

    auto leftArea = botRow.removeFromLeft (sx (170));
    leftArea.removeFromLeft (sx (16));

    captureButton.setBounds (leftArea.getX(),
                             leftArea.getCentreY() - buttonH - sx (3),
                             buttonW, buttonH);
    clearButton  .setBounds (leftArea.getX() + buttonW + buttonSpacing,
                             leftArea.getCentreY() - buttonH - sx (3),
                             buttonW, buttonH);

    botRow.removeFromLeft (sx (16));
    auto sliderArea = botRow;
    sliderArea.removeFromRight (sx (12));

    const int labelW   = sx (48);
    const int rowCount = 4;
    const int rowH     = sliderArea.getHeight() / rowCount;

    auto layoutSliderRow = [&] (juce::Slider& s, juce::Label& label)
    {
        auto row = sliderArea.removeFromTop (rowH);
        label.setBounds (row.removeFromLeft (labelW));
        s    .setBounds (row);
    };

    layoutSliderRow (f0Slider,    f0Label);
    layoutSliderRow (f1Slider,    f1Label);
    layoutSliderRow (sweepSlider, sweepLabel);
    layoutSliderRow (tailSlider,  tailLabel);
}

void FarinaDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    auto header = bounds.removeFromTop (sx (88));
    auto topRow = header.removeFromTop (sx (28));

    g.setColour (WTColors::analysis);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText (statusText(),
                topRow.withTrimmedLeft (sx (16)),
                juce::Justification::centredLeft, false);

    // Sweep summary on the right - quick-glance reference for what
    // the captured IR is measuring.
    const auto& f = processor.farinaIR;
    auto formatHz = [] (float hz) -> juce::String
    {
        if      (hz < 1000.0f)  return juce::String ((int) std::round (hz)) + " Hz";
        else if (hz < 10000.0f) return juce::String (hz / 1000.0f, 2) + " kHz";
        else                    return juce::String (hz / 1000.0f, 1) + " kHz";
    };

    const juce::String summary = formatHz (f.getF0Hz()) + "  to  "
                               + formatHz (f.getF1Hz()) + "  over  "
                               + juce::String (f.getSweepSec(), 1) + " s"
                               + "   tail " + juce::String (f.getTailSec(), 2) + " s";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));
    g.drawText (summary,
                topRow.withTrimmedRight (sx (16)),
                juce::Justification::centredRight, false);

    drawWaveform (g, bounds.reduced (sx (24), sx (12)));
}

void FarinaDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& f = processor.farinaIR;
    const int   N = f.getIRLength();
    const float sr = f.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Set sweep parameters, click Capture, then play a log sine sweep through the device",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const auto& buffer = f.getIR();
    const float windowMs = (float) N * 1000.0f / sr;

    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
        peak = std::max (peak, std::abs (buffer[(size_t) i]));

    if (peak < 1.0e-6f) peak = 1.0e-6f;
    const float yRange = peak * 1.1f;

    auto sampleToY = [&] (float v) -> float
    {
        const float tt = 0.5f - 0.5f * (juce::jlimit (-yRange, yRange, v) / yRange);
        return (float) plotArea.getY() + tt * (float) plotArea.getHeight();
    };

    g.setColour (juce::Colour (0xff2a2d32));
    {
        const float yZero = sampleToY (0.0f);
        g.drawLine ((float) plotArea.getX(), yZero, (float) plotArea.getRight(), yZero, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    auto drawYLabel = [&] (float v, const juce::String& text)
    {
        const int y = (int) sampleToY (v);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (text, r, juce::Justification::centredRight, false);
    };
    drawYLabel ( peak, juce::String (peak, 3));
    drawYLabel ( 0.0f, "0");
    drawYLabel (-peak, "-" + juce::String (peak, 3));

    auto drawXLabel = [&] (float tNorm, float ms)
    {
        const int x = plotArea.getX() + juce::roundToInt (tNorm * (float) plotArea.getWidth());
        const int textW = sx (44);
        juce::Rectangle<int> r (x - textW / 2,
                                labelGutterBottom.getY(),
                                textW,
                                labelGutterBottom.getHeight());

        juce::String text;
        if      (ms >= 1000.0f) text = juce::String (ms / 1000.0f, 2) + " s";
        else if (ms >= 10.0f)   text = juce::String ((int) std::round (ms)) + " ms";
        else                    text = juce::String (ms, 1) + " ms";

        g.drawText (text, r, juce::Justification::centredTop, false);
    };
    drawXLabel (0.00f, 0.0f);
    drawXLabel (0.25f, windowMs * 0.25f);
    drawXLabel (0.50f, windowMs * 0.50f);
    drawXLabel (0.75f, windowMs * 0.75f);
    drawXLabel (1.00f, windowMs);

    const int   plotW           = plotArea.getWidth();
    const float samplesPerPixel = (float) N / (float) std::max (1, plotW);

    g.setColour (WTColors::analysis);

    if (samplesPerPixel <= 1.0f)
    {
        juce::Path path;
        for (int i = 0; i < N; ++i)
        {
            const float tNorm = (float) i / (float) std::max (1, N - 1);
            const float x = (float) plotArea.getX() + tNorm * (float) plotW;
            const float y = sampleToY (buffer[(size_t) i]);
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo          (x, y);
        }
        g.strokePath (path, juce::PathStrokeType (sf (1.2f)));
    }
    else
    {
        for (int px = 0; px < plotW; ++px)
        {
            const int i0 = (int) ((float) px * samplesPerPixel);
            const int i1 = std::min (N, (int) ((float) (px + 1) * samplesPerPixel));
            if (i1 <= i0) continue;

            float minV =  std::numeric_limits<float>::infinity();
            float maxV = -std::numeric_limits<float>::infinity();
            for (int i = i0; i < i1; ++i)
            {
                const float v = buffer[(size_t) i];
                if (v < minV) minV = v;
                if (v > maxV) maxV = v;
            }

            const float x  = (float) (plotArea.getX() + px);
            const float y0 = sampleToY (maxV);
            const float y1 = sampleToY (minV);
            g.drawLine (x, y0, x, std::max (y1, y0 + 1.0f), sf (1.0f));
        }
    }
}
