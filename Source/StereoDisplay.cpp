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

    auto configureViewButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter ("stereoView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureViewButton (divergenceButton,  0);
    configureViewButton (correlationButton, 1);
    configureViewButton (goniometerButton,  2);

    processor.apvts.addParameterListener ("stereoView", this);
    syncViewButtons();

    startTimerHz (30);
}

StereoDisplay::~StereoDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("stereoView", this);
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
    if (parameterID == "stereoView")
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
        case SubView::Divergence:  drawDivergence (g, plotArea); break;
        case SubView::Correlation: drawPlaceholder (g, plotArea, "Correlation"); break;
        case SubView::Goniometer:  drawPlaceholder (g, plotArea, "Goniometer");  break;
    }
}

void StereoDisplay::drawPlaceholder (juce::Graphics& g, juce::Rectangle<int> plotArea,
                                     const juce::String& title)
{
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (13.0f)));
    g.drawText (title + " - not yet implemented",
                plotArea, juce::Justification::centred, false);
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
