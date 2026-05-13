/*
  ==============================================================================

    LevelMetersPanel.cpp

  ==============================================================================
*/

#include "LevelMetersPanel.h"

LevelMetersPanel::LevelMetersPanel (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (false);
    startTimerHz (30);
}

LevelMetersPanel::~LevelMetersPanel()
{
    stopTimer();
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
                                  const juce::String& label, float db, bool active)
{
    auto labelArea = row.removeFromLeft (sx (110));
    g.setColour (active ? juce::Colours::whitesmoke : juce::Colours::grey);
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
        g.setColour (juce::Colours::limegreen);
        g.fillRect (fill);
    }

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("(not routed)"),
                meter.reduced (sx (6), 0),
                juce::Justification::centredRight);
}

void LevelMetersPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    const int rowHeight = sx (40);
    const int gap       = sx (4);

    auto postRow = bounds.removeFromTop (rowHeight);
    bounds.removeFromTop (gap);
    auto preRow  = bounds.removeFromTop (rowHeight);

    drawMeter (g, postRow, "Post-Effect",
               processor.postEffectLevelDb.load (std::memory_order_relaxed),
               true);

    drawMeter (g, preRow, "Pre-Effect",
               processor.preEffectLevelDb.load (std::memory_order_relaxed),
               processor.preBusActive.load     (std::memory_order_relaxed));
}
