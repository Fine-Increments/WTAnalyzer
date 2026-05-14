/*
  ==============================================================================

    CursorReadout.cpp

  ==============================================================================
*/

#include "CursorReadout.h"

#include <cmath>

CursorReadout::CursorReadout (const SpectrumDisplay& src)
    : source (src)
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);   // don't steal events from the spectrum
    startTimerHz (30);
}

CursorReadout::~CursorReadout()
{
    stopTimer();
}

void CursorReadout::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
}

void CursorReadout::timerCallback()
{
    repaint();
}

void CursorReadout::paint (juce::Graphics& g)
{
    if (! source.isHoverActive())
        return;

    const float freq = source.getHoverFreq();
    const float db   = source.getHoverDb();

    // Frequency: integer Hz below 1 kHz, two-decimal kHz from 1 to 10 kHz,
    // one-decimal kHz above 10 kHz.
    juce::String freqStr;
    if (freq < 1000.0f)
        freqStr = juce::String ((int) std::round (freq)) + " Hz";
    else if (freq < 10000.0f)
        freqStr = juce::String (freq / 1000.0f, 2) + " kHz";
    else
        freqStr = juce::String (freq / 1000.0f, 1) + " kHz";

    juce::String dbStr;
    if (db >= 0.0f) dbStr = "+" + juce::String (db, 1) + " dB";
    else            dbStr =       juce::String (db, 1) + " dB";

    const juce::String readout = freqStr + "  |  " + dbStr;

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));
    g.drawText (readout, getLocalBounds(), juce::Justification::centredLeft);
}
