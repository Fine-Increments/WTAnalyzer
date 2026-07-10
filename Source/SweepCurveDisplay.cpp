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
#include <array>
#include <cmath>
#include <vector>

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

    configureButton (lineButton,    "sweepView", 9, 0);
    configureButton (heatmapButton, "sweepView", 9, 1);
    configureButton (surfaceButton, "sweepView", 9, 2);

    processor.apvts.addParameterListener ("sweepView", this);
    syncButtons();

    // Surface camera controls - hidden child components, shown only while
    // the 3D view is active (see syncCameraButtons()).
    for (auto* b : { &zoomInButton, &zoomOutButton, &panUpButton,
                     &panDownButton, &panLeftButton, &panRightButton })
        addChildComponent (b);

    zoomInButton  .onClick = [this] { applyDolly (1.3f); };
    zoomOutButton .onClick = [this] { applyDolly (1.0f / 1.3f); };
    panUpButton   .onClick = [this] { applyPan ( 0.0f, -0.08f); };
    panDownButton .onClick = [this] { applyPan ( 0.0f,  0.08f); };
    panLeftButton .onClick = [this] { applyPan (-0.08f, 0.0f); };
    panRightButton.onClick = [this] { applyPan ( 0.08f, 0.0f); };
    syncCameraButtons();

    startTimerHz (30);
}

SweepCurveDisplay::~SweepCurveDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("sweepView", this);
}

void SweepCurveDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void SweepCurveDisplay::setMetric (bool isImd) noexcept
{
    metricIsImd = isImd;
    repaint();
}

void SweepCurveDisplay::timerCallback()
{
    syncCameraButtons();
    repaint();
}

void SweepCurveDisplay::syncCameraButtons()
{
    const bool surface = (currentView() == View::Surface);
    for (auto* b : { &zoomInButton, &zoomOutButton, &panUpButton,
                     &panDownButton, &panLeftButton, &panRightButton })
        b->setVisible (surface);
}

void SweepCurveDisplay::resetCamera() noexcept
{
    camAzimuth   = -0.6f;
    camElevation =  0.42f;
    camDolly     =  1.0f;
    camPanX      =  0.0f;
    camPanY      =  0.0f;
}

void SweepCurveDisplay::applyDolly (float factor) noexcept
{
    camDolly = juce::jlimit (0.3f, 4.0f, camDolly * factor);
    repaint();
}

void SweepCurveDisplay::applyPan (float dx, float dy) noexcept
{
    camPanX = juce::jlimit (-0.6f, 0.6f, camPanX + dx);
    camPanY = juce::jlimit (-0.6f, 0.6f, camPanY + dy);
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

void SweepCurveDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = (currentView() == View::Surface);
    if (dragging)
    {
        dragStart = e.position;
        dragAz0   = camAzimuth;
        dragEl0   = camElevation;
    }
}

void SweepCurveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    camAzimuth   = dragAz0 + (e.position.x - dragStart.x) * 0.01f;
    camElevation = juce::jlimit (-1.4f, 1.4f,
                                 dragEl0 + (e.position.y - dragStart.y) * 0.01f);
    repaint();
}

void SweepCurveDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    resetCamera();
    dragging = false;
    repaint();
}

void SweepCurveDisplay::mouseWheelMove (const juce::MouseEvent&,
                                        const juce::MouseWheelDetails& wheel)
{
    if (currentView() != View::Surface || wheel.deltaY == 0.0f)
        return;
    applyDolly (1.0f + wheel.deltaY * 0.5f);
}

void SweepCurveDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "sweepView")
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
    const int view = (int) *processor.apvts.getRawParameterValue ("sweepView");
    lineButton   .setToggleState (view == 0, juce::dontSendNotification);
    heatmapButton.setToggleState (view == 1, juce::dontSendNotification);
    surfaceButton.setToggleState (view == 2, juce::dontSendNotification);
}

SweepCurveDisplay::Metric SweepCurveDisplay::currentMetric() const noexcept
{
    return metricIsImd ? Metric::IMD : Metric::THD;
}

SweepCurveDisplay::View SweepCurveDisplay::currentView() const noexcept
{
    switch ((int) *processor.apvts.getRawParameterValue ("sweepView"))
    {
        case 1:  return View::Heatmap;
        case 2:  return View::Surface;
        default: return View::Line;
    }
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

    const int buttonW = sx (58);
    const int buttonH = sx (22);
    const int gap     = sx (4);
    const int totalW  = buttonW * 3 + gap * 2;
    int       x       = header.getCentreX() - totalW / 2;
    const int y       = header.getCentreY() - buttonH / 2;

    lineButton   .setBounds (x, y, buttonW, buttonH); x += buttonW + gap;
    heatmapButton.setBounds (x, y, buttonW, buttonH); x += buttonW + gap;
    surfaceButton.setBounds (x, y, buttonW, buttonH);

    // Surface camera cluster, upper-right of the plot: two stacked zoom
    // buttons, then a 4-way pan cross below them.
    const int cbw = sx (20), cg = sx (3), cm = sx (8);
    const int col2 = getWidth() - cm - cbw;
    const int col1 = col2 - cbw - cg;
    const int col0 = col1 - cbw - cg;
    const int top  = sx (36) + cm;

    zoomInButton  .setBounds (col1, top,                    cbw, cbw);
    zoomOutButton .setBounds (col1, top + cbw + cg,         cbw, cbw);

    const int crossTop = top + 2 * (cbw + cg) + sx (8);
    panUpButton   .setBounds (col1, crossTop,                    cbw, cbw);
    panLeftButton .setBounds (col0, crossTop + cbw + cg,         cbw, cbw);
    panRightButton.setBounds (col2, crossTop + cbw + cg,         cbw, cbw);
    panDownButton .setBounds (col1, crossTop + 2 * (cbw + cg),   cbw, cbw);
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

    if      (currentView() == View::Heatmap) drawHeatmap (g, area);
    else if (currentView() == View::Surface) drawSurface (g, area);
    else                                     drawLine    (g, area);

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

void SweepCurveDisplay::drawSurface (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (area);
    if (area.getWidth() <= 0 || area.getHeight() <= 0) return;

    const bool isThd   = (currentMetric() == Metric::THD);
    const int  numCols = isThd ? 15 : processor.imdMeasurement.getNumProducts();
    const int  numBkts = processor.sweepGrid.getNumBuckets();
    if (numCols <= 0) return;

    if (! processor.sweepGrid.hasAnyData())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Arm Capture and run the sweep automation",
                    area, juce::Justification::centred, false);
        return;
    }

    // Auto-range: the brightest captured cell anchors the top of the
    // height scale; an 80 dB span runs down to the floor.
    float dMax = -1.0e8f;
    for (int b = 0; b < numBkts; ++b)
        for (int c = 0; c < numCols; ++c)
        {
            const float v = processor.sweepGrid.getValueL (b, c);
            if (v > -1.0e8f) dMax = juce::jmax (dMax, v);
        }
    if (dMax < -1.0e7f) dMax = 0.0f;
    const float floorDb = dMax - 80.0f;

    // Model box half-extents - a rigid rectangular solid, wider on the
    // harmonic / product axis. The non-uniform shape is baked into these
    // MODEL extents, before the rotation, so the orbit stays rigid (a
    // non-uniform scale applied after rotation is what shears a 3D view).
    constexpr float hx = 1.45f;   // harmonic / product
    constexpr float hy = 0.52f;   // value (differential dB)
    constexpr float hz = 0.74f;   // sweep position

    const float cosA = std::cos (camAzimuth),   sinA = std::sin (camAzimuth);
    const float cosE = std::cos (camElevation), sinE = std::sin (camElevation);

    // Rotation only, no screen mapping. depth is the painter's-algorithm key.
    struct R { float x, y, depth; };
    auto rotate = [&] (float mx, float my, float mz) -> R
    {
        const float x1 =  mx * cosA + mz * sinA;
        const float z1 = -mx * sinA + mz * cosA;
        const float y2 =  my * cosE - z1 * sinE;
        const float z2 =  my * sinE + z1 * cosE;
        return { x1, y2, z2 };
    };

    // Fit-to-view: scale so the label-margin envelope of the box fills
    // the panel at the current orbit angle. camDolly zooms on top.
    float maxAbsX = 1.0e-3f, maxAbsY = 1.0e-3f;
    for (int i = 0; i < 8; ++i)
    {
        const R c = rotate ((i & 1)        ? hx * 1.24f : -hx * 1.24f,
                            ((i >> 1) & 1) ? hy * 1.30f : -hy * 1.30f,
                            ((i >> 2) & 1) ? hz * 1.30f : -hz * 1.30f);
        maxAbsX = juce::jmax (maxAbsX, std::abs (c.x));
        maxAbsY = juce::jmax (maxAbsY, std::abs (c.y));
    }
    const float fitW  = juce::jmax (1.0f, (float) area.getWidth()  - sf (48.0f));
    const float fitH  = juce::jmax (1.0f, (float) area.getHeight() - sf (40.0f));
    const float scale = juce::jmin (fitW / (2.0f * maxAbsX),
                                    fitH / (2.0f * maxAbsY)) * camDolly;

    const float cx = (float) area.getCentreX() + camPanX * (float) area.getWidth();
    const float cy = (float) area.getCentreY() + camPanY * (float) area.getHeight();

    struct P { float x, y, depth; };
    auto project = [&] (float mx, float my, float mz) -> P
    {
        const R r = rotate (mx, my, mz);
        return { cx + r.x * scale, cy - r.y * scale, r.depth };
    };
    auto colToMx = [&] (int c) -> float
    {
        const float t = numCols > 1 ? (float) c / (float) (numCols - 1) : 0.5f;
        return (t - 0.5f) * 2.0f * hx;
    };
    auto valToMy = [&] (float n) -> float
    {
        return (juce::jlimit (0.0f, 1.0f, n) - 0.5f) * 2.0f * hy;
    };
    auto posToMz = [&] (float t) -> float { return (0.5f - t) * 2.0f * hz; };

    // Orientation wireframe box.
    g.setColour (juce::Colour (0xff242730));
    std::array<P, 8> corner;
    for (int i = 0; i < 8; ++i)
        corner[(size_t) i] = project ((i & 1)        ? hx : -hx,
                                      ((i >> 1) & 1) ? hy : -hy,
                                      ((i >> 2) & 1) ? hz : -hz);
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
        {
            const int d = a ^ b;
            if (d != 0 && (d & (d - 1)) == 0)
                g.drawLine (corner[(size_t) a].x, corner[(size_t) a].y,
                            corner[(size_t) b].x, corner[(size_t) b].y, sf (1.0f));
        }

    // Position rows, depth-sorted back to front.
    std::vector<std::pair<float, int>> order;
    order.reserve ((size_t) numBkts);
    for (int b = 0; b < numBkts; ++b)
    {
        const float tz = numBkts > 1 ? (float) b / (float) (numBkts - 1) : 0.0f;
        order.emplace_back (project (0.0f, 0.0f, posToMz (tz)).depth, b);
    }
    std::sort (order.begin(), order.end(),
               [] (auto& l, auto& r) { return l.first < r.first; });

    const float depthMin  = order.front().first;
    const float depthSpan = juce::jmax (1.0e-4f, order.back().first - depthMin);

    for (auto& entry : order)
    {
        const int   b  = entry.second;
        const float tz = numBkts > 1 ? (float) b / (float) (numBkts - 1) : 0.0f;
        const float mz = posToMz (tz);

        const float depthN     = (entry.first - depthMin) / depthSpan;
        const juce::Colour rowC = WTColors::analysis.withAlpha (0.30f + 0.6f * depthN);

        // An isolated valid column (a run of one) cannot stroke as a line,
        // so it is drawn as a stem from the floor with a dot at the tip.
        juce::Path top;
        bool open      = false;
        int  runLength = 0;
        P    lastPt {}, lastBase {};

        auto flushRun = [&]
        {
            if (runLength == 1)
            {
                g.setColour (rowC);
                g.drawLine (lastBase.x, lastBase.y, lastPt.x, lastPt.y, sf (1.1f));
                const float d = sf (3.0f);
                g.fillEllipse (lastPt.x - d * 0.5f, lastPt.y - d * 0.5f, d, d);
            }
        };

        for (int c = 0; c < numCols; ++c)
        {
            const float v = processor.sweepGrid.getValueL (b, c);
            if (v <= -1.0e8f) { flushRun(); open = false; runLength = 0; continue; }
            const float lv = (v - floorDb) / kHeatSpanDb;
            const P pt   = project (colToMx (c), valToMy (lv), mz);
            const P base = project (colToMx (c), -hy,          mz);
            if (! open) { top.startNewSubPath (pt.x, pt.y); open = true; runLength = 1; }
            else        { top.lineTo (pt.x, pt.y); ++runLength; }
            lastPt = pt; lastBase = base;
        }
        flushRun();

        g.setColour (rowC);
        g.strokePath (top, juce::PathStrokeType (sf (1.1f)));
    }

    // ---- Axes ----------------------------------------------------------
    // Anchored to box edges so they rotate with the view: harmonic /
    // product along the front-bottom edge, sweep position along the
    // left-bottom edge, value up the front-right vertical edge.
    auto label3D = [&] (const juce::String& txt, P at)
    {
        g.drawText (txt,
                    juce::Rectangle<float> (at.x - sf (20.0f), at.y - sf (6.0f),
                                            sf (40.0f), sf (12.0f)).toNearestInt(),
                    juce::Justification::centred, false);
    };
    g.setFont (juce::FontOptions (sf (10.0f)));

    // Harmonic / product axis.
    {
        const P e0 = project (-hx, -hy, hz);
        const P e1 = project ( hx, -hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        const int step = juce::jmax (1, (numCols + 7) / 8);   // thin to <= 8 labels
        for (int c = 0; c < numCols; c += step)
        {
            const float mx = colToMx (c);
            const P on  = project (mx, -hy, hz);
            const P end = project (mx, -hy, hz * 1.07f);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (isThd ? juce::String (c + 2)
                           : juce::String (processor.imdMeasurement.getProductLabel (c)),
                     project (mx, -hy, hz * 1.22f));
        }
        label3D (isThd ? "harm" : "prod", project (hx, -hy, hz * 1.40f));
    }

    // Sweep-position axis.
    {
        const P e0 = project (-hx, -hy, -hz);
        const P e1 = project (-hx, -hy,  hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        for (int i = 0; i <= 4; ++i)
        {
            const float posv = (float) i / 4.0f;
            const float mz   = posToMz (posv);   // position 0 at the front edge
            const P on  = project (-hx,         -hy, mz);
            const P end = project (-hx * 1.05f, -hy, mz);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (juce::String (posv, 2), project (-hx * 1.16f, -hy, mz));
        }
        label3D ("pos", project (-hx * 1.16f, -hy, hz * 1.34f));
    }

    // Value axis - differential dB.
    {
        const P e0 = project (hx, -hy, hz);
        const P e1 = project (hx,  hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        for (int i = 0; i <= 4; ++i)
        {
            const float n  = (float) i / 4.0f;
            const float my = valToMy (n);
            const P on  = project (hx,         my, hz);
            const P end = project (hx * 1.05f, my, hz);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (juce::String (juce::roundToInt (floorDb + n * kHeatSpanDb)),
                     project (hx * 1.17f, my, hz));
        }
        label3D ("dB", project (hx * 1.17f, hy * 1.30f, hz));
    }
}
