/*
  ==============================================================================

    CursorReadout.h
    Small text panel that shows the frequency and dB value at the
    SpectrumDisplay's current cursor position. Lives in the layout strip
    between the spectrum and the level meters, left-aligned. Updates at
    30 Hz via its own Timer; reads SpectrumDisplay's hover state via
    const-ref getters - no callback wiring needed.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SpectrumDisplay.h"

class CursorReadout  : public juce::Component,
                       private juce::Timer
{
public:
    explicit CursorReadout (const SpectrumDisplay& src);
    ~CursorReadout() override;

    void setUiScale (float newScale) noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    float sf (float v) const noexcept { return v * uiScale; }

    const SpectrumDisplay& source;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CursorReadout)
};
