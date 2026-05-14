/*
  ==============================================================================

    Colors.h
    Centralised colour palette for WTAnalyzer's UI. Anything that should look
    consistent across components - the pre/post channel identity colours,
    analysis trace colours, etc. - lives here so a single edit retones the
    whole plugin.

    Compile-time `inline const` constants today; if we ever need runtime
    theming (light/dark, user-selectable palette), wrap these in a Colours
    class with a singleton accessor. Until then, edit-and-rebuild is the
    intended change path.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace WTColors
{
    // ---- Signal channel identity --------------------------------------------
    // Used by spectrum traces, level meter labels, and anywhere else that
    // needs to consistently identify the pre-effect vs post-effect signal.
    inline const juce::Colour preEffect  { 0xffe0a050 };  // amber
    inline const juce::Colour postEffect { 0xff5cd4e8 };  // cyan

    // ---- Analysis trace colours ---------------------------------------------
    // Per-analysis result curves drawn on top of the spectrum.
    inline const juce::Colour frequencyResponse { 0xff9be15c };  // green-yellow

    // ---- THD analysis -------------------------------------------------------
    // Fundamental uses the postEffect colour (it is the signal you wanted),
    // harmonics get a warm distortion tone so they read as "artifact, not
    // signal" at a glance.
    inline const juce::Colour thdHarmonic { 0xffe8743a };  // warm orange

    // ---- Optional: dim variant used for inactive channels -------------------
    // For consistent "not routed / no data" rendering. Each consumer can opt
    // in or use its own dimming math.
    inline juce::Colour dim (juce::Colour c, float alpha = 0.5f) noexcept
    {
        return c.withAlpha (alpha);
    }
}
