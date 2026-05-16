/*
  ==============================================================================

    StereoDisplay.cpp

  ==============================================================================
*/

#include "StereoDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Bipolar divergence axis: +/- 30 dB of R-vs-L level difference.
    constexpr float kDivMaxDb = 30.0f;   // top of plot  = R louder
    constexpr float kDivMinDb = -30.0f;  // bottom       = L louder

    constexpr float kViewMinHz = 20.0f;
}

StereoDisplay::StereoDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureRadioButton = [this] (juce::TextButton& b, juce::String paramID,
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

    configureRadioButton (divergenceButton,   "stereoView", 4, 0);
    configureRadioButton (correlationButton,  "stereoView", 4, 1);
    configureRadioButton (goniometerButton,   "stereoView", 4, 2);
    configureRadioButton (gonioPrePostButton, "gonioMode",  5, 0);
    configureRadioButton (gonioDiffButton,    "gonioMode",  5, 1);

    processor.apvts.addParameterListener ("stereoView", this);
    processor.apvts.addParameterListener ("gonioMode",  this);
    syncViewButtons();

    startTimerHz (30);
}

StereoDisplay::~StereoDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("stereoView", this);
    processor.apvts.removeParameterListener ("gonioMode",  this);
}

void StereoDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void StereoDisplay::timerCallback()
{
    repaint();
}

void StereoDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "stereoView" || parameterID == "gonioMode")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncViewButtons();
            repaint();
        });
    }
}

void StereoDisplay::syncViewButtons()
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("stereoView");
    divergenceButton .setToggleState (idx == 0, juce::dontSendNotification);
    correlationButton.setToggleState (idx == 1, juce::dontSendNotification);
    goniometerButton .setToggleState (idx == 2, juce::dontSendNotification);

    const int gmode = (int) *processor.apvts.getRawParameterValue ("gonioMode");
    gonioPrePostButton.setToggleState (gmode == 0, juce::dontSendNotification);
    gonioDiffButton   .setToggleState (gmode == 1, juce::dontSendNotification);

    // The mode toggle only applies to the Goniometer sub-view.
    const bool gonioActive = (idx == 2);
    gonioPrePostButton.setVisible (gonioActive);
    gonioDiffButton   .setVisible (gonioActive);
}

StereoDisplay::SubView StereoDisplay::currentSubView() const noexcept
{
    switch ((int) *processor.apvts.getRawParameterValue ("stereoView"))
    {
        case 1:  return SubView::Correlation;
        case 2:  return SubView::Goniometer;
        default: return SubView::Divergence;
    }
}

StereoDisplay::GonioMode StereoDisplay::currentGonioMode() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("gonioMode") == 1)
               ? GonioMode::Difference : GonioMode::PrePost;
}

void StereoDisplay::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (sx (36));

    // View selector centred in the header band.
    const int buttonW = sx (86);
    const int buttonH = sx (22);
    const int spacing = sx (6);
    const int totalW  = buttonW * 3 + spacing * 2;
    const int startX  = header.getCentreX() - totalW / 2;
    const int buttonY = header.getCentreY() - buttonH / 2;

    divergenceButton .setBounds (startX,                           buttonY, buttonW, buttonH);
    correlationButton.setBounds (startX +     (buttonW + spacing), buttonY, buttonW, buttonH);
    goniometerButton .setBounds (startX + 2 * (buttonW + spacing), buttonY, buttonW, buttonH);

    // Goniometer mode toggle at the right end of the header band - only
    // visible while the Goniometer sub-view is active.
    const int gW     = sx (72);
    const int gRight = header.getRight() - sx (8);
    gonioDiffButton   .setBounds (gRight -     gW,               buttonY, gW, buttonH);
    gonioPrePostButton.setBounds (gRight - 2 * gW - spacing,     buttonY, gW, buttonH);
}

void StereoDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    bounds.removeFromTop (sx (36));   // header band - owned by the view buttons
    auto plotArea = bounds.reduced (sx (8), sx (8));

    switch (currentSubView())
    {
        case SubView::Divergence:  drawDivergence (g, plotArea);  break;
        case SubView::Correlation: drawCorrelation (g, plotArea); break;
        case SubView::Goniometer:  drawGoniometer (g, plotArea);  break;
    }
}

void StereoDisplay::drawDivergence (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft  (sx (40));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (16));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const float sr        = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float nyquist   = sr * 0.5f;
    const float viewMaxHz = juce::jmax (kViewMinHz * 2.0f, nyquist);
    const float logMin    = std::log10 (kViewMinHz);
    const float logMax    = std::log10 (viewMaxHz);
    const float logRange  = logMax - logMin;
    const float dbRange   = kDivMaxDb - kDivMinDb;

    auto freqToX = [&] (float hz) -> float
    {
        const float t = (std::log10 (juce::jlimit (kViewMinHz, viewMaxHz, hz)) - logMin) / logRange;
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };
    auto dbToY = [&] (float db) -> float
    {
        const float t = (db - kDivMinDb) / dbRange;
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    // ---- Grid + axis labels --------------------------------------------
    const std::array<float, 7> kDbGrid { -30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f };

    g.setColour (juce::Colour (0xff2a2d32));
    for (float db : kDbGrid)
    {
        const float y = dbToY (db);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : kDbGrid)
    {
        const int y = juce::roundToInt (dbToY (db));
        // Bipolar label: magnitude plus the channel it leans toward.
        juce::String text;
        if (db > 0.0f)      text = juce::String ((int) db) + " R";
        else if (db < 0.0f) text = juce::String ((int) -db) + " L";
        else                text = "0";
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 10> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,   "100" },
        { 200.0f,   "200" }, { 500.0f,   "500"  }, { 1000.0f,  "1k"  },
        { 2000.0f,  "2k"  }, { 5000.0f,  "5k"   }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    }};
    for (auto label : kFreqLabels)
    {
        if (label.hz < kViewMinHz || label.hz > viewMaxHz) continue;
        const int x = juce::roundToInt (freqToX (label.hz));
        const int textWidth = sx (30);
        juce::Rectangle<int> r (x - textWidth / 2, labelGutterBottom.getY(),
                                textWidth, labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    // ---- Divergence trace ----------------------------------------------
    // The no-sidechain case is handled by the editor's shared
    // SidechainNotice overlay, consistently across every mode.
    const auto& div = processor.stereoAnalysis.getDivergence();
    const int   N   = (int) div.size();
    if (N <= 0) return;

    const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
    constexpr float sentFloor = StereoAnalysis::kNoMeasurementDb + 0.5f;

    // Find the lowest / highest bin with a valid measurement. Outside that
    // range the trace rests on the zero centre line; inside, gaps are
    // skipped so the trace connects valid bins directly.
    int loBin = -1, hiBin = -1;
    for (int bin = 1; bin < N; ++bin)
    {
        if (div[(size_t) bin] > sentFloor)
        {
            if (loBin < 0) loBin = bin;
            hiBin = bin;
        }
    }

    juce::Path path;
    bool       hasOpenSubPath = false;
    for (int bin = 1; bin < N; ++bin)
    {
        const float hz = (float) bin * binFreqScale;
        if (hz < kViewMinHz) continue;
        if (hz > viewMaxHz)  break;

        const float v = div[(size_t) bin];
        const bool  valid = v > sentFloor;
        const bool  inSignalRange = loBin >= 0 && bin >= loBin && bin <= hiBin;

        float drawDb;
        if (valid)                drawDb = v;
        else if (! inSignalRange) drawDb = 0.0f;     // baseline outside signal
        else                      continue;          // gap inside - interpolate

        const float x = freqToX (hz);
        const float y = dbToY (juce::jlimit (kDivMinDb, kDivMaxDb, drawDb));
        if (! hasOpenSubPath) { path.startNewSubPath (x, y); hasOpenSubPath = true; }
        else                  { path.lineTo          (x, y); }
    }

    // Two clipped passes: above the zero line in the R-variant colour
    // (R louder), below in the L-master colour (L louder).
    const int zeroY = juce::roundToInt (dbToY (0.0f));
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plotArea.getX(), plotArea.getY(),
                            plotArea.getWidth(),
                            juce::jmax (0, zeroY - plotArea.getY()));
        g.setColour (WTColors::analysis_R);
        g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plotArea.getX(), zeroY,
                            plotArea.getWidth(),
                            juce::jmax (0, plotArea.getBottom() - zeroY));
        g.setColour (WTColors::analysis);
        g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
    }

    // Zero-divergence centre line, drawn on top: a balanced signal's
    // trace lies flat beneath it and reads as a clean green line.
    g.setColour (WTColors::analysis);
    g.drawLine ((float) plotArea.getX(), (float) zeroY,
                (float) plotArea.getRight(), (float) zeroY, sf (1.5f));
}

void StereoDisplay::drawCorrelation (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft  (sx (40));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (16));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const float sr        = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float nyquist   = sr * 0.5f;
    const float viewMaxHz = juce::jmax (kViewMinHz * 2.0f, nyquist);
    const float logMin    = std::log10 (kViewMinHz);
    const float logMax    = std::log10 (viewMaxHz);
    const float logRange  = logMax - logMin;

    auto freqToX = [&] (float hz) -> float
    {
        const float t = (std::log10 (juce::jlimit (kViewMinHz, viewMaxHz, hz)) - logMin) / logRange;
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };
    auto corrToY = [&] (float c) -> float
    {
        const float t = (c + 1.0f) * 0.5f;   // -1 -> bottom, +1 -> top
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    // ---- Grid + axis labels --------------------------------------------
    const std::array<float, 5> kCorrGrid { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f };

    g.setColour (juce::Colour (0xff2a2d32));
    for (float c : kCorrGrid)
    {
        const float y = corrToY (c);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float c : kCorrGrid)
    {
        const int y = juce::roundToInt (corrToY (c));
        juce::String text;
        if (c > 0.0f)      text = "+" + juce::String (c, 1);
        else if (c < 0.0f) text = juce::String (c, 1);
        else               text = "0";
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 10> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,   "100" },
        { 200.0f,   "200" }, { 500.0f,   "500"  }, { 1000.0f,  "1k"  },
        { 2000.0f,  "2k"  }, { 5000.0f,  "5k"   }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    }};
    for (auto label : kFreqLabels)
    {
        if (label.hz < kViewMinHz || label.hz > viewMaxHz) continue;
        const int x = juce::roundToInt (freqToX (label.hz));
        const int textWidth = sx (30);
        juce::Rectangle<int> r (x - textWidth / 2, labelGutterBottom.getY(),
                                textWidth, labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    // ---- Correlation trace ---------------------------------------------
    // The no-sidechain case is handled by the editor's shared
    // SidechainNotice overlay, consistently across every mode.
    const auto& corr = processor.stereoAnalysis.getCorrelation();
    const int   N    = (int) corr.size();
    if (N <= 0) return;

    const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
    constexpr float sentFloor = StereoAnalysis::kNoMeasurementDb + 0.5f;

    // Bins with no post-signal energy carry the sentinel and break the
    // path - correlation is undefined where there is nothing to measure.
    juce::Path path;
    bool       hasOpenSubPath = false;
    for (int bin = 1; bin < N; ++bin)
    {
        const float hz = (float) bin * binFreqScale;
        if (hz < kViewMinHz) continue;
        if (hz > viewMaxHz)  break;

        const float v = corr[(size_t) bin];
        if (v <= sentFloor) { hasOpenSubPath = false; continue; }

        const float x = freqToX (hz);
        const float y = corrToY (juce::jlimit (-1.0f, 1.0f, v));
        if (! hasOpenSubPath) { path.startNewSubPath (x, y); hasOpenSubPath = true; }
        else                  { path.lineTo          (x, y); }
    }

    g.setColour (WTColors::analysis);
    g.strokePath (path, juce::PathStrokeType (sf (1.6f)));

    // ---- Broadband correlation HUD -------------------------------------
    // Energy-weighted aggregate of the per-bin coherence. Tracks the
    // classic phase meter; the true time-domain figure belongs to the
    // Goniometer view.
    const float bb = processor.stereoAnalysis.getBroadbandCorrelation();
    const juce::String bbValue = (bb > sentFloor)
        ? (bb >= 0.0f ? "+" : "") + juce::String (bb, 2)
        : juce::String ("--");

    const int hudW = sx (124);
    const int hudH = sx (20);
    juce::Rectangle<int> hudBox (plotArea.getRight() - hudW - sx (8),
                                 plotArea.getY() + sx (8), hudW, hudH);

    g.setColour (juce::Colour (0xcc111213));
    g.fillRoundedRectangle (hudBox.toFloat(), sf (3.0f));

    auto hudInner = hudBox.reduced (sx (8), 0);
    g.setFont (juce::FontOptions (sf (11.0f)));
    g.setColour (juce::Colours::grey);
    g.drawText ("Broadband", hudInner, juce::Justification::centredLeft, false);
    g.setColour (WTColors::analysis);
    g.drawText (bbValue, hudInner, juce::Justification::centredRight, false);
}

void StereoDisplay::drawGoniometer (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (area);

    // The goniometer is intrinsically square - use the largest centred
    // square that fits the panel.
    const int side = juce::jmin (area.getWidth(), area.getHeight());
    if (side <= 0) return;

    const float cx     = (float) area.getCentreX();
    const float cy     = (float) area.getCentreY();
    const float radius = (float) side * 0.5f - sf (14.0f);
    const float diag   = radius * 0.70710678f;   // 45-degree projection

    // ---- Graticule -----------------------------------------------------
    // M (mono / in-phase) axis vertical, S (side) axis horizontal, the
    // L and R channel axes on the 45-degree diagonals, unit circle.
    g.setColour (juce::Colour (0xff2a2d32));
    g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, sf (1.0f));
    g.drawLine (cx, cy - radius, cx, cy + radius, sf (1.0f));
    g.drawLine (cx - radius, cy, cx + radius, cy, sf (1.0f));
    g.drawLine (cx - diag, cy - diag, cx + diag, cy + diag, sf (1.0f));
    g.drawLine (cx + diag, cy - diag, cx - diag, cy + diag, sf (1.0f));

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    auto labelAt = [&] (const juce::String& t, float px, float py)
    {
        juce::Rectangle<float> r (px - sf (9.0f), py - sf (7.0f), sf (18.0f), sf (14.0f));
        g.drawText (t, r.toNearestInt(), juce::Justification::centred, false);
    };
    labelAt ("M", cx,                    cy - radius - sf (9.0f));
    labelAt ("L", cx - diag - sf (8.0f), cy - diag - sf (8.0f));
    labelAt ("R", cx + diag + sf (8.0f), cy - diag - sf (8.0f));

    // ---- Sample cloud --------------------------------------------------
    // Each L/R pair projected onto the rotated mid/side axes:
    //   x = (R - L)/sqrt2  -> +x toward R, -x toward L
    //   y = (L + R)/sqrt2  -> +y up (in-phase / mono), down for negative
    // A mono signal collapses to the vertical M axis; an anti-phase pair
    // spreads along the horizontal S axis.
    //
    // Pre / Post mode overlays the input cloud (pre colour) and output
    // cloud (post colour) so the device's effect is the visible delta.
    // Difference mode scopes (post - aligned pre) per channel - the
    // stereo image of the signal the device ADDED, the time-domain
    // analog of the Divergence view.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);

    const int N    = WTAnalyzerAudioProcessor::kGonioBufferSize;
    const int head = processor.gonioWritePos.load (std::memory_order_relaxed);
    constexpr float kRoot2Inv = 0.70710678f;
    const float dot = sf (1.6f);

    auto plotCloud = [&] (const float* chL, const float* chR,
                          const float* subL, const float* subR,
                          juce::Colour colour)
    {
        g.setColour (colour);
        for (int i = 0; i < N; ++i)
        {
            const int idx = (head + i) & (N - 1);
            float l = chL[(size_t) idx];
            float r = chR[(size_t) idx];
            if (subL != nullptr)
            {
                l -= subL[(size_t) idx];
                r -= subR[(size_t) idx];
            }
            const float px = cx + (r - l) * kRoot2Inv * radius;
            const float py = cy - (l + r) * kRoot2Inv * radius;
            g.fillRect (px - dot * 0.5f, py - dot * 0.5f, dot, dot);
        }
    };

    if (currentGonioMode() == GonioMode::Difference)
    {
        plotCloud (processor.gonioPostL.data(), processor.gonioPostR.data(),
                   processor.gonioPreL.data(),  processor.gonioPreR.data(),
                   WTColors::analysis.withAlpha (0.6f));
    }
    else
    {
        // Pre cloud first, post drawn on top.
        plotCloud (processor.gonioPreL.data(),  processor.gonioPreR.data(),
                   nullptr, nullptr, WTColors::preEffect.withAlpha (0.5f));
        plotCloud (processor.gonioPostL.data(), processor.gonioPostR.data(),
                   nullptr, nullptr, WTColors::postEffect.withAlpha (0.5f));
    }
}
