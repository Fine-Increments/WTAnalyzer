/*
  ==============================================================================

    DynamicsDisplay.cpp

  ==============================================================================
*/

#include "DynamicsDisplay.h"
#include "Colors.h"
#include "Analyses/DynamicsCurve.h"

#include <cmath>

namespace
{
    // Shared axis span for both input and output, so the unity line is a
    // true 45-degree diagonal. Eight 12 dB divisions span the range
    // exactly: -90, -78, ... -6, +6.
    constexpr float kAxisMinDb = DynamicsCurve::kMinInputDb;   // -90
    constexpr float kAxisMaxDb = DynamicsCurve::kMaxInputDb;   //  +6
    constexpr int   kNumDivs   = 8;

    constexpr int kHeaderH  = 36;
    constexpr int kReadoutH = 18;
    constexpr int kPadH     = 8;
    constexpr int kGutterL  = 44;
    constexpr int kGutterB  = 18;
}

DynamicsDisplay::DynamicsDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this] { processor.dynamicsCurve.requestClear(); };   // deferred to the audio thread

    startTimerHz (30);
}

DynamicsDisplay::~DynamicsDisplay()
{
    stopTimer();
}

void DynamicsDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void DynamicsDisplay::timerCallback()
{
    repaint();
}

juce::Rectangle<int> DynamicsDisplay::plotBounds() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop    (sx (kHeaderH));
    bounds.removeFromBottom (sx (kReadoutH));
    auto area = bounds.reduced (sx (kPadH), sx (kPadH));
    area.removeFromLeft   (sx (kGutterL));
    area.removeFromBottom (sx (kGutterB));
    return area;
}

void DynamicsDisplay::mouseMove (const juce::MouseEvent& e)
{
    cursorPos    = e.getPosition();
    cursorInside = true;
    repaint();
}

void DynamicsDisplay::mouseExit (const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}

void DynamicsDisplay::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (sx (kHeaderH));

    const int buttonW = sx (58);
    const int buttonH = sx (22);
    clearButton.setBounds (header.getCentreX() - buttonW / 2,
                           header.getCentreY() - buttonH / 2,
                           buttonW, buttonH);
}

void DynamicsDisplay::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff111213));
    g.fillRect (getLocalBounds());

    auto readoutRow = getLocalBounds();
    readoutRow.removeFromTop (getHeight() - sx (kReadoutH));

    const auto plotArea = plotBounds();
    if (plotArea.getWidth() <= 0 || plotArea.getHeight() <= 0) return;

    auto labelGutterLeft   = plotArea.withX (plotArea.getX() - sx (kGutterL))
                                     .withWidth (sx (kGutterL));
    auto labelGutterBottom = plotArea.withY (plotArea.getBottom())
                                     .withHeight (sx (kGutterB));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const float span = kAxisMaxDb - kAxisMinDb;

    auto dbToX = [&] (float db) -> float
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kAxisMinDb) / span);
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };
    auto dbToY = [&] (float db) -> float
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kAxisMinDb) / span);
        return (float) plotArea.getBottom() - t * (float) plotArea.getHeight();
    };

    // ---- Grid -----------------------------------------------------------
    g.setColour (juce::Colour (0xff2a2d32));
    for (int i = 0; i <= kNumDivs; ++i)
    {
        const float db = kAxisMinDb + span * (float) i / (float) kNumDivs;
        const float x  = dbToX (db);
        const float y  = dbToY (db);
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    // ---- Axis labels ----------------------------------------------------
    // The extreme tick at +6 dB is replaced by the axis caption on each
    // axis, so the caption never overlaps a numeric tick.
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (int i = 0; i <= kNumDivs; ++i)
    {
        const float db    = kAxisMinDb + span * (float) i / (float) kNumDivs;
        const bool  isTop = (i == kNumDivs);

        const int y = juce::roundToInt (dbToY (db));
        // Numeric ticks sit shifted left so the bottom one clears the
        // X-axis label in the corner; the wider "out dB" caption keeps
        // the full gutter width (it is at the top, with nothing to hit).
        const int rightInset = isTop ? sx (4) : sx (12);
        g.drawText (isTop ? juce::String ("out dB") : juce::String (juce::roundToInt (db)),
                    juce::Rectangle<int> (labelGutterLeft.getX(), y - sx (6),
                                          labelGutterLeft.getWidth() - rightInset, sx (12)),
                    juce::Justification::centredRight, false);

        const int x = juce::roundToInt (dbToX (db));
        if (isTop)
            g.drawText ("in dB",
                        juce::Rectangle<int> (x - sx (40), labelGutterBottom.getY(),
                                              sx (38), labelGutterBottom.getHeight()),
                        juce::Justification::centredRight, false);
        else
            g.drawText (juce::String (juce::roundToInt (db)),
                        juce::Rectangle<int> (x - sx (18), labelGutterBottom.getY(),
                                              sx (36), labelGutterBottom.getHeight()),
                        juce::Justification::centredTop, false);
    }

    // ---- Unity reference diagonal --------------------------------------
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.35f));
    g.drawLine (dbToX (kAxisMinDb), dbToY (kAxisMinDb),
                dbToX (kAxisMaxDb), dbToY (kAxisMaxDb), sf (1.0f));

    // ---- Captured transfer curves --------------------------------------
    const bool hasData = processor.dynamicsCurve.hasAnyData();

    if (! hasData)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Feed a slow amplitude ramp through the device",
                    plotArea, juce::Justification::centred, false);
    }
    else
    {
        const bool showL = *processor.apvts.getRawParameterValue ("showChannelL") > 0.5f;
        const bool showR = *processor.apvts.getRawParameterValue ("showChannelR") > 0.5f;
        const int  numBins = processor.dynamicsCurve.getNumBins();

        auto strokeCurve = [&] (bool rightChannel, juce::Colour colour)
        {
            juce::Path path;
            bool open = false;
            for (int b = 0; b < numBins; ++b)
            {
                const float v = rightChannel ? processor.dynamicsCurve.getOutputDbR (b)
                                             : processor.dynamicsCurve.getOutputDbL (b);
                if (v == DynamicsCurve::kNoData) { open = false; continue; }

                const float x = dbToX (DynamicsCurve::binInputDb (b));
                const float y = dbToY (v);
                if (! open) { path.startNewSubPath (x, y); open = true; }
                else        { path.lineTo          (x, y); }
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
        };

        if (showR) strokeCurve (true,  WTColors::analysis_R);   // R drawn first
        if (showL) strokeCurve (false, WTColors::analysis);     // L on top
    }

    // ---- Cursor readout strip ------------------------------------------
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));

    juce::String readout;
    if (cursorInside && plotArea.contains (cursorPos))
    {
        const float tx = (float) (cursorPos.x - plotArea.getX())
                            / (float) plotArea.getWidth();
        const float ty = (float) (plotArea.getBottom() - cursorPos.y)
                            / (float) plotArea.getHeight();
        const float inDb  = kAxisMinDb + juce::jlimit (0.0f, 1.0f, tx) * span;
        const float outDb = kAxisMinDb + juce::jlimit (0.0f, 1.0f, ty) * span;
        readout = "in " + juce::String (inDb, 1) + " dB    out "
                        + juce::String (outDb, 1) + " dB";
    }

    g.drawText (readout,
                readoutRow.withTrimmedLeft (sx (kPadH)),
                juce::Justification::centredLeft, false);
}
