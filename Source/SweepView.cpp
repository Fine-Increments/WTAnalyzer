/*
  ==============================================================================

    SweepView.cpp
    See SweepView.h.

  ==============================================================================
*/

#include "SweepView.h"
#include "Colors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    constexpr float kViewMinHz = 20.0f;

    // Monotonic level ramp: near-black -> analysis green -> near-white.
    juce::Colour rampColour (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const juce::Colour lo  (0xff0c0c12);
        const juce::Colour mid = WTColors::analysis;
        const juce::Colour hi  (0xfff2f2f0);
        return t < 0.5f ? lo .interpolatedWith (mid, t * 2.0f)
                        : mid.interpolatedWith (hi, (t - 0.5f) * 2.0f);
    }
}

SweepView::SweepView (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureButton = [this] (juce::TextButton& b, int idx)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (11, juce::dontSendNotification);
        b.onClick = [this, idx]
        {
            if (auto* p = processor.apvts.getParameter ("sweepView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
        };
    };
    configureButton (lineButton,    0);
    configureButton (heatmapButton, 1);
    configureButton (surfaceButton, 2);
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

SweepView::~SweepView()
{
    stopTimer();
}

void SweepView::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void SweepView::setValueRange (float lo, float hi, const juce::String& unit)
{
    valueLo   = lo;
    valueHi   = hi;
    valueUnit = unit;
    repaint();
}

void SweepView::timerCallback()
{
    syncButtons();
    syncCameraButtons();
    repaint();
}

void SweepView::syncCameraButtons()
{
    const bool surface = (currentStyle() == Style::Surface);
    for (auto* b : { &zoomInButton, &zoomOutButton, &panUpButton,
                     &panDownButton, &panLeftButton, &panRightButton })
        b->setVisible (surface);
}

void SweepView::resetCamera() noexcept
{
    camAzimuth   = -0.6f;
    camElevation =  0.42f;
    camDolly     =  1.0f;
    camPanX      =  0.0f;
    camPanY      =  0.0f;
}

void SweepView::applyDolly (float factor) noexcept
{
    camDolly = juce::jlimit (0.3f, 4.0f, camDolly * factor);
    repaint();
}

void SweepView::applyPan (float dx, float dy) noexcept
{
    camPanX = juce::jlimit (-0.6f, 0.6f, camPanX + dx);
    camPanY = juce::jlimit (-0.6f, 0.6f, camPanY + dy);
    repaint();
}

void SweepView::syncButtons()
{
    const int v = (int) *processor.apvts.getRawParameterValue ("sweepView");
    lineButton   .setToggleState (v == 0, juce::dontSendNotification);
    heatmapButton.setToggleState (v == 1, juce::dontSendNotification);
    surfaceButton.setToggleState (v == 2, juce::dontSendNotification);
}

SweepView::Style SweepView::currentStyle() const noexcept
{
    switch ((int) *processor.apvts.getRawParameterValue ("sweepView"))
    {
        case 1:  return Style::Heatmap;
        case 2:  return Style::Surface;
        default: return Style::Line;
    }
}

float SweepView::valueToNorm (float v) const noexcept
{
    const float span = valueHi - valueLo;
    if (span <= 0.0f)
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (v - valueLo) / span);
}

//==============================================================================
void SweepView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111213));
    auto bounds = getLocalBounds();
    bounds.removeFromTop (sx (28));   // header band - owned by the view buttons

    if (! processor.sweepCapture.hasAnyData())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Arm Capture and automate the swept parameter",
                    bounds, juce::Justification::centred, false);
        return;
    }

    if (currentStyle() == Style::Surface)
    {
        drawSurface (g, bounds.reduced (sx (8)));
        return;
    }

    // Line / Heatmap share a plot rect with a value gutter (left) and a
    // frequency gutter (bottom).
    auto area         = bounds.reduced (sx (8));
    auto leftGutter   = area.removeFromLeft   (sx (44));
    auto bottomGutter = area.removeFromBottom (sx (16));
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    if (currentStyle() == Style::Heatmap) drawHeatmap     (g, area);
    else                                  drawLineOverlay (g, area);

    // ---- Axis labels ---------------------------------------------------
    const float sr   = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float hiHz = juce::jmax (40.0f, sr * 0.5f);
    const float logMin   = std::log10 (kViewMinHz);
    const float logRange = std::log10 (hiHz) - logMin;

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    for (float hz : { 20.0f, 100.0f, 1000.0f, 10000.0f })
    {
        if (hz > hiHz) continue;
        const float t = (std::log10 (hz) - logMin) / logRange;
        const int   x = area.getX() + juce::roundToInt (t * (float) area.getWidth());
        g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k"
                                  : juce::String ((int) hz),
                    juce::Rectangle<int> (x - sx (20), bottomGutter.getY(),
                                          sx (40), bottomGutter.getHeight()),
                    juce::Justification::centredTop, false);
    }

    auto valueLabel = [&] (float frac, float v)
    {
        const int y = area.getBottom() - juce::roundToInt (frac * (float) area.getHeight());
        g.drawText (juce::String (v, 1),
                    juce::Rectangle<int> (leftGutter.getX(), y - sx (6),
                                          leftGutter.getWidth() - sx (4), sx (12)),
                    juce::Justification::centredRight, false);
    };
    valueLabel (0.0f, valueLo);
    valueLabel (0.5f, (valueLo + valueHi) * 0.5f);
    valueLabel (1.0f, valueHi);
}

void SweepView::resized()
{
    auto header = getLocalBounds().removeFromTop (sx (28));
    const int bw = sx (58), bh = sx (20), gap = sx (4);
    const int totalW = bw * 3 + gap * 2;
    int       x = header.getCentreX() - totalW / 2;
    const int y = header.getCentreY() - bh / 2;
    lineButton   .setBounds (x, y, bw, bh); x += bw + gap;
    heatmapButton.setBounds (x, y, bw, bh); x += bw + gap;
    surfaceButton.setBounds (x, y, bw, bh);

    // Surface camera cluster, upper-right of the plot: two stacked zoom
    // buttons, then a 4-way pan cross below them.
    const int cbw = sx (20), cg = sx (3), cm = sx (8);
    const int col2 = getWidth() - cm - cbw;
    const int col1 = col2 - cbw - cg;
    const int col0 = col1 - cbw - cg;
    const int top  = sx (28) + cm;

    zoomInButton  .setBounds (col1, top,                    cbw, cbw);
    zoomOutButton .setBounds (col1, top + cbw + cg,         cbw, cbw);

    const int crossTop = top + 2 * (cbw + cg) + sx (8);
    panUpButton   .setBounds (col1, crossTop,                    cbw, cbw);
    panLeftButton .setBounds (col0, crossTop + cbw + cg,         cbw, cbw);
    panRightButton.setBounds (col2, crossTop + cbw + cg,         cbw, cbw);
    panDownButton .setBounds (col1, crossTop + 2 * (cbw + cg),   cbw, cbw);
}

//==============================================================================
void SweepView::drawHeatmap (juce::Graphics& g, juce::Rectangle<int> plotArea)
{
    const int W = plotArea.getWidth();
    const int H = plotArea.getHeight();
    if (W <= 0 || H <= 0) return;

    const float sr    = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float binHz = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
    const int numBuckets = processor.sweepCapture.getNumBuckets();
    const int numBins    = processor.sweepCapture.getNumBins();
    if (numBins <= 0 || binHz <= 0.0f) return;

    const float loHz     = kViewMinHz;
    const float hiHz     = juce::jmax (40.0f, sr * 0.5f);
    const float logMin   = std::log10 (loHz);
    const float logRange = std::log10 (hiHz) - logMin;

    juce::Image image (juce::Image::RGB, W, H, false);
    juce::Image::BitmapData bmp (image, juce::Image::BitmapData::writeOnly);

    std::vector<int> colBin ((size_t) W, 0);
    for (int px = 0; px < W; ++px)
    {
        const float xN = (float) px / (float) std::max (1, W - 1);
        const float hz = std::pow (10.0f, logMin + xN * logRange);
        colBin[(size_t) px] = juce::jlimit (0, numBins - 1, (int) (hz / binHz + 0.5f));
    }

    for (int py = 0; py < H; ++py)
    {
        // py 0 = top = sweep position 1; py H-1 = bottom = position 0.
        const float yN     = 1.0f - (float) py / (float) std::max (1, H - 1);
        const int   bucket = juce::jlimit (0, numBuckets - 1,
                                           (int) (yN * (float) (numBuckets - 1) + 0.5f));
        auto* row = bmp.getLinePointer (py);
        for (int px = 0; px < W; ++px)
        {
            const float v = processor.sweepCapture.getValue (bucket, colBin[(size_t) px]);
            const juce::Colour c = (v <= SweepCapture::kNoDataDb + 1.0f)
                                     ? juce::Colour (0xff181a1d)
                                     : rampColour (valueToNorm (v));
            row[px * (int) bmp.pixelStride + 0] = c.getBlue();
            row[px * (int) bmp.pixelStride + 1] = c.getGreen();
            row[px * (int) bmp.pixelStride + 2] = c.getRed();
        }
    }

    g.drawImageAt (image, plotArea.getX(), plotArea.getY());

    // Current sweep-position marker.
    const float pos  = juce::jlimit (0.0f, 1.0f,
        (float) *processor.apvts.getRawParameterValue ("sweepPosition"));
    const int   posY = plotArea.getBottom()
                     - juce::roundToInt (pos * (float) plotArea.getHeight());
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.35f));
    g.drawLine ((float) plotArea.getX(), (float) posY,
                (float) plotArea.getRight(), (float) posY, sf (1.0f));
}

void SweepView::drawLineOverlay (juce::Graphics& g, juce::Rectangle<int> plotArea)
{
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const float sr    = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float binHz = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
    const int numBuckets = processor.sweepCapture.getNumBuckets();
    const int numBins    = processor.sweepCapture.getNumBins();
    if (numBins <= 0 || binHz <= 0.0f) return;

    const float loHz     = kViewMinHz;
    const float hiHz     = juce::jmax (40.0f, sr * 0.5f);
    const float logMin   = std::log10 (loHz);
    const float logRange = std::log10 (hiHz) - logMin;

    auto hzToX = [&] (float hz) -> float
    {
        const float t = juce::jlimit (0.0f, 1.0f,
            (std::log10 (juce::jmax (loHz, hz)) - logMin) / logRange);
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };
    auto valToY = [&] (float v) -> float
    {
        return (float) plotArea.getBottom()
             - valueToNorm (v) * (float) plotArea.getHeight();
    };

    const float pos       = juce::jlimit (0.0f, 1.0f,
        (float) *processor.apvts.getRawParameterValue ("sweepPosition"));
    const int   curBucket = juce::roundToInt (pos * (float) (numBuckets - 1));

    // Captured per-position curves overlaid; empty buckets stroke nothing.
    // Drawn oldest-to-newest with position-graded alpha; the curve at the
    // current sweep position is highlighted on top.
    //
    // A run of one valid bin between no-data gaps - a sparse aliasing
    // spike, say - cannot stroke as a line, so it is marked with a dot;
    // continuous curves (FR / Phase) never trip that path.
    auto strokeCurve = [&] (int bucket, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool  open      = false;
        int   runLength = 0;
        float lastX = 0.0f, lastY = 0.0f;

        auto flushRun = [&]
        {
            if (runLength == 1)
            {
                const float d = sf (3.0f);
                g.setColour (colour);
                g.fillEllipse (lastX - d * 0.5f, lastY - d * 0.5f, d, d);
            }
        };

        for (int bin = 1; bin < numBins; ++bin)
        {
            const float hz = (float) bin * binHz;
            if (hz < loHz) continue;
            if (hz > hiHz) break;
            const float v = processor.sweepCapture.getValue (bucket, bin);
            if (v <= SweepCapture::kNoDataDb + 1.0f)
                { flushRun(); open = false; runLength = 0; continue; }
            const float x = hzToX (hz), y = valToY (v);
            if (! open) { path.startNewSubPath (x, y); open = true; runLength = 1; }
            else        { path.lineTo (x, y); ++runLength; }
            lastX = x; lastY = y;
        }
        flushRun();

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    for (int b = 0; b < numBuckets; ++b)
    {
        if (b == curBucket) continue;   // drawn last, on top
        const float posN = (float) b / (float) std::max (1, numBuckets - 1);
        strokeCurve (b, WTColors::analysis.withAlpha (0.12f + 0.45f * posN), sf (1.0f));
    }

    // Current-position curve, highlighted.
    strokeCurve (curBucket, juce::Colours::whitesmoke, sf (1.6f));
}

void SweepView::drawSurface (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (area);
    if (area.getWidth() <= 0 || area.getHeight() <= 0) return;

    const float sr    = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float binHz = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
    const int numBuckets = processor.sweepCapture.getNumBuckets();
    const int numBins    = processor.sweepCapture.getNumBins();
    if (numBins <= 0 || binHz <= 0.0f) return;

    const float loHz     = kViewMinHz;
    const float hiHz     = juce::jmax (40.0f, sr * 0.5f);
    const float logMin   = std::log10 (loHz);
    const float logRange = juce::jmax (1.0e-4f, std::log10 (hiHz) - logMin);

    // Model box half-extents. This is a spectrum surface, so the
    // frequency axis is the long one; value and sweep position are
    // shallower. The box is a rigid rectangular solid - the non-uniform
    // shape is baked into these MODEL extents, before the rotation, so
    // the orbit stays rigid (a non-uniform scale applied after rotation
    // is what shears a 3D view).
    constexpr float hx = 1.62f;   // frequency       (log Hz)
    constexpr float hy = 0.52f;   // value           (dB / degrees)
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

    // Fit-to-view: rotate a label-margin envelope of the box, measure
    // its screen spread, and take the largest uniform scale that still
    // fits inside the panel. Recomputed each frame so the box fills the
    // space at every orbit angle; camDolly is the user's zoom on top.
    float maxAbsX = 1.0e-3f, maxAbsY = 1.0e-3f;
    for (int i = 0; i < 8; ++i)
    {
        const R c = rotate ((i & 1)        ? hx * 1.22f : -hx * 1.22f,
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

    // Axis-coordinate mappers into the model box.
    auto freqToMx = [&] (float f) -> float
    {
        const float t = juce::jlimit (0.0f, 1.0f,
            (std::log10 (juce::jmax (loHz, f)) - logMin) / logRange);
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
    order.reserve ((size_t) numBuckets);
    for (int b = 0; b < numBuckets; ++b)
    {
        const float tz = numBuckets > 1 ? (float) b / (float) (numBuckets - 1) : 0.0f;
        order.emplace_back (project (0.0f, 0.0f, posToMz (tz)).depth, b);
    }
    std::sort (order.begin(), order.end(),
               [] (auto& l, auto& r) { return l.first < r.first; });

    const float depthMin  = order.front().first;
    const float depthSpan = juce::jmax (1.0e-4f, order.back().first - depthMin);

    for (auto& entry : order)
    {
        const int   b  = entry.second;
        const float tz = numBuckets > 1 ? (float) b / (float) (numBuckets - 1) : 0.0f;
        const float mz = posToMz (tz);

        const float depthN     = (entry.first - depthMin) / depthSpan;
        const juce::Colour rowC = WTColors::analysis.withAlpha (0.30f + 0.6f * depthN);

        // An isolated valid bin (a run of one) is a sparse spike - e.g. an
        // aliasing product between two grid harmonics. A bare one-point
        // subpath strokes nothing, so it is drawn as a stem from the floor
        // with a dot at the tip; continuous curves connect as runs.
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

        for (int bin = 1; bin < numBins; ++bin)
        {
            const float hzv = (float) bin * binHz;
            if (hzv < loHz) continue;
            if (hzv > hiHz) break;
            const float v = processor.sweepCapture.getValue (b, bin);
            if (v <= SweepCapture::kNoDataDb + 1.0f)
                { flushRun(); open = false; runLength = 0; continue; }
            const float mx = freqToMx (hzv);
            const P pt   = project (mx, valToMy (valueToNorm (v)), mz);
            const P base = project (mx, -hy,                       mz);
            if (! open) { top.startNewSubPath (pt.x, pt.y); open = true; runLength = 1; }
            else        { top.lineTo (pt.x, pt.y); ++runLength; }
            lastPt = pt; lastBase = base;
        }
        flushRun();

        g.setColour (rowC);
        g.strokePath (top, juce::PathStrokeType (sf (1.1f)));
    }

    // ---- Axes ----------------------------------------------------------
    // Anchored to box edges so they rotate with the view: frequency along
    // the front-bottom edge, sweep position along the left-bottom edge,
    // value up the front-right vertical edge.
    auto label3D = [&] (const juce::String& txt, P at)
    {
        g.drawText (txt,
                    juce::Rectangle<float> (at.x - sf (20.0f), at.y - sf (6.0f),
                                            sf (40.0f), sf (12.0f)).toNearestInt(),
                    juce::Justification::centred, false);
    };
    g.setFont (juce::FontOptions (sf (10.0f)));

    // Frequency axis.
    {
        const P e0 = project (-hx, -hy, hz);
        const P e1 = project ( hx, -hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        for (float f : { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f })
        {
            if (f < loHz || f > hiHz) continue;
            const float mx = freqToMx (f);
            const P on  = project (mx, -hy, hz);
            const P end = project (mx, -hy, hz * 1.07f);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k"
                                  : juce::String ((int) f),
                     project (mx, -hy, hz * 1.22f));
        }
        label3D ("Hz", project (hx, -hy, hz * 1.40f));
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

    // Value axis.
    {
        const P e0 = project (hx, -hy, hz);
        const P e1 = project (hx,  hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        const int vdec = (std::abs (valueHi - valueLo) >= 20.0f) ? 0 : 1;
        for (int i = 0; i <= 4; ++i)
        {
            const float n  = (float) i / 4.0f;
            const float my = valToMy (n);
            const P on  = project (hx,         my, hz);
            const P end = project (hx * 1.05f, my, hz);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (juce::String (valueLo + n * (valueHi - valueLo), vdec),
                     project (hx * 1.17f, my, hz));
        }
        label3D (valueUnit, project (hx * 1.17f, hy * 1.30f, hz));
    }
}

//==============================================================================
void SweepView::mouseDown (const juce::MouseEvent& e)
{
    dragging = (currentStyle() == Style::Surface);
    if (dragging)
    {
        dragStart = e.position;
        dragAz0   = camAzimuth;
        dragEl0   = camElevation;
    }
}

void SweepView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float dx = e.position.x - dragStart.x;
    const float dy = e.position.y - dragStart.y;
    camAzimuth   = dragAz0 + dx * 0.01f;
    camElevation = juce::jlimit (-1.4f, 1.4f, dragEl0 + dy * 0.01f);
    repaint();
}

void SweepView::mouseDoubleClick (const juce::MouseEvent&)
{
    resetCamera();
    dragging = false;
    repaint();
}

void SweepView::mouseWheelMove (const juce::MouseEvent&,
                                const juce::MouseWheelDetails& wheel)
{
    if (currentStyle() != Style::Surface || wheel.deltaY == 0.0f)
        return;
    applyDolly (1.0f + wheel.deltaY * 0.5f);
}
