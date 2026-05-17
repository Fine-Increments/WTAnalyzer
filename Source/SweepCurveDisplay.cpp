/*
  ==============================================================================

    SweepCurveDisplay.cpp

  ==============================================================================
*/

#include "SweepCurveDisplay.h"
#include "Colors.h"
#include "Analyses/SweepCurve.h"
#include "Analyses/SweepGrid.h"

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

    // Heatmap decay-level ramp: near-black -> analysis green -> near-white.
    juce::Colour heatColour (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const juce::Colour lo  (0xff0c0c12);
        const juce::Colour mid = WTColors::analysis;
        const juce::Colour hi  (0xfff2f2f0);
        return t < 0.5f ? lo .interpolatedWith (mid, t * 2.0f)
                        : mid.interpolatedWith (hi, (t - 0.5f) * 2.0f);
    }

    // Differential-dB span shown below the heatmap's brightest cell.
    constexpr float kHeatSpanDb = 80.0f;
}

SweepCurveDisplay::SweepCurveDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureButton = [this] (juce::TextButton& b, const juce::String& paramID,
                                   int radioGroup, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (radioGroup, juce::dontSendNotification);
        b.onClick = [this, paramID, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter (paramID))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureButton (thdButton,     "sweepMetric", 6, 0);
    configureButton (imdButton,     "sweepMetric", 6, 1);
    configureButton (lineButton,    "sweepView",   9, 0);
    configureButton (heatmapButton, "sweepView",   9, 1);

    processor.apvts.addParameterListener ("sweepMetric", this);
    processor.apvts.addParameterListener ("sweepView",   this);
    syncButtons();

    startTimerHz (30);
}

SweepCurveDisplay::~SweepCurveDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("sweepMetric", this);
    processor.apvts.removeParameterListener ("sweepView",   this);
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

void SweepCurveDisplay::mouseMove (const juce::MouseEvent& e)
{
    cursorPos    = e.getPosition();
    cursorInside = true;
    repaint();
}

void SweepCurveDisplay::mouseExit (const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}

void SweepCurveDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "sweepMetric" || parameterID == "sweepView")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncButtons();
            repaint();
        });
    }
}

void SweepCurveDisplay::syncButtons()
{
    const int metric = (int) *processor.apvts.getRawParameterValue ("sweepMetric");
    thdButton.setToggleState (metric == 0, juce::dontSendNotification);
    imdButton.setToggleState (metric == 1, juce::dontSendNotification);

    const int view = (int) *processor.apvts.getRawParameterValue ("sweepView");
    lineButton   .setToggleState (view == 0, juce::dontSendNotification);
    heatmapButton.setToggleState (view == 1, juce::dontSendNotification);
}

SweepCurveDisplay::Metric SweepCurveDisplay::currentMetric() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("sweepMetric") == 1)
               ? Metric::IMD : Metric::THD;
}

SweepCurveDisplay::View SweepCurveDisplay::currentView() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("sweepView") == 1)
               ? View::Heatmap : View::Line;
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

    const int buttonW  = sx (58);
    const int buttonH  = sx (22);
    const int gap      = sx (4);
    const int groupGap = sx (14);
    const int totalW   = buttonW * 4 + gap * 2 + groupGap;
    int       x        = header.getCentreX() - totalW / 2;
    const int y        = header.getCentreY() - buttonH / 2;

    thdButton    .setBounds (x, y, buttonW, buttonH); x += buttonW + gap;
    imdButton    .setBounds (x, y, buttonW, buttonH); x += buttonW + groupGap;
    lineButton   .setBounds (x, y, buttonW, buttonH); x += buttonW + gap;
    heatmapButton.setBounds (x, y, buttonW, buttonH);
}

void SweepCurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    bounds.removeFromTop (sx (36));   // header band - owned by the buttons
    auto readoutRow = bounds.removeFromBottom (sx (18));
    auto area       = bounds.reduced (sx (8), sx (8));

    hoverText.clear();

    if (currentView() == View::Heatmap) drawHeatmap (g, area);
    else                                drawLine    (g, area);

    // Cursor readout strip below the plot (the active view sets it).
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));
    g.drawText (hoverText, readoutRow.withTrimmedLeft (sx (8)),
                juce::Justification::centredLeft, false);
}

void SweepCurveDisplay::drawLine (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft   (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const int   numBuckets = processor.sweepCurve.getNumBuckets();
    const float lastBucket = (float) juce::jmax (1, numBuckets - 1);

    // ---- Y auto-range --------------------------------------------------
    float dataMax = 0.0f;
    for (int b = 0; b < numBuckets; ++b)
    {
        const float l = processor.sweepCurve.getValueL (b);
        const float r = processor.sweepCurve.getValueR (b);
        if (l != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, l);
        if (r != SweepCurve::kNoData) dataMax = juce::jmax (dataMax, r);
    }

    // The live reading is NOT folded into the auto-range. A transient
    // spike - e.g. THD blowing up as the signal decays when transport
    // stops - must not rescale (and visually bounce) the captured curve.
    // An off-scale live dot simply clamps to the plot edge.
    float liveL = SweepCurve::kNoData, liveR = SweepCurve::kNoData;
    liveValues (liveL, liveR);

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
        // The topmost tick slot carries the "%" unit caption instead of
        // the number, so the caption never overlaps the top value.
        g.drawText (i == 4 ? juce::String ("%") : formatTick (yVal, yDecimals),
                    r, juce::Justification::centredRight, false);

        const float frac = (float) i / 4.0f;
        const int   x    = juce::roundToInt (posToX (frac));
        // Clamp the end labels inside the plot: leftmost left-justified,
        // rightmost right-justified, so neither is clipped by the edge.
        juce::Rectangle<int> xr (x - sx (16), labelGutterBottom.getY(),
                                 sx (32), labelGutterBottom.getHeight());
        juce::Justification just = juce::Justification::centredTop;
        if (i == 0)
        {
            xr.setX (x);
            just = juce::Justification::topLeft;
        }
        else if (i == 4)
        {
            xr.setX (x - sx (32));
            just = juce::Justification::topRight;
        }
        g.drawText (formatTick (frac, 2), xr, just, false);
    }

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

    // ---- Cursor readout ------------------------------------------------
    if (cursorInside && plotArea.contains (cursorPos))
    {
        const float tx = (float) (cursorPos.x - plotArea.getX())
                            / (float) plotArea.getWidth();
        const float ty = (float) (cursorPos.y - plotArea.getY())
                            / (float) plotArea.getHeight();
        const float pos = juce::jlimit (0.0f, 1.0f, tx);
        const float val = yMax * (1.0f - juce::jlimit (0.0f, 1.0f, ty));
        hoverText = "pos " + juce::String (pos, 2) + "    "
                  + juce::String (val, 3) + "%";
    }
}

void SweepCurveDisplay::drawHeatmap (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft   (sx (44));   // sweep position
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));   // harmonic / product
    if (plotArea.getWidth() <= 0 || plotArea.getHeight() <= 0) return;

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const bool isThd   = (currentMetric() == Metric::THD);
    const int  numCols = isThd ? 15 : processor.imdMeasurement.getNumProducts();
    const int  numBkts = processor.sweepGrid.getNumBuckets();
    if (numCols <= 0) return;

    // Empty-state hint.
    if (! processor.sweepGrid.hasAnyData())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Arm Capture and run the sweep automation",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    // ---- Auto-range: brightest captured cell is the top of the ramp ----
    float dMax = -1.0e8f;
    for (int b = 0; b < numBkts; ++b)
        for (int c = 0; c < numCols; ++c)
        {
            const float v = processor.sweepGrid.getValueL (b, c);
            if (v > -1.0e8f) dMax = juce::jmax (dMax, v);
        }
    if (dMax < -1.0e7f) dMax = 0.0f;
    const float floorDb = dMax - kHeatSpanDb;

    // ---- Cells ---------------------------------------------------------
    const float pW = (float) plotArea.getWidth();
    const float pH = (float) plotArea.getHeight();
    for (int c = 0; c < numCols; ++c)
    {
        const float cx0 = (float) plotArea.getX() + (float) c       / (float) numCols * pW;
        const float cx1 = (float) plotArea.getX() + (float) (c + 1) / (float) numCols * pW;
        for (int b = 0; b < numBkts; ++b)
        {
            const float v = processor.sweepGrid.getValueL (b, c);
            const float t = (v > -1.0e8f) ? (v - floorDb) / kHeatSpanDb : 0.0f;
            // Bucket 0 at the bottom, bucket N-1 at the top.
            const float cy1 = (float) plotArea.getBottom() - (float) b       / (float) numBkts * pH;
            const float cy0 = (float) plotArea.getBottom() - (float) (b + 1) / (float) numBkts * pH;
            g.setColour (heatColour (t));
            g.fillRect (juce::Rectangle<float> (cx0, cy0, cx1 - cx0, cy1 - cy0));
        }
    }

    // ---- Current sweep position marker ---------------------------------
    const float pos  = juce::jlimit (0.0f, 1.0f,
        (float) *processor.apvts.getRawParameterValue ("sweepPosition"));
    const float posY = (float) plotArea.getBottom() - pos * pH;
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.35f));
    g.drawLine ((float) plotArea.getX(), posY, (float) plotArea.getRight(), posY, sf (1.0f));

    // ---- Axis labels ---------------------------------------------------
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    for (int i = 0; i <= 4; ++i)   // sweep position down the left gutter
    {
        const float frac = (float) i / 4.0f;
        const int   y    = (int) ((float) plotArea.getBottom() - frac * pH);
        g.drawText (juce::String (frac, 2),
                    juce::Rectangle<int> (labelGutterLeft.getX(), y - sx (6),
                                          labelGutterLeft.getWidth() - sx (4), sx (12)),
                    juce::Justification::centredRight, false);
    }

    for (int c = 0; c < numCols; ++c)   // harmonic / product along the bottom
    {
        const int   cxc = (int) ((float) plotArea.getX()
                                 + ((float) c + 0.5f) / (float) numCols * pW);
        const juce::String txt = isThd ? juce::String (c + 2)
                                       : juce::String (processor.imdMeasurement.getProductLabel (c));
        g.drawText (txt,
                    juce::Rectangle<int> (cxc - sx (22), labelGutterBottom.getY(),
                                          sx (44), labelGutterBottom.getHeight()),
                    juce::Justification::centredTop, false);
    }

    // ---- Cursor readout ------------------------------------------------
    if (cursorInside && plotArea.contains (cursorPos))
    {
        const float tx = (float) (cursorPos.x - plotArea.getX()) / pW;
        const float ty = (float) (plotArea.getBottom() - cursorPos.y) / pH;
        const int   col = juce::jlimit (0, numCols - 1,
                                        (int) (juce::jlimit (0.0f, 0.999f, tx)
                                               * (float) numCols));
        const float pos = juce::jlimit (0.0f, 1.0f, ty);
        const juce::String colLabel = isThd
            ? "harmonic " + juce::String (col + 2)
            : "product "  + juce::String (processor.imdMeasurement.getProductLabel (col));
        hoverText = colLabel + "    pos " + juce::String (pos, 2);
    }
}
