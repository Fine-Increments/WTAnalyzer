/*
  ==============================================================================

    ImpulseDisplay.cpp

  ==============================================================================
*/

#include "ImpulseDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>

ImpulseDisplay::ImpulseDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        processor.impulseResponse.reset();
        repaint();
    };

    auto setupSlider = [this] (juce::Slider& s, int minVal, int maxVal, juce::Label& label, const juce::String& suffix)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setRange ((double) minVal, (double) maxVal, 1.0);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, sx (64), sx (20));
        s.setTextValueSuffix (suffix);

        addAndMakeVisible (label);
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredRight);
    };

    setupSlider (windowSlider,   ImpulseResponse::kMinWindowMs, ImpulseResponse::kMaxWindowMs,
                 windowLabel,    " ms");
    setupSlider (averagesSlider, ImpulseResponse::kMinAverages, ImpulseResponse::kMaxAverages,
                 averagesLabel,  "");

    windowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "irWindowMs", windowSlider);
    averagesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "irAverageCount", averagesSlider);

    // 120-second range is huge; without skew the slider's lower half is
    // useless. Skew so 1000 ms lands roughly mid-track.
    windowSlider.setSkewFactorFromMidPoint (1000.0);

    startTimerHz (30);
}

ImpulseDisplay::~ImpulseDisplay()
{
    stopTimer();
}

void ImpulseDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void ImpulseDisplay::timerCallback()
{
    repaint();
}

void ImpulseDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (28));
    auto botRow = header;

    juce::ignoreUnused (topRow);   // paint() draws the text readouts directly

    // Bottom row: Clear button on the far left, then Window and Averages
    // slider rows on the right half.
    const int buttonW = sx (62);
    const int buttonH = sx (22);
    clearButton.setBounds (botRow.getX() + sx (16),
                           botRow.getCentreY() - buttonH / 2,
                           buttonW, buttonH);

    auto controlArea = botRow.removeFromRight (botRow.getWidth() - sx (96));
    controlArea.removeFromLeft (sx (16));

    const int labelW   = sx (60);
    const int sliderH  = sx (18);
    const int sliderY1 = controlArea.getY() + sx (3);
    const int sliderY2 = controlArea.getY() + sx (25);

    juce::ignoreUnused (sliderY2);

    // Stack the two sliders vertically inside the 44 px bot row.
    const int rowHeight = botRow.getHeight() / 2;
    auto winRow = controlArea.removeFromTop (rowHeight);
    auto avgRow = controlArea;

    windowLabel  .setBounds (winRow.removeFromLeft (labelW));
    windowSlider .setBounds (winRow);
    averagesLabel.setBounds (avgRow.removeFromLeft (labelW));
    averagesSlider.setBounds (avgRow);

    juce::ignoreUnused (sliderH, sliderY1);
}

void ImpulseDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    const auto& ir = processor.impulseResponse;
    const int   completed = ir.getCompletedCaptures();
    const int   goal      = ir.getAverageGoal();
    const int   windowMs  = (int) *processor.apvts.getRawParameterValue ("irWindowMs");

    // Header text: averaging progress on left, window length on right.
    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (28));
    bounds.removeFromTop (0);   // botRow is owned by the controls

    const juce::String progressText =
        completed == 0
            ? juce::String ("Waiting for impulse trigger")
            : (juce::String (completed) + " / " + juce::String (goal) + " averaged");

    g.setColour (WTColors::analysis);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText (progressText,
                topRow.withTrimmedLeft (sx (16)),
                juce::Justification::centredLeft, false);

    juce::String windowText;
    if (windowMs >= 1000)
        windowText = juce::String (windowMs / 1000.0f, 2) + " s";
    else
        windowText = juce::String (windowMs) + " ms";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("Window  " + windowText,
                topRow.withTrimmedRight (sx (16)),
                juce::Justification::centredRight, false);

    // Plot area starts below the 72-px header and the 12-px gap and goes
    // all the way to the bottom of the panel.
    drawWaveform (g, bounds.reduced (sx (24), sx (12)));
}

void ImpulseDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& ir = processor.impulseResponse;
    const int   N  = ir.getDisplayLength();
    const float sr = ir.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        // No data yet - just draw an empty plot with axes.
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Play an impulse train through the device under test",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const float windowMs = (float) N * 1000.0f / sr;
    const auto& buffer   = ir.getAveragedBuffer();

    // Find peak for Y-axis scaling. Plot Y is symmetric around zero.
    float peak = 0.0f;
    for (int i = 0; i < N; ++i)
        peak = std::max (peak, std::abs (buffer[(size_t) i]));

    if (peak < 1.0e-6f) peak = 1.0e-6f;
    const float yRange = peak * 1.1f;   // 10% headroom

    auto sampleToY = [&] (float v) -> float
    {
        const float t = 0.5f - 0.5f * (juce::jlimit (-yRange, yRange, v) / yRange);
        return (float) plotArea.getY() + t * (float) plotArea.getHeight();
    };

    // Center line (zero amplitude).
    g.setColour (juce::Colour (0xff2a2d32));
    {
        const float yZero = sampleToY (0.0f);
        g.drawLine ((float) plotArea.getX(), yZero, (float) plotArea.getRight(), yZero, sf (1.0f));
    }

    // Y-axis labels: +peak, 0, -peak.
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

    // X-axis labels: 0, 1/4, 1/2, 3/4, full window.
    auto drawXLabel = [&] (float tNorm, float ms)
    {
        const int x = plotArea.getX() + juce::roundToInt (tNorm * (float) plotArea.getWidth());
        const int textW = sx (44);
        juce::Rectangle<int> r (x - textW / 2,
                                labelGutterBottom.getY(),
                                textW,
                                labelGutterBottom.getHeight());

        juce::String text;
        if (ms >= 1000.0f) text = juce::String (ms / 1000.0f, 2) + " s";
        else if (ms >= 10.0f) text = juce::String ((int) std::round (ms)) + " ms";
        else               text = juce::String (ms, 1) + " ms";

        g.drawText (text, r, juce::Justification::centredTop, false);
    };
    drawXLabel (0.00f, 0.0f);
    drawXLabel (0.25f, windowMs * 0.25f);
    drawXLabel (0.50f, windowMs * 0.50f);
    drawXLabel (0.75f, windowMs * 0.75f);
    drawXLabel (1.00f, windowMs);

    // Waveform path. For long windows N can be millions of samples while
    // the plot is ~1000 pixels wide, so we decimate by walking
    // samples-per-pixel chunks and tracking each chunk's min/max for a
    // proper waveform overview.
    const int   plotW    = plotArea.getWidth();
    const float samplesPerPixel = (float) N / (float) std::max (1, plotW);

    g.setColour (WTColors::analysis);

    if (samplesPerPixel <= 1.0f)
    {
        // Few samples - draw as connected line.
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
        // Many samples per pixel - draw min/max vertical bars per column.
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
