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
    // _R variants stay in the SAME colour family as their L sibling but
    // shifted to a clearly distinct hue within that family - reads as
    // "kin, not a stranger". The master (L) is the bolder of each pair
    // because layered traces draw L underneath R; making the master the
    // bolder colour keeps the channel identity readable:
    //   pre   warm red-orange     (L, master)  vs  amber  (R)  - warm family
    //   post  periwinkle / violet (L, master)  vs  cyan   (R)  - cool family
    //   analysis green            (L, master)  vs  chartreuse (R) - green family
    // See PLANNING.md 8.5.1.
    inline const juce::Colour preEffect    { 0xffe85838 };  // warm red-orange    (L, master)
    inline const juce::Colour preEffect_R  { 0xffe0a050 };  // amber              (R)
    inline const juce::Colour postEffect   { 0xff7c7ce8 };  // periwinkle violet  (L, master)
    inline const juce::Colour postEffect_R { 0xff5cd4e8 };  // cyan               (R)

    // ---- Analysis result colour --------------------------------------------
    // Single fixed colour used for every analysis output across every mode:
    // FrequencyResponse trace, THD differential bars + %, AliasingDetection
    // residue trace, peak-alias HUD - anything that represents "departure
    // from input" or "what the analysis wants you to notice." Channel-
    // specific UI (pre/post traces, meter labels, classical-view THD bars)
    // uses preEffect / postEffect instead. Do not introduce per-mode
    // accent colours - see feedback-color-semantics memory.
    // _R variant follows the same L/R hue-contrast pattern as the channel
    // colours for when analyses go stereo. Master is the bolder of the
    // pair (chartreuse) for the same readability reason as preEffect /
    // postEffect: layered traces draw L under R, so master = bolder keeps
    // the channel identity visible.
    inline const juce::Colour analysis   { 0xffd0ff30 };  // chartreuse   (L, master)
    inline const juce::Colour analysis_R { 0xff9be15c };  // green-yellow (R)

    // ---- Optional: dim variant used for inactive channels -------------------
    // For consistent "not routed / no data" rendering. Each consumer can opt
    // in or use its own dimming math.
    inline juce::Colour dim (juce::Colour c, float alpha = 0.5f) noexcept
    {
        return c.withAlpha (alpha);
    }
}
