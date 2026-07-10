/*
  ==============================================================================

    CSDView.cpp

  ==============================================================================
*/

#include "CSDView.h"
#include "Colors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    constexpr float kViewMinHz = 20.0f;

    constexpr float kDefaultAzimuth   = -0.6f;
    constexpr float kDefaultElevation =  0.42f;

    // Decay-level colour ramp: near-black at the floor, through the
    // analysis green, to near-white at the 0 dB peak.
    juce::Colour csdColour (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const juce::Colour lo  (0xff0c0c12);
        const juce::Colour mid = WTColors::analysis;
        const juce::Colour hi  (0xfff2f2f0);
        return t < 0.5f ? lo .interpolatedWith (mid, t * 2.0f)
                        : mid.interpolatedWith (hi, (t - 0.5f) * 2.0f);
    }

    juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    juce::String formatHz (float hz)
    {
        if (hz < 1000.0f) return juce::String (juce::roundToInt (hz));
        const float k = hz / 1000.0f;
        if (std::abs (k - std::round (k)) < 0.01f)
            return juce::String (juce::roundToInt (k)) + "k";
        return juce::String (k, 1) + "k";
    }

    // Log-correct frequency ticks across [loHz, hiHz]: decade 1/2/5
    // normally, finer decade subdivisions when zoomed, a linear nice-step
    // fallback for very narrow ranges.
    void buildFreqTicks (float loHz, float hiHz, std::vector<float>& out)
    {
        out.clear();
        if (hiHz <= loHz) return;

        const int dLo = (int) std::floor (std::log10 (loHz));
        const int dHi = (int) std::ceil  (std::log10 (hiHz));
        auto fill = [&] (std::initializer_list<float> ms)
        {
            out.clear();
            for (int d = dLo; d <= dHi; ++d)
                for (float m : ms)
                {
                    const float f = m * std::pow (10.0f, (float) d);
                    if (f >= loHz && f <= hiHz) out.push_back (f);
                }
        };
        fill ({ 1.0f, 2.0f, 5.0f });
        if (out.size() < 4)
            fill ({ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f });
        if (out.size() < 3)
        {
            out.clear();
            const float rough = juce::jmax (hiHz - loHz, 1.0f) / 6.0f;
            const float mag   = std::pow (10.0f, std::floor (std::log10 (rough)));
            const float norm  = rough / mag;
            const float step  = ((norm < 1.5f) ? 1.0f : (norm < 3.5f) ? 2.0f
                                : (norm < 7.5f) ? 5.0f : 10.0f) * mag;
            for (float f = std::ceil (loHz / step) * step;
                 f <= hiHz + step * 0.001f; f += step)
                out.push_back (f);
        }
    }
}

CSDView::CSDView (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);
    setInterceptsMouseClicks (true, true);   // allow the dolly buttons

    for (auto* b : { &zoomInButton, &zoomOutButton, &panUpButton,
                     &panDownButton, &panLeftButton, &panRightButton })
        addChildComponent (b);

    zoomInButton  .onClick = [this] { applyDolly (1.3f); };
    zoomOutButton .onClick = [this] { applyDolly (1.0f / 1.3f); };
    panUpButton   .onClick = [this] { applyPan ( 0.0f, -0.08f); };
    panDownButton .onClick = [this] { applyPan ( 0.0f,  0.08f); };
    panLeftButton .onClick = [this] { applyPan (-0.08f, 0.0f); };
    panRightButton.onClick = [this] { applyPan ( 0.08f, 0.0f); };
}

void CSDView::applyDolly (float factor)
{
    camDolly = juce::jlimit (0.25f, 6.0f, camDolly * factor);
    renderImage();
    repaint();
}

void CSDView::applyPan (float dx, float dy)
{
    camPanX = juce::jlimit (-0.6f, 0.6f, camPanX + dx);
    camPanY = juce::jlimit (-0.6f, 0.6f, camPanY + dy);
    renderImage();
    repaint();
}

void CSDView::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
}

CSDView::Style CSDView::currentStyle() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("irView") == 2)
               ? Style::Waterfall : Style::Heatmap;
}

int CSDView::currentChannel() const noexcept
{
    return (*processor.apvts.getRawParameterValue ("showChannelL") > 0.5f) ? 0 : 1;
}

juce::Rectangle<int> CSDView::heatmapPlotArea() const noexcept
{
    auto a = getLocalBounds();
    a.removeFromLeft   (sx (44));
    a.removeFromBottom (sx (16));
    return a;
}

void CSDView::heatmapFreqRange (float& loHz, float& hiHz) const noexcept
{
    const float maxHz = juce::jmax (kViewMinHz * 2.0f, csd.getMaxHz());
    if (zoomFreqHiHz > zoomFreqLoHz && zoomFreqLoHz > 0.0f)
    {
        loHz = juce::jlimit (kViewMinHz, maxHz, zoomFreqLoHz);
        hiHz = juce::jlimit (loHz + 1.0f, maxHz, zoomFreqHiHz);
    }
    else
    {
        loHz = kViewMinHz;
        hiHz = maxHz;
    }
}

void CSDView::resetZoom()
{
    zoomFreqLoHz = 0.0f;
    zoomFreqHiHz = 0.0f;
    zoomTimeLo   = 0.0f;
    zoomTimeHi   = 1.0f;
}

void CSDView::resetCamera()
{
    camAzimuth   = kDefaultAzimuth;
    camElevation = kDefaultElevation;
    camDolly     = 1.0f;
    camPanX      = 0.0f;
    camPanY      = 0.0f;
}

void CSDView::updateSource (const float* irL, int nL,
                            const float* irR, int nR,
                            double sampleRate, int generation)
{
    bool dirty = false;

    if (generation != lastGeneration)
    {
        if (nL > 0 || nR > 0)
        {
            csd.compute (irL, nL, irR, nR, sampleRate);
            haveData = csd.isReady();
        }
        else
        {
            csd.clear();
            haveData = false;
        }
        lastGeneration = generation;
        dirty = true;
    }

    const int ch = currentChannel();
    const int st = (int) currentStyle();
    if (ch != lastChannel || st != lastStyleIdx)
    {
        lastChannel  = ch;
        lastStyleIdx = st;
        dirty = true;
    }

    const bool waterfall = (currentStyle() == Style::Waterfall);
    for (auto* b : { &zoomInButton, &zoomOutButton, &panUpButton,
                     &panDownButton, &panLeftButton, &panRightButton })
        b->setVisible (waterfall);

    if (dirty)
    {
        renderImage();
        repaint();
    }
}

void CSDView::resized()
{
    const int bw = sx (20), g = sx (3), m = sx (8);
    const int col2 = getWidth() - m - bw;
    const int col1 = col2 - bw - g;
    const int col0 = col1 - bw - g;

    // Two zoom buttons stacked, then a 4-way pan cross below them.
    zoomInButton  .setBounds (col1, m,                bw, bw);
    zoomOutButton .setBounds (col1, m + bw + g,       bw, bw);

    const int crossTop = m + 2 * (bw + g) + sx (8);
    panUpButton   .setBounds (col1, crossTop,                bw, bw);
    panLeftButton .setBounds (col0, crossTop + bw + g,       bw, bw);
    panRightButton.setBounds (col2, crossTop + bw + g,       bw, bw);
    panDownButton .setBounds (col1, crossTop + 2 * (bw + g), bw, bw);

    renderImage();
}

void CSDView::paint (juce::Graphics& g)
{
    if (cachedImage.isValid())
        g.drawImageAt (cachedImage, 0, 0);
    else
        g.fillAll (juce::Colour (0xff111213));

    // Live drag-zoom selection rectangle (heatmap only).
    if (dragging && currentStyle() == Style::Heatmap
        && dragZone != DragZone::None && dragZone != DragZone::Orbit)
    {
        auto plot = heatmapPlotArea();
        float x0 = juce::jmin (dragStart.x, dragCurrent.x);
        float x1 = juce::jmax (dragStart.x, dragCurrent.x);
        float y0 = juce::jmin (dragStart.y, dragCurrent.y);
        float y1 = juce::jmax (dragStart.y, dragCurrent.y);

        if (dragZone == DragZone::TimeGutter) { x0 = (float) plot.getX(); x1 = (float) plot.getRight(); }
        if (dragZone == DragZone::FreqGutter) { y0 = (float) plot.getY(); y1 = (float) plot.getBottom(); }

        juce::Rectangle<float> r (x0, y0, x1 - x0, y1 - y0);
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.12f));
        g.fillRect (r);
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.7f));
        g.drawRect (r, sf (1.0f));
    }
}

void CSDView::renderImage()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) { cachedImage = juce::Image(); return; }

    cachedImage = juce::Image (juce::Image::ARGB, w, h, true);

    {
        juce::Graphics g (cachedImage);
        g.setColour (juce::Colour (0xff111213));
        g.fillRect (0, 0, w, h);
    }

    if (! haveData || ! csd.isReady())
    {
        juce::Graphics g (cachedImage);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (13.0f)));
        g.drawText ("Capture an IR to see its spectral decay",
                    juce::Rectangle<int> (0, 0, w, h), juce::Justification::centred, false);
        return;
    }

    if (currentStyle() == Style::Waterfall) renderWaterfall (currentChannel());
    else                                    renderHeatmap   (currentChannel());
}

void CSDView::renderHeatmap (int channel)
{
    auto plotArea     = heatmapPlotArea();
    auto gutterLeft   = juce::Rectangle<int> (0, plotArea.getY(), sx (44), plotArea.getHeight());
    auto gutterBottom = juce::Rectangle<int> (plotArea.getX(), plotArea.getBottom(),
                                              plotArea.getWidth(), sx (16));
    if (plotArea.getWidth() <= 0 || plotArea.getHeight() <= 0) return;

    float loHz = 0.0f, hiHz = 0.0f;
    heatmapFreqRange (loHz, hiHz);
    const float logMin   = std::log10 (loHz);
    const float logMax   = std::log10 (hiHz);
    const float logRange = juce::jmax (1.0e-4f, logMax - logMin);
    const float binHz1   = csd.getSampleRate() / (float) CSD::kSliceFftSize;
    const int   nSlices  = csd.getNumSlices();
    const float timeSpan = juce::jmax (1.0e-4f, zoomTimeHi - zoomTimeLo);

    {
        juce::Image::BitmapData bmp (cachedImage, juce::Image::BitmapData::writeOnly);
        for (int py = plotArea.getY(); py < plotArea.getBottom(); ++py)
        {
            const float fy    = ((float) (py - plotArea.getY()) + 0.5f) / (float) plotArea.getHeight();
            const float tFrac = zoomTimeLo + fy * timeSpan;
            const int   slice = juce::jlimit (0, nSlices - 1,
                                              juce::roundToInt (tFrac * (float) (nSlices - 1)));
            for (int px = plotArea.getX(); px < plotArea.getRight(); ++px)
            {
                const float fx  = ((float) (px - plotArea.getX()) + 0.5f) / (float) plotArea.getWidth();
                const float hz  = std::pow (10.0f, logMin + fx * logRange);
                const int   bin = juce::jlimit (0, CSD::kNumBins - 1,
                                                juce::roundToInt (hz / binHz1));
                const float db  = csd.getValue (channel, slice, bin);
                bmp.setPixelColour (px, py, csdColour ((db - CSD::kFloorDb) / (-CSD::kFloorDb)));
            }
        }
    }

    juce::Graphics g (cachedImage);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    std::vector<float> freqTicks;
    buildFreqTicks (loHz, hiHz, freqTicks);
    for (float f : freqTicks)
    {
        const float t = (std::log10 (f) - logMin) / logRange;
        const int   x = plotArea.getX() + juce::roundToInt (t * (float) plotArea.getWidth());
        g.drawText (formatHz (f),
                    juce::Rectangle<int> (x - sx (20), gutterBottom.getY(), sx (40),
                                          gutterBottom.getHeight()),
                    juce::Justification::centredTop, false);
    }

    for (int i = 0; i <= 4; ++i)
    {
        const float frac = (float) i / 4.0f;
        const int   y    = plotArea.getY() + juce::roundToInt (frac * (float) plotArea.getHeight());
        const float ms   = (zoomTimeLo + frac * timeSpan) * csd.getSpanMs();
        g.drawText (formatMs (ms),
                    juce::Rectangle<int> (gutterLeft.getX(), y - sx (6),
                                          gutterLeft.getWidth() - sx (4), sx (12)),
                    juce::Justification::centredRight, false);
    }
}

void CSDView::renderWaterfall (int channel)
{
    auto area = getLocalBounds().reduced (sx (8));
    if (area.getWidth() <= 0 || area.getHeight() <= 0) return;

    // Model box half-extents. Frequency is the long axis - a log
    // spectrum, read left to right - while level and time are shallower.
    // The non-uniform shape lives in MODEL space, before the rotation,
    // so the orbit stays rigid (a non-uniform scale applied after the
    // rotation is what shears a 3D view).
    constexpr float hx = 1.62f;   // frequency  (log Hz)
    constexpr float hy = 0.52f;   // level      (dB)
    constexpr float hz = 0.74f;   // time

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
    // fits the plot. Recomputed each render so the box fills the space
    // at every orbit angle; camDolly is the user's zoom on top.
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

    juce::Graphics g (cachedImage);

    // The freq axis spans the ACTUAL bin range (bin 1 up), not a nominal
    // 20 Hz - otherwise the box and axes anchor to an empty sub-bin-1
    // strip and float off the left edge of the real data.
    const float binHz1   = csd.getSampleRate() / (float) CSD::kSliceFftSize;
    const float loHz     = juce::jmax (kViewMinHz, binHz1);
    const float maxHz    = juce::jmax (loHz * 2.0f, csd.getMaxHz());
    const float logMin   = std::log10 (loHz);
    const float logMax   = std::log10 (maxHz);
    const float logRange = juce::jmax (1.0e-4f, logMax - logMin);
    const int   nSlices  = csd.getNumSlices();

    auto freqToMx = [&] (float hzv) -> float
    {
        return (juce::jlimit (0.0f, 1.0f, (std::log10 (hzv) - logMin) / logRange) - 0.5f)
                   * 2.0f * hx;
    };
    auto lvlToMy  = [&] (float n) -> float
    {
        return (juce::jlimit (0.0f, 1.0f, n) - 0.5f) * 2.0f * hy;
    };
    auto timeToMz = [&] (float t) -> float { return (0.5f - t) * 2.0f * hz; };

    // ---- Orientation wireframe box -------------------------------------
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

    // ---- Decay slices, depth-sorted back-to-front ----------------------
    std::vector<std::pair<float, int>> order;
    order.reserve ((size_t) nSlices);
    for (int s = 0; s < nSlices; ++s)
    {
        const float tz = nSlices > 1 ? (float) s / (float) (nSlices - 1) : 0.0f;
        order.emplace_back (project (0.0f, 0.0f, timeToMz (tz)).depth, s);
    }
    std::sort (order.begin(), order.end(),
               [] (auto& l, auto& r) { return l.first < r.first; });

    const float depthMin  = order.front().first;
    const float depthMax  = order.back().first;
    const float depthSpan = juce::jmax (1.0e-4f, depthMax - depthMin);

    for (auto& entry : order)
    {
        const int   s  = entry.second;
        const float tz = nSlices > 1 ? (float) s / (float) (nSlices - 1) : 0.0f;
        const float mz = timeToMz (tz);   // t = 0 at the front (+mz) edge

        juce::Path top, filled;
        bool open = false;
        std::vector<juce::Point<float>> basePts;

        for (int bin = 1; bin < CSD::kNumBins; ++bin)
        {
            const float hzv = (float) bin * binHz1;
            if (hzv < loHz)  continue;
            if (hzv > maxHz) break;

            const float mx = freqToMx (hzv);
            const float lv = juce::jlimit (0.0f, 1.0f,
                (csd.getValue (channel, s, bin) - CSD::kFloorDb) / (-CSD::kFloorDb));

            const P pt   = project (mx, lvlToMy (lv), mz);
            const P base = project (mx, -hy,          mz);

            if (! open) { top.startNewSubPath (pt.x, pt.y); open = true; }
            else        { top.lineTo          (pt.x, pt.y); }
            basePts.push_back ({ base.x, base.y });
        }
        if (! open || basePts.size() < 2) continue;

        filled = top;
        for (auto it = basePts.rbegin(); it != basePts.rend(); ++it)
            filled.lineTo (*it);
        filled.closeSubPath();
        g.setColour (juce::Colour (0xff111213));
        g.fillPath (filled);

        const float depthN = (entry.first - depthMin) / depthSpan;
        g.setColour (WTColors::analysis.withAlpha (0.4f + 0.6f * depthN));
        g.strokePath (top, juce::PathStrokeType (sf (1.2f)));
    }

    // ---- Axes: ticks + labels along the box edges ----------------------
    // Drawn after the slices so they read on top; anchored to box edges
    // so they rotate with the view.
    auto label3D = [&] (const juce::String& txt, P at)
    {
        g.drawText (txt,
                    juce::Rectangle<float> (at.x - sf (20.0f), at.y - sf (6.0f),
                                            sf (40.0f), sf (12.0f)).toNearestInt(),
                    juce::Justification::centred, false);
    };

    g.setFont (juce::FontOptions (sf (10.0f)));

    // Frequency axis: front-bottom edge (my = -hy, mz = +hz), along mx.
    {
        const P e0 = project (-hx, -hy, hz);
        const P e1 = project ( hx, -hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        std::vector<float> freqTicks;
        buildFreqTicks (loHz, maxHz, freqTicks);
        g.setColour (juce::Colours::grey);
        for (float f : freqTicks)
        {
            const float mx  = freqToMx (f);
            const P on  = project (mx, -hy, hz);
            const P end = project (mx, -hy, hz * 1.07f);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (formatHz (f), project (mx, -hy, hz * 1.22f));
        }
        label3D ("Hz", project (hx, -hy, hz * 1.40f));
    }

    // Time axis: left-bottom edge (mx = -hx, my = -hy), along mz.
    {
        const P e0 = project (-hx, -hy, -hz);
        const P e1 = project (-hx, -hy,  hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        for (int i = 0; i <= 4; ++i)
        {
            const float tf = (float) i / 4.0f;
            const float mz = timeToMz (tf);   // t = 0 at the front (+mz) edge
            const P on  = project (-hx,         -hy, mz);
            const P end = project (-hx * 1.05f, -hy, mz);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (formatMs (tf * csd.getSpanMs()), project (-hx * 1.16f, -hy, mz));
        }
        label3D ("s", project (-hx * 1.16f, -hy, hz * 1.34f));
    }

    // Level axis: front-right vertical edge (mx = +hx, mz = +hz), along my.
    {
        const P e0 = project (hx, -hy, hz);
        const P e1 = project (hx,  hy, hz);
        g.setColour (juce::Colour (0xff3a3e46));
        g.drawLine (e0.x, e0.y, e1.x, e1.y, sf (1.2f));

        g.setColour (juce::Colours::grey);
        for (int i = 0; i <= 4; ++i)
        {
            const float n  = (float) i / 4.0f;
            const float my = lvlToMy (n);
            const P on  = project (hx,         my, hz);
            const P end = project (hx * 1.05f, my, hz);
            g.drawLine (on.x, on.y, end.x, end.y, sf (1.0f));
            label3D (juce::String (juce::roundToInt (CSD::kFloorDb * (1.0f - n))),
                     project (hx * 1.17f, my, hz));
        }
        label3D ("dB", project (hx * 1.17f, hy * 1.30f, hz));
    }
}

void CSDView::mouseDown (const juce::MouseEvent& e)
{
    dragStart   = e.position;
    dragCurrent = e.position;
    dragging    = true;

    if (currentStyle() == Style::Waterfall)
    {
        dragZone = DragZone::Orbit;
        dragAz0  = camAzimuth;
        dragEl0  = camElevation;
        return;
    }

    auto plot = heatmapPlotArea();
    if      ((int) e.position.x < plot.getX())       dragZone = DragZone::TimeGutter;
    else if ((int) e.position.y > plot.getBottom())  dragZone = DragZone::FreqGutter;
    else                                             dragZone = DragZone::Plot;
}

void CSDView::mouseDrag (const juce::MouseEvent& e)
{
    dragCurrent = e.position;

    if (dragZone == DragZone::Orbit)
    {
        camAzimuth   = dragAz0 + (e.position.x - dragStart.x) * 0.012f;
        camElevation = juce::jlimit (-1.45f, 1.45f,
                                     dragEl0 - (e.position.y - dragStart.y) * 0.012f);
        renderImage();
    }

    repaint();
}

void CSDView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && (dragZone == DragZone::Plot
                  || dragZone == DragZone::FreqGutter
                  || dragZone == DragZone::TimeGutter))
    {
        auto plot = heatmapPlotArea();
        if (plot.getWidth() > 0 && plot.getHeight() > 0)
        {
            const float plotX = (float) plot.getX(),  plotW = (float) plot.getWidth();
            const float plotY = (float) plot.getY(),  plotH = (float) plot.getHeight();

            float x0 = juce::jlimit (plotX, plotX + plotW, juce::jmin (dragStart.x, dragCurrent.x));
            float x1 = juce::jlimit (plotX, plotX + plotW, juce::jmax (dragStart.x, dragCurrent.x));
            float y0 = juce::jlimit (plotY, plotY + plotH, juce::jmin (dragStart.y, dragCurrent.y));
            float y1 = juce::jlimit (plotY, plotY + plotH, juce::jmax (dragStart.y, dragCurrent.y));

            float loHz = 0.0f, hiHz = 0.0f;
            heatmapFreqRange (loHz, hiHz);
            const float logLo = std::log10 (loHz);
            const float logHi = std::log10 (hiHz);
            const float timeSpan = zoomTimeHi - zoomTimeLo;

            auto xToHz   = [&] (float x) { return std::pow (10.0f,
                logLo + ((x - plotX) / plotW) * (logHi - logLo)); };
            auto yToFrac = [&] (float y) { return zoomTimeLo
                + ((y - plotY) / plotH) * timeSpan; };

            const bool zoomFreq = (dragZone == DragZone::Plot || dragZone == DragZone::FreqGutter);
            const bool zoomTime = (dragZone == DragZone::Plot || dragZone == DragZone::TimeGutter);
            const float minDrag = sf (6.0f);

            if (zoomFreq && (x1 - x0) > minDrag)
            {
                const float nLo = xToHz (x0), nHi = xToHz (x1);
                if (nHi / juce::jmax (1.0e-3f, nLo) > 1.05f)
                {
                    zoomFreqLoHz = nLo;
                    zoomFreqHiHz = nHi;
                }
            }
            if (zoomTime && (y1 - y0) > minDrag)
            {
                const float nLo = yToFrac (y0), nHi = yToFrac (y1);
                if (nHi - nLo > 0.01f)
                {
                    zoomTimeLo = nLo;
                    zoomTimeHi = nHi;
                }
            }
            renderImage();
        }
    }

    dragging = false;
    dragZone = DragZone::None;
    repaint();
}

void CSDView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (currentStyle() == Style::Waterfall) resetCamera();
    else                                    resetZoom();
    renderImage();
    repaint();
}

void CSDView::mouseWheelMove (const juce::MouseEvent&,
                              const juce::MouseWheelDetails& wheel)
{
    if (currentStyle() != Style::Waterfall) return;
    if (wheel.deltaY == 0.0f) return;

    applyDolly (1.0f + wheel.deltaY * 0.6f);
}
