/*
  ==============================================================================

    THDDisplay.cpp

  ==============================================================================
*/

#include "THDDisplay.h"
#include "Colors.h"
#include "Analyses/THDMeasurement.h"

#include <cmath>

THDDisplay::THDDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);
    startTimerHz (30);
}

THDDisplay::~THDDisplay()
{
    stopTimer();
}

void THDDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
}

void THDDisplay::timerCallback()
{
    repaint();
}

void THDDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    const auto& thd = processor.thdMeasurement;

    if (! thd.isValid())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (14.0f)));
        g.drawText ("Play a single sine tone to measure THD",
                    bounds, juce::Justification::centred, false);
        return;
    }

    // ---- Top: big THD% readout ----------------------------------------------
    auto topSection = bounds.removeFromTop (sx (72));
    auto subSection = bounds.removeFromTop (sx (22));

    const float thdPct = thd.getTotalThdPercent();
    juce::String thdText;
    if (thdPct < 0.01f)         thdText = juce::String (thdPct, 4) + "%";
    else if (thdPct < 1.0f)     thdText = juce::String (thdPct, 3) + "%";
    else if (thdPct < 100.0f)   thdText = juce::String (thdPct, 2) + "%";
    else                        thdText = juce::String (thdPct, 1) + "%";

    g.setColour (WTColors::thdHarmonic);
    g.setFont (juce::FontOptions (sf (36.0f)));
    g.drawText (thdText + " THD", topSection, juce::Justification::centred, false);

    // ---- Subline: fundamental frequency and dB level ------------------------
    const float f0    = thd.getFundamentalHz();
    const float fundDb = thd.getHarmonicDb (1);

    juce::String freqStr;
    if      (f0 < 1000.0f)   freqStr = juce::String ((int) std::round (f0)) + " Hz";
    else if (f0 < 10000.0f)  freqStr = juce::String (f0 / 1000.0f, 2) + " kHz";
    else                     freqStr = juce::String (f0 / 1000.0f, 1) + " kHz";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("Fundamental " + freqStr + "   at " + juce::String (fundDb, 1) + " dB",
                subSection, juce::Justification::centred, false);

    // ---- Bars area ----------------------------------------------------------
    drawHarmonicBars (g, bounds.reduced (sx (24), sx (12)));
}

void THDDisplay::drawHarmonicBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& thd = processor.thdMeasurement;

    // Y axis: dB ratio relative to the fundamental. h1 sits at 0 dB at the
    // top, harmonics extend down by their attenuation.
    constexpr float kMaxDb =   0.0f;
    constexpr float kMinDb = -100.0f;
    const float     dbRange = kMaxDb - kMinDb;

    // Reserve gutters for axis labels.
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (32));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    // Background of bar area.
    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    // dB gridlines + labels (left axis).
    g.setColour (juce::Colour (0xff2a2d32));
    const std::array<float, 5> kDbGrid { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f };
    for (float db : kDbGrid)
    {
        const float t = (db - kMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());
        g.drawLine ((float) plotArea.getX(), (float) y, (float) plotArea.getRight(), (float) y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : kDbGrid)
    {
        const float t = (db - kMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());

        const int textHeight = sx (12);
        const juce::String text = (db == 0.0f) ? juce::String ("0") : juce::String ((int) db);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4), textHeight);
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    // Bars: cap at the first 12 harmonics or whatever fits below Nyquist.
    const int numBars = juce::jmin (thd.getNumValidHarmonics(), 12);
    if (numBars <= 0) return;

    const float availableWidth = (float) plotArea.getWidth();
    const float slotWidth      = availableWidth / (float) numBars;
    const int   barInset       = sx (6);

    for (int i = 0; i < numBars; ++i)
    {
        const int harmonicNum = i + 1;
        const float ratioDb   = (i == 0) ? 0.0f : thd.getHarmonicRatioDb (harmonicNum);

        // Skip bars where the harmonic is below the floor (visual noise).
        if (ratioDb <= kMinDb + 0.5f) continue;

        const float t          = (juce::jlimit (kMinDb, kMaxDb, ratioDb) - kMinDb) / dbRange;
        const int   barHeight  = juce::roundToInt (t * (float) plotArea.getHeight());

        const int   slotLeft   = plotArea.getX() + juce::roundToInt (i * slotWidth);
        const int   slotRight  = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);
        const int   barLeft    = slotLeft  + barInset;
        const int   barRight   = slotRight - barInset;
        const int   barTop     = plotArea.getBottom() - barHeight;

        juce::Rectangle<int> bar (barLeft, barTop, barRight - barLeft, barHeight);

        // Fundamental in the post-effect channel colour (it's the wanted
        // signal); harmonics in the distortion colour (they're artifacts).
        g.setColour (i == 0 ? WTColors::postEffect : WTColors::thdHarmonic);
        g.fillRect (bar);

        // Harmonic label below the bar (h1, h2, ...).
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (10.0f)));
        juce::Rectangle<int> labelRect (slotLeft, labelGutterBottom.getY(),
                                        slotRight - slotLeft, labelGutterBottom.getHeight());
        g.drawText ("h" + juce::String (harmonicNum), labelRect,
                    juce::Justification::centredTop, false);
    }
}
