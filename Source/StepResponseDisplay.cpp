/*
  ==============================================================================

    StepResponseDisplay.cpp
    See StepResponseDisplay.h.

  ==============================================================================
*/

#include "StepResponseDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>
#include <limits>

StepResponseDisplay::StepResponseDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    addAndMakeVisible (windowSlider);
    windowSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    windowSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, sx (60), sx (18));
    windowSlider.setTextValueSuffix (" ms");

    addAndMakeVisible (windowLabel);
    windowLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    windowLabel.setJustificationType (juce::Justification::centredRight);

    windowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "stepWindowMs", windowSlider);

    startTimerHz (10);   // poll StepResponse state + nudge metric extraction
}

StepResponseDisplay::~StepResponseDisplay()
{
    stopTimer();
}

void StepResponseDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void StepResponseDisplay::timerCallback()
{
    processor.stepResponse.tryProcessCapture();
    repaint();
}

juce::String StepResponseDisplay::statusText() const
{
    const auto& s = processor.stepResponse;
    switch (s.getState())
    {
        case StepResponse::State::Idle:           return "Idle - click Capture to arm";
        case StepResponse::State::Armed:          return "Waiting for step edge";
        case StepResponse::State::Capturing:
        {
            const int progL = s.getCaptureProgress (StepResponse::Channel::L);
            const int progR = s.getCaptureProgress (StepResponse::Channel::R);
            const int len   = s.getCaptureLength   (StepResponse::Channel::L);
            const int prog  = juce::jmin (progL, progR);
            return "Capturing " + juce::String (prog) + " / " + juce::String (len);
        }
        case StepResponse::State::ReadyToProcess: return "Processing...";
        case StepResponse::State::Ready:          return "Step response ready";
    }
    return {};
}

void StepResponseDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (60));
    header.removeFromTop (sx (28));   // status / metrics row (painted)

    header.removeFromLeft (sx (16));
    auto sliderArea = header;
    sliderArea.removeFromRight (sx (12));

    const int labelW = sx (60);
    windowLabel .setBounds (sliderArea.removeFromLeft (labelW));
    windowSlider.setBounds (sliderArea);
}

void StepResponseDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    auto header = bounds.removeFromTop (sx (60));
    auto topRow = header.removeFromTop (sx (28));

    g.setColour (WTColors::analysis);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText (statusText(),
                topRow.withTrimmedLeft (sx (16)),
                juce::Justification::centredLeft, false);

    // Metrics on the right, once a capture has been processed.
    const auto& s = processor.stepResponse;
    if (s.getState() == StepResponse::State::Ready)
    {
        using Ch = StepResponse::Channel;
        const juce::String metrics =
            "Rise  L " + juce::String (s.getRiseTimeMs (Ch::L), 2)
                + "  R " + juce::String (s.getRiseTimeMs (Ch::R), 2) + " ms"
            + "      Overshoot  L " + juce::String (s.getOvershootPct (Ch::L), 1)
                + "  R " + juce::String (s.getOvershootPct (Ch::R), 1) + " %";

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (11.0f)));
        g.drawText (metrics,
                    topRow.withTrimmedRight (sx (16)),
                    juce::Justification::centredRight, false);
    }

    drawWaveform (g, bounds.reduced (sx (24), sx (8)));
}

void StepResponseDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& s  = processor.stepResponse;
    const int   nL = s.getResponseLength (StepResponse::Channel::L);
    const int   nR = s.getResponseLength (StepResponse::Channel::R);
    const int   N  = juce::jmax (nL, nR);
    const float sr = s.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Click Capture, then play a step through the device",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const auto& bufL = s.getResponse (StepResponse::Channel::L);
    const auto& bufR = s.getResponse (StepResponse::Channel::R);
    const int   P    = s.getPreRollSamples();

    const bool showL    = *processor.apvts.getRawParameterValue ("showChannelL")    > 0.5f;
    const bool showR    = *processor.apvts.getRawParameterValue ("showChannelR")    > 0.5f;
    const bool showDiff = *processor.apvts.getRawParameterValue ("showChannelDiff") > 0.5f;
    const bool diffMode = showDiff;

    float peak = 0.0f;
    auto scanPeak = [&] (const std::vector<float>& buf, int len)
    {
        for (int i = 0; i < len; ++i)
            peak = std::max (peak, std::abs (buf[(size_t) i]));
    };
    if (diffMode)
    {
        const int diffLen = juce::jmin (nL, nR);
        for (int i = 0; i < diffLen; ++i)
            peak = std::max (peak, std::abs (bufR[(size_t) i] - bufL[(size_t) i]));
    }
    else
    {
        if (showL) scanPeak (bufL, nL);
        if (showR) scanPeak (bufR, nR);
    }
    if (peak < 1.0e-6f) peak = 1.0e-6f;
    const float yRange = peak * 1.1f;

    auto sampleToY = [&] (float v) -> float
    {
        const float tt = 0.5f - 0.5f * (juce::jlimit (-yRange, yRange, v) / yRange);
        return (float) plotArea.getY() + tt * (float) plotArea.getHeight();
    };
    auto sampleToX = [&] (int i) -> float
    {
        const float tNorm = (float) i / (float) std::max (1, N - 1);
        return (float) plotArea.getX() + tNorm * (float) plotArea.getWidth();
    };

    // Zero amplitude line.
    g.setColour (juce::Colour (0xff2a2d32));
    {
        const float yZero = sampleToY (0.0f);
        g.drawLine ((float) plotArea.getX(), yZero, (float) plotArea.getRight(), yZero, sf (1.0f));
    }

    // t = 0 marker - the step edge sits at sample P.
    {
        const float xStep = sampleToX (P);
        g.setColour (juce::Colour (0xff4a4d52));
        g.drawLine (xStep, (float) plotArea.getY(), xStep, (float) plotArea.getBottom(), sf (1.0f));
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

    // X labels: time relative to the step edge, so the pre-roll reads negative.
    auto drawXLabel = [&] (float tNorm)
    {
        const int   sample = (int) (tNorm * (float) (N - 1));
        const float ms     = (float) (sample - P) / sr * 1000.0f;
        const int   x      = plotArea.getX() + juce::roundToInt (tNorm * (float) plotArea.getWidth());
        const int   textW  = sx (48);
        juce::Rectangle<int> r (x - textW / 2,
                                labelGutterBottom.getY(),
                                textW,
                                labelGutterBottom.getHeight());
        g.drawText (juce::String (ms, 1) + " ms", r, juce::Justification::centredTop, false);
    };
    drawXLabel (0.00f);
    drawXLabel (0.25f);
    drawXLabel (0.50f);
    drawXLabel (0.75f);
    drawXLabel (1.00f);

    const int plotW = plotArea.getWidth();

    auto drawTrace = [&] (auto sampler, int sampleCount, juce::Colour colour)
    {
        if (sampleCount <= 0) return;
        const float samplesPerPixel = (float) sampleCount / (float) std::max (1, plotW);

        g.setColour (colour);
        if (samplesPerPixel <= 1.0f)
        {
            juce::Path path;
            for (int i = 0; i < sampleCount; ++i)
            {
                const float x = sampleToX (i);
                const float y = sampleToY (sampler (i));
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
                const int i1 = std::min (sampleCount, (int) ((float) (px + 1) * samplesPerPixel));
                if (i1 <= i0) continue;
                float minV =  std::numeric_limits<float>::infinity();
                float maxV = -std::numeric_limits<float>::infinity();
                for (int i = i0; i < i1; ++i)
                {
                    const float v = sampler (i);
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }
                const float x  = (float) (plotArea.getX() + px);
                const float y0 = sampleToY (maxV);
                const float y1 = sampleToY (minV);
                g.drawLine (x, y0, x, std::max (y1, y0 + 1.0f), sf (1.0f));
            }
        }
    };

    if (diffMode)
    {
        const int diffLen = juce::jmin (nL, nR);
        const int zeroY   = juce::roundToInt (sampleToY (0.0f));

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plotArea.getX(), plotArea.getY(),
                                plotArea.getWidth(),
                                juce::jmax (0, zeroY - plotArea.getY()));
            drawTrace ([&] (int i) { return bufR[(size_t) i] - bufL[(size_t) i]; },
                       diffLen, WTColors::analysis_R);
        }
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plotArea.getX(), zeroY,
                                plotArea.getWidth(),
                                juce::jmax (0, plotArea.getBottom() - zeroY));
            drawTrace ([&] (int i) { return bufR[(size_t) i] - bufL[(size_t) i]; },
                       diffLen, WTColors::analysis);
        }
    }
    else
    {
        if (showR)
            drawTrace ([&] (int i) { return bufR[(size_t) i]; }, nR, WTColors::analysis_R);
        if (showL)
            drawTrace ([&] (int i) { return bufL[(size_t) i]; }, nL, WTColors::analysis);
    }
}
