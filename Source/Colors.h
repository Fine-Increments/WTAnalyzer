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

    // ---- Analysis result colour --------------------------------------------
    // Single fixed colour used for every analysis output across every mode:
    // FrequencyResponse trace, THD differential bars + %, AliasingDetection
    // residue trace, peak-alias HUD - anything that represents "departure
    // from input" or "what the analysis wants you to notice." Channel-
    // specific UI (pre/post traces, meter labels, classical-view THD bars)
    // uses preEffect / postEffect instead. Do not introduce per-mode
    // accent colours - see feedback-color-semantics memory.
    inline const juce::Colour analysis { 0xff9be15c };  // green-yellow

    // ---- Optional: dim variant used for inactive channels -------------------
    // For consistent "not routed / no data" rendering. Each consumer can opt
    // in or use its own dimming math.
    inline juce::Colour dim (juce::Colour c, float alpha = 0.5f) noexcept
    {
        return c.withAlpha (alpha);
    }
}
