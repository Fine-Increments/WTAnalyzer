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

void LevelMetersPanel::drawMeter (juce::Graphics& g, juce::Rectangle<int> row,
                                  const juce::String& label, juce::Colour labelColour,
                                  float db, bool active)
{
    auto labelArea = row.removeFromLeft (sx (110));
    g.setColour (active ? labelColour : WTColors::dim (labelColour, 0.45f));
    g.setFont (juce::FontOptions (sf (14.0f)));
    g.drawText (label, labelArea, juce::Justification::centredLeft);

    auto meter = row.reduced (sx (4), sx (6));
    g.setColour (juce::Colour (0xff111213));
    g.fillRect (meter);

    if (active)
    {
        const float clamped = juce::jlimit (-60.0f, 0.0f, db);
        const float ratio   = (clamped + 60.0f) / 60.0f;
        auto fill = meter.withWidth (juce::roundToInt ((float) meter.getWidth() * ratio));

        // Gradient anchored to the full meter width so the colour at any
        // level is the same regardless of how far the fill currently extends.
        // Green for the bulk of the range, transitioning through yellow to
        // orange to red near 0 dBFS.
        juce::ColourGradient gradient (
            juce::Colour (0xff5cc26b),
            (float) meter.getX(),     (float) meter.getCentreY(),
            juce::Colour (0xffd83838),
            (float) meter.getRight(), (float) meter.getCentreY(),
            false);
        gradient.addColour (0.55, juce::Colour (0xff5cc26b));   // green plateau to ~-27 dB
        gradient.addColour (0.75, juce::Colour (0xfff5c842));   // yellow around -15 dB
        gradient.addColour (0.88, juce::Colour (0xffe6731c));   // orange around -7 dB

        g.setGradientFill (gradient);
        g.fillRect (fill);
    }

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("(not routed)"),
                meter.reduced (sx (6), 0),
                juce::Justification::centredRight);
}

void LevelMetersPanel::drawLevelScale (juce::Graphics& g, juce::Rectangle<int> row)
{
    // Align horizontally to the meter bars: skip the 110 px label area on
    // the left, then match the 4 px reduce that drawMeter() applies.
    auto strip = row;
    strip.removeFromLeft (sx (110));
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

    const float postDb = useRms ? processor.postEffectLevelDb.load (std::memory_order_relaxed)
                                 : processor.postEffectPeakDb .load (std::memory_order_relaxed);
    const float preDb  = useRms ? processor.preEffectLevelDb .load (std::memory_order_relaxed)
                                 : processor.preEffectPeakDb  .load (std::memory_order_relaxed);

    drawMeter (g, postRow, "Post-Effect", WTColors::postEffect, postDb, true);

    drawLevelScale (g, scaleRow);

    drawMeter (g, preRow, "Pre-Effect", WTColors::preEffect, preDb,
               processor.preBusActive.load (std::memory_order_relaxed));
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
