/*
  ==============================================================================

    LevelMetersPanel.cpp

  ==============================================================================
*/

#include "LevelMetersPanel.h"
#include "Colors.h"

LevelMetersPanel::LevelMetersPanel (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (false);

    addAndMakeVisible (meterModeButton);
    meterModeButton.setClickingTogglesState (true);
    meterModeButton.onClick = [this] { updateMeterModeButtonText(); };

    meterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "meterUseRms", meterModeButton);

    updateMeterModeButtonText();

    startTimerHz (30);
}

LevelMetersPanel::~LevelMetersPanel()
{
    stopTimer();
}

void LevelMetersPanel::updateMeterModeButtonText()
{
    meterModeButton.setButtonText (meterModeButton.getToggleState() ? "RMS" : "Peak");
}

void LevelMetersPanel::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
}

void LevelMetersPanel::timerCallback()
{
    repaint();
}

void LevelMetersPanel::drawMeterHalf (juce::Graphics& g, juce::Rectangle<int> bar,
                                      juce::Colour colour, float db, bool active)
{
    if (! active)
        return;

    const float clamped = juce::jlimit (-60.0f, 0.0f, db);
    const float ratio   = (clamped + 60.0f) / 60.0f;
    auto fill = bar.withWidth (juce::roundToInt ((float) bar.getWidth() * ratio));

    // base -> white linear gradient, anchored across the full bar width so
    // the colour at any horizontal position is consistent regardless of how
    // far the fill currently extends. The midpoint is a brightened version
    // of the base colour - this preserves the channel's identity in the
    // bulk of the range and reads "hot / clipping" as it fades to white.
    juce::ColourGradient gradient (
        colour,
        (float) bar.getX(),     (float) bar.getCentreY(),
        juce::Colours::white,
        (float) bar.getRight(), (float) bar.getCentreY(),
        false);
    gradient.addColour (0.70, colour.brighter (0.4f));

    g.setGradientFill (gradient);
    g.fillRect (fill);
}

void LevelMetersPanel::drawMeter (juce::Graphics& g, juce::Rectangle<int> row,
                                  const juce::String& label,
                                  juce::Colour lColour, juce::Colour rColour,
                                  float lDb, float rDb,
                                  bool lActive, bool rActive)
{
    auto labelArea = row.removeFromLeft (sx (110));
    const bool anyActive = lActive || rActive;
    g.setColour (anyActive ? lColour : WTColors::dim (lColour, 0.45f));
    g.setFont (juce::FontOptions (sf (14.0f)));
    g.drawText (label, labelArea, juce::Justification::centredLeft);

    // Gutter between the channel label and the bar - holds the L/R glyphs
    // that identify which half of the bar is which channel.
    auto indicatorCol = row.removeFromLeft (sx (14));

    auto meter = row.reduced (sx (4), sx (6));
    g.setColour (juce::Colour (0xff111213));
    g.fillRect (meter);

    // Split the bar in half vertically. L sits on top, R on the bottom.
    const int halfHeight = meter.getHeight() / 2;
    auto lBar = meter.withHeight (halfHeight);
    auto rBar = meter.withTrimmedTop (halfHeight);

    drawMeterHalf (g, lBar, lColour, lDb, lActive);
    drawMeterHalf (g, rBar, rColour, rDb, rActive);

    // L / R indicator glyphs vertically centred on their respective halves.
    g.setFont (juce::FontOptions (sf (10.0f)));
    g.setColour (lActive ? lColour : WTColors::dim (lColour, 0.45f));
    g.drawText ("L",
                juce::Rectangle<int> (indicatorCol.getX(), lBar.getY(),
                                      indicatorCol.getWidth(), lBar.getHeight()),
                juce::Justification::centred);
    g.setColour (rActive ? rColour : WTColors::dim (rColour, 0.45f));
    g.drawText ("R",
                juce::Rectangle<int> (indicatorCol.getX(), rBar.getY(),
                                      indicatorCol.getWidth(), rBar.getHeight()),
                juce::Justification::centred);

    // Numeric readouts, one per half, right-aligned inside each sub-bar.
    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (10.0f)));
    if (anyActive)
    {
        g.drawText (juce::String (lDb, 1) + " dB",
                    lBar.reduced (sx (6), 0),
                    juce::Justification::centredRight);
        g.drawText (juce::String (rDb, 1) + " dB",
                    rBar.reduced (sx (6), 0),
                    juce::Justification::centredRight);
    }
    else
    {
        g.drawText ("(not routed)",
                    meter.reduced (sx (6), 0),
                    juce::Justification::centredRight);
    }
}

void LevelMetersPanel::drawLevelScale (juce::Graphics& g, juce::Rectangle<int> row)
{
    // Align horizontally to the meter bars: skip the 110 px label area and
    // the 14 px L/R indicator gutter on the left, then match the 4 px
    // reduce that drawMeter() applies.
    auto strip = row;
    strip.removeFromLeft (sx (110) + sx (14));
    strip = strip.reduced (sx (4), 0);

    // 12 dB increments to match the spectrum display's vertical labels.
    const std::array<float, 6> kLevels { -60.0f, -48.0f, -36.0f, -24.0f, -12.0f, 0.0f };

    // Tick marks at top and bottom of the strip, visually connecting the
    // labels to the meter bars above and below.
    g.setColour (juce::Colour (0xff444a52));
    for (float db : kLevels)
    {
        const float ratio = (db + 60.0f) / 60.0f;
        const float x     = (float) strip.getX() + ratio * (float) strip.getWidth();
        g.drawLine (x, (float) strip.getY(),                    x, (float) strip.getY() + sf (3.0f), sf (1.0f));
        g.drawLine (x, (float) strip.getBottom() - sf (3.0f),   x, (float) strip.getBottom(),        sf (1.0f));
    }

    // Numeric labels centered between the tick marks.
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : kLevels)
    {
        const float ratio = (db + 60.0f) / 60.0f;
        const int   x     = strip.getX() + juce::roundToInt (ratio * (float) strip.getWidth());

        const int textWidth = sx (40);
        juce::Rectangle<int> labelRect (x - textWidth / 2, strip.getY(), textWidth, strip.getHeight());

        const juce::String text = (db == 0.0f) ? juce::String ("0") : juce::String ((int) db);
        g.drawText (text, labelRect, juce::Justification::centred);
    }
}

void LevelMetersPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    const int rowHeight   = sx (40);
    const int scaleHeight = sx (20);

    auto postRow  = bounds.removeFromTop (rowHeight);
    auto scaleRow = bounds.removeFromTop (scaleHeight);
    auto preRow   = bounds.removeFromTop (rowHeight);

    // Pick peak vs RMS based on the meterUseRms parameter. Both are computed
    // every block; we just decide which one to display.
    const bool useRms = *processor.apvts.getRawParameterValue ("meterUseRms") > 0.5f;

    const float postLDb = useRms ? processor.postEffectLevelDb  .load (std::memory_order_relaxed)
                                 : processor.postEffectPeakDb   .load (std::memory_order_relaxed);
    const float postRDb = useRms ? processor.postEffectLevelDb_R.load (std::memory_order_relaxed)
                                 : processor.postEffectPeakDb_R .load (std::memory_order_relaxed);
    const float preLDb  = useRms ? processor.preEffectLevelDb   .load (std::memory_order_relaxed)
                                 : processor.preEffectPeakDb    .load (std::memory_order_relaxed);
    const float preRDb  = useRms ? processor.preEffectLevelDb_R .load (std::memory_order_relaxed)
                                 : processor.preEffectPeakDb_R  .load (std::memory_order_relaxed);

    const bool preActive = processor.preBusActive.load (std::memory_order_relaxed);

    drawMeter (g, postRow, "Post-Effect",
               WTColors::postEffect, WTColors::postEffect_R,
               postLDb, postRDb,
               true, true);

    drawLevelScale (g, scaleRow);

    drawMeter (g, preRow, "Pre-Effect",
               WTColors::preEffect, WTColors::preEffect_R,
               preLDb, preRDb,
               preActive, preActive);
}

void LevelMetersPanel::resized()
{
    auto bounds = getLocalBounds();
    const int rowHeight   = sx (40);
    const int scaleHeight = sx (20);

    bounds.removeFromTop (rowHeight);   // skip past the post-effect row
    auto scaleRow = bounds.removeFromTop (scaleHeight);

    // Mode toggle sits in the left 110 px of the scale row (matching the
    // meter-label column above and below), vertically padded for a clean
    // button hit area.
    auto buttonArea = scaleRow.removeFromLeft (sx (110));
    meterModeButton.setBounds (buttonArea.reduced (sx (16), sx (1)));
}
