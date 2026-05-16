/*
  ==============================================================================

    SweepCurveDisplay.cpp

  ==============================================================================
*/

#include "SweepCurveDisplay.h"
#include "Colors.h"
#include "Analyses/SweepCurve.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Rounds a positive value up to the next "nice" axis maximum from the
    // 1 / 2 / 5 sequence, so the Y grid lands on readable numbers.
    float niceCeil (float v)
    {
        if (v <= 0.0f) return 1.0f;
        const float e    = std::floor (std::log10 (v));
        const float base = std::pow (10.0f, e);
        const float frac = v / base;
        float niceFrac;
        if      (frac <= 1.0f) niceFrac = 1.0f;
        else if (frac <= 2.0f) niceFrac = 2.0f;
        else if (frac <= 5.0f) niceFrac = 5.0f;
        else                   niceFrac = 10.0f;
        return niceFrac * base;
    }

    // Decimal places needed for a Y axis whose tick step is `step`, so
    // sub-unit ranges (THD often sits well below 0.01%) still render
    // distinct labels instead of a column of "0.00".
    int tickDecimals (float step)
    {
        if (step <= 0.0f) return 2;
        const int d = (int) std::ceil (-std::log10 (step)) + 1;
        return juce::jlimit (0, 6, d);
    }

    juce::String formatTick (float v, int decimals)
    {
        if (v <= 0.0f) return "0";
        return juce::String (v, decimals);
    }
}

SweepCurveDisplay::SweepCurveDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (6, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter ("sweepMetric"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureButton (thdButton, 0);
    configureButton (imdButton, 1);

    processor.apvts.addParameterListener ("sweepMetric", this);
    syncMetricButtons();

    startTimerHz (30);
}

SweepCurveDisplay::~SweepCurveDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("sweepMetric", this);
}

void SweepCurveDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void SweepCurveDisplay::timerCallback()
{
    repaint();
}

void SweepCurveDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "sweepMetric")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncMetricButtons();
            repaint();
        });
    }
}

void SweepCurveDisplay::syncMetricButtons()
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("sweepMetric");
    thdButton.setToggleState (idx == 0, juce::dontSendNotification);
    imdButton.setToggleState (idx == 1, juce::dontSendNotification);
}

SweepCurveDisplay::Metric SweepCurveDisplay::currentMetric() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("sweepMetric") == 1)
               ? Metric::IMD : Metric::THD;
}

void SweepCurveDisplay::liveValues (float& outL, float& outR) const
{
    outL = SweepCurve::kNoData;
    outR = SweepCurve::kNoData;

    if (currentMetric() == Metric::THD)
    {
        const auto& m = processor.thdMeasurement;
        if (m.isValid (THDMeasurement::Channel::L))
            outL = m.getTotalThdPercent (THDMeasurement::Channel::L);
        if (m.isValid (THDMeasurement::Channel::R))
            outR = m.getTotalThdPercent (THDMeasurement::Channel::R);
    }
    else
    {
        const auto& m = processor.imdMeasurement;
        if (m.isValid (IMDMeasurement::Channel::L))
            outL = m.getTotalImdPercent (IMDMeasurement::Channel::L);
        if (m.isValid (IMDMeasurement::Channel::R))
            outR = m.getTotalImdPercent (IMDMeasurement::Channel::R);
    }
}

void SweepCurveDisplay::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (sx (36));

    const int buttonW = sx (74);
    const int buttonH = sx (22);
    const int spacing = sx (6);
    const int totalW  = buttonW * 2 + spacing;
    const int startX  = header.getCentreX() - totalW / 2;
    const int buttonY = header.getCentreY() - buttonH / 2;

    thdButton.setBounds (startX,                       buttonY, buttonW, buttonH);
    imdButton.setBounds (startX + buttonW + spacing,   buttonY, buttonW, buttonH);
}

void SweepCurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    bounds.removeFromTop (sx (36));   // header band - owned by the metric buttons
    auto area = bounds.reduced (sx (8), sx (8));

    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft   (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const int   numBuckets = processor.sweepCurve.getNumBuckets();
    const float lastBucket = (float) juce::jmax (1, numBuckets - 1);

    // ---- Y auto-range --------------------------------------------------
    // Scan every captured value plus the live readings for the maximum.
    float dataMax = 0.0f;
    for (int b = 0; b < numBuckets; ++b)
    {
        const float l = processor.sweepCurve.getValueL (b);
        const float r = processor.sweepCurve.getValueR (b);
        if (l != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, l);
        if (r != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, r);
    }

    float liveL = SweepCurve::kNoData, liveR = SweepCurve::kNoData;
    liveValues (liveL, liveR);
    if (liveL != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, liveL);
    if (liveR != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, liveR);

    const float yMax = niceCeil (dataMax);

    auto posToX = [&] (float pos) -> float
    {
        return (float) plotArea.getX()
             + juce::jlimit (0.0f, 1.0f, pos) * (float) plotArea.getWidth();
    };
    auto valToY = [&] (float v) -> float
    {
        const float t = juce::jlimit (0.0f, 1.0f, v / yMax);
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    // ---- Grid + axis labels --------------------------------------------
    g.setColour (juce::Colour (0xff2a2d32));
    for (int i = 0; i <= 4; ++i)
    {
        const float y = valToY (yMax * (float) i / 4.0f);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));

        const float x = posToX ((float) i / 4.0f);
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    const int yDecimals = tickDecimals (yMax / 4.0f);
    for (int i = 0; i <= 4; ++i)
    {
        const float yVal = yMax * (float) i / 4.0f;
        const int   y    = juce::roundToInt (valToY (yVal));
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (formatTick (yVal, yDecimals), r,
                    juce::Justification::centredRight, false);

        const int x = juce::roundToInt (posToX ((float) i / 4.0f));
        juce::Rectangle<int> xr (x - sx (16), labelGutterBottom.getY(),
                                 sx (32), labelGutterBottom.getHeight());
        g.drawText (formatTick ((float) i / 4.0f, 2), xr,
                    juce::Justification::centredTop, false);
    }

    // "%" unit marker at the top of the Y gutter.
    g.drawText ("%", juce::Rectangle<int> (labelGutterLeft.getX(), plotArea.getY() - sx (2),
                                           labelGutterLeft.getWidth() - sx (4), sx (12)),
                juce::Justification::centredRight, false);

    // ---- Captured curves -----------------------------------------------
    auto strokeCurve = [&] (bool rightChannel, juce::Colour colour)
    {
        juce::Path path;
        bool open = false;
        for (int b = 0; b < numBuckets; ++b)
        {
            const float v = rightChannel ? processor.sweepCurve.getValueR (b)
                                         : processor.sweepCurve.getValueL (b);
            if (v == SweepCurve::kNoData) { open = false; continue; }

            const float x = posToX ((float) b / lastBucket);
            const float y = valToY (v);
            if (! open) { path.startNewSubPath (x, y); open = true; }
            else        { path.lineTo          (x, y); }
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
    };

    strokeCurve (true,  WTColors::analysis_R);   // R drawn first
    strokeCurve (false, WTColors::analysis);     // L on top

    // ---- Current sweep position + live dots ----------------------------
    const float pos = juce::jlimit (0.0f, 1.0f,
        (float) *processor.apvts.getRawParameterValue ("sweepPosition"));
    const float posX = posToX (pos);

    g.setColour (juce::Colours::whitesmoke.withAlpha (0.35f));
    g.drawLine (posX, (float) plotArea.getY(), posX, (float) plotArea.getBottom(), sf (1.0f));

    auto liveDot = [&] (float v, juce::Colour colour)
    {
        if (v == SweepCurve::kNoData) return;
        const float y = valToY (v);
        const float d = sf (4.0f);
        g.setColour (colour);
        g.fillEllipse (posX - d * 0.5f, y - d * 0.5f, d, d);
    };

    liveDot (liveR, WTColors::analysis_R);
    liveDot (liveL, WTColors::analysis);
}
