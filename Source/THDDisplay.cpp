/*
  ==============================================================================

    THDDisplay.cpp

  ==============================================================================
*/

#include "THDDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>

THDDisplay::THDDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureViewButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            // Reflect the click into the APVTS choice parameter. The
            // parameterChanged listener will re-sync all three buttons.
            if (auto* p = processor.apvts.getParameter ("thdBarsView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureViewButton (diffButton, 0);
    configureViewButton (preButton,  1);
    configureViewButton (postButton, 2);

    // Hold/Freeze use the same neutral engaged colour so engagement reads as
    // a simple "on/off" state, without implying any relation to the channel
    // or measurement-mode colours (Hold preserves whichever view is active,
    // not just the differential).
    const juce::Colour engagedFill (0xffcfd2d6);   // neutral light grey

    auto setEngagedHighlight = [&engagedFill] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, engagedFill);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    };

    addAndMakeVisible (holdButton);
    holdButton.setClickingTogglesState (true);
    setEngagedHighlight (holdButton);
    holdButton.onClick = [this]
    {
        isHolding = holdButton.getToggleState();
        // No explicit reset here; the live frame coming in on the next tick
        // becomes the initial peak. Toggling off then on re-arms the peak.
    };

    addAndMakeVisible (freezeButton);
    freezeButton.setClickingTogglesState (true);
    setEngagedHighlight (freezeButton);
    freezeButton.onClick = [this]
    {
        isFrozen = freezeButton.getToggleState();
    };

    processor.apvts.addParameterListener ("thdBarsView", this);
    syncToggleButtons();

    startTimerHz (30);
}

THDDisplay::~THDDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("thdBarsView", this);
}

void THDDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void THDDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "thdBarsView")
    {
        // Listener may fire on a non-message thread; bounce to the message
        // thread to touch components safely.
        juce::MessageManager::callAsync ([this]
        {
            syncToggleButtons();
            repaint();
        });
    }
}

void THDDisplay::syncToggleButtons()
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("thdBarsView");
    diffButton.setToggleState (idx == 0, juce::dontSendNotification);
    preButton .setToggleState (idx == 1, juce::dontSendNotification);
    postButton.setToggleState (idx == 2, juce::dontSendNotification);
}

int THDDisplay::sourceIndex (THDMeasurement::Source s) noexcept
{
    switch (s)
    {
        case THDMeasurement::Source::Diff: return 0;
        case THDMeasurement::Source::Pre:  return 1;
        case THDMeasurement::Source::Post: return 2;
    }
    return 0;
}

THDMeasurement::Source THDDisplay::currentViewSource() const noexcept
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("thdBarsView");
    switch (idx)
    {
        case 1:  return THDMeasurement::Source::Pre;
        case 2:  return THDMeasurement::Source::Post;
        default: return THDMeasurement::Source::Diff;
    }
}

THDDisplay::ViewColours THDDisplay::currentViewColours() const noexcept
{
    switch (currentViewSource())
    {
        case THDMeasurement::Source::Pre:  return { WTColors::preEffect,  WTColors::preEffect_R  };
        case THDMeasurement::Source::Post: return { WTColors::postEffect, WTColors::postEffect_R };
        case THDMeasurement::Source::Diff: return { WTColors::analysis,   WTColors::analysis_R   };
    }
    return { WTColors::analysis, WTColors::analysis_R };
}

THDDisplay::DisplayFrame THDDisplay::sampleProcessor() const
{
    DisplayFrame f;

    const auto& thd = processor.thdMeasurement;

    auto fillFromChannel = [&] (ChannelFrame& cf, THDMeasurement::Channel chSel)
    {
        // Seed every cell with kNoMeasurementDb so callers don't need a
        // per-cell "valid" probe.
        for (auto& srcArr : cf.ratioDb)
            std::fill (srcArr.begin(), srcArr.end(), THDMeasurement::kNoMeasurementDb);

        cf.valid = thd.isValid (chSel);
        if (! cf.valid) return;

        cf.thdPercent        = thd.getTotalThdPercent   (chSel);
        cf.fundamentalHz     = thd.getFundamentalHz     (chSel);
        cf.preFundamentalDb  = thd.getPreFundamentalDb  (chSel);
        cf.postFundamentalDb = thd.getPostFundamentalDb (chSel);
        cf.numValidHarmonics = thd.getNumValidHarmonics (chSel);

        static constexpr THDMeasurement::Source kSources[3] = {
            THDMeasurement::Source::Diff,
            THDMeasurement::Source::Pre,
            THDMeasurement::Source::Post
        };

        for (int s = 0; s < 3; ++s)
            for (int h = 1; h <= THDMeasurement::kMaxHarmonics; ++h)
                cf.ratioDb[(size_t) s][(size_t) (h - 1)]
                    = thd.getHarmonicRatioDb (kSources[s], h, chSel);
    };

    fillFromChannel (f.L, THDMeasurement::Channel::L);
    fillFromChannel (f.R, THDMeasurement::Channel::R);

    return f;
}

void THDDisplay::timerCallback()
{
    if (isFrozen)
    {
        // Underlying audio thread keeps measuring; we just don't refresh
        // what's on screen. A repaint here covers expose / invalidation.
        repaint();
        return;
    }

    DisplayFrame current = sampleProcessor();

    auto holdChannel = [] (ChannelFrame& held, const ChannelFrame& cur)
    {
        if (! (held.valid && cur.valid))
        {
            held = cur;
            return;
        }
        for (size_t s = 0; s < held.ratioDb.size(); ++s)
        {
            auto& heldArr = held.ratioDb[s];
            const auto& curArr = cur.ratioDb[s];
            for (size_t h = 0; h < heldArr.size(); ++h)
            {
                const float curVal = curArr[h];
                if (curVal <= THDMeasurement::kNoMeasurementDb + 1.0f) continue;
                heldArr[h] = (heldArr[h] <= THDMeasurement::kNoMeasurementDb + 1.0f)
                                ? curVal
                                : std::max (heldArr[h], curVal);
            }
        }
        held.thdPercent        = std::max (held.thdPercent, cur.thdPercent);
        held.numValidHarmonics = std::max (held.numValidHarmonics, cur.numValidHarmonics);
        held.fundamentalHz     = cur.fundamentalHz;
        held.preFundamentalDb  = cur.preFundamentalDb;
        held.postFundamentalDb = cur.postFundamentalDb;
    };

    if (isHolding)
    {
        holdChannel (displayed.L, current.L);
        holdChannel (displayed.R, current.R);
    }
    else
    {
        displayed = current;
    }

    repaint();
}

void THDDisplay::resized()
{
    auto bounds = getLocalBounds();

    // Header band: top row carries the big readouts (THD% on the left,
    // fundamental subline on the right). Bottom row carries the buttons
    // (Hold + Freeze on the left, Diff/Pre/Post on the right). Bars get
    // the rest of the panel.
    auto header   = bounds.removeFromTop (sx (72));
    auto topRow   = header.removeFromTop (sx (46));
    auto botRow   = header;

    auto topL = topRow.removeFromLeft (topRow.getWidth() / 2);
    auto topR = topRow;
    juce::ignoreUnused (topL, topR);   // paint() draws into the same rects

    auto botL = botRow.removeFromLeft (botRow.getWidth() / 2);
    auto botR = botRow;

    auto layoutRow = [this] (juce::Rectangle<int> row,
                             std::initializer_list<juce::TextButton*> buttons,
                             int buttonW, int buttonH, int spacing)
    {
        const int n      = (int) buttons.size();
        const int totalW = buttonW * n + spacing * juce::jmax (0, n - 1);
        const int startX = row.getCentreX() - totalW / 2;
        const int buttonY = row.getCentreY() - buttonH / 2;
        int i = 0;
        for (auto* b : buttons)
        {
            b->setBounds (startX + i * (buttonW + spacing), buttonY, buttonW, buttonH);
            ++i;
        }
    };

    layoutRow (botL, { &holdButton, &freezeButton },           sx (62), sx (22), sx (6));
    layoutRow (botR, { &diffButton, &preButton, &postButton }, sx (56), sx (22), sx (6));
}

void THDDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    if (! displayed.anyValid())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (14.0f)));
        g.drawText ("Play a single sine tone to measure THD",
                    bounds, juce::Justification::centred, false);
        return;
    }

    // Mirror the split done in resized() so text lands above the buttons.
    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (46));
    /* botRow is owned by the toggle buttons */

    auto topL = topRow.removeFromLeft (topRow.getWidth() / 2);
    auto topR = topRow;

    auto formatPct = [] (float p) -> juce::String
    {
        if (p < 0.01f)        return juce::String (p, 4) + "%";
        if (p < 1.0f)         return juce::String (p, 3) + "%";
        if (p < 100.0f)       return juce::String (p, 2) + "%";
        return                       juce::String (p, 1) + "%";
    };

    // ---- THD% header (per-channel readouts, always differential) ------------
    // Stacks L on top and R below. Each line uses its own master/variant
    // colour so the channel identity is unambiguous.
    auto topLTop = topL.withHeight (topL.getHeight() / 2);
    auto topLBot = topL.withTrimmedTop (topL.getHeight() / 2);

    g.setFont (juce::FontOptions (sf (18.0f)));
    if (displayed.L.valid)
    {
        g.setColour (WTColors::analysis);
        g.drawText (formatPct (displayed.L.thdPercent) + " L  THD",
                    topLTop, juce::Justification::centred, false);
    }
    if (displayed.R.valid)
    {
        g.setColour (WTColors::analysis_R);
        g.drawText (formatPct (displayed.R.thdPercent) + " R  THD",
                    topLBot, juce::Justification::centred, false);
    }

    // ---- Fundamental subline ------------------------------------------------
    const auto& primary = displayed.L.valid ? displayed.L : displayed.R;
    const float f0     = primary.fundamentalHz;
    const auto  src    = currentViewSource();
    const float fundDb = (src == THDMeasurement::Source::Post)
                            ? primary.postFundamentalDb
                            : primary.preFundamentalDb;

    juce::String freqStr;
    if      (f0 < 1000.0f)   freqStr = juce::String ((int) std::round (f0)) + " Hz";
    else if (f0 < 10000.0f)  freqStr = juce::String (f0 / 1000.0f, 2) + " kHz";
    else                     freqStr = juce::String (f0 / 1000.0f, 1) + " kHz";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("Fundamental " + freqStr + "   at " + juce::String (fundDb, 1) + " dB",
                topR, juce::Justification::centred, false);

    // ---- Bars area ----------------------------------------------------------
    drawHarmonicBars (g, bounds.reduced (sx (24), sx (12)));
}

void THDDisplay::drawHarmonicBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto  src    = currentViewSource();
    const auto  cols   = currentViewColours();
    const int   srcIdx = sourceIndex (src);

    const bool showL    = *processor.apvts.getRawParameterValue ("showChannelL")    > 0.5f;
    const bool showR    = *processor.apvts.getRawParameterValue ("showChannelR")    > 0.5f;
    const bool showDiff = *processor.apvts.getRawParameterValue ("showChannelDiff") > 0.5f;
    const bool diffMode = showDiff;

    // Y axis range. In L/R view: normal dB ratio scale (0 at top, harmonics
    // extend down). In Diff view: bipolar centred at zero so the R - L
    // difference can grow up (R louder) or down (L louder).
    constexpr float kMaxDb     =   0.0f;
    constexpr float kMinDb     = -100.0f;
    constexpr float kDiffMaxDb = +30.0f;
    constexpr float kDiffMinDb = -30.0f;
    const float     axisMaxDb  = diffMode ? kDiffMaxDb : kMaxDb;
    const float     axisMinDb  = diffMode ? kDiffMinDb : kMinDb;
    const float     dbRange    = axisMaxDb - axisMinDb;

    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (32));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    // Gridlines / Y-axis labels. In normal view: dB-down-from-zero scale
    // (0, -20, -40, ...). In Diff view: bipolar (-30, -15, 0, +15, +30).
    auto yForDb = [&] (float db) -> int
    {
        const float t = (db - axisMinDb) / dbRange;
        return plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());
    };

    g.setColour (juce::Colour (0xff2a2d32));
    const std::array<float, 5> kDbGridNormal { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f };
    const std::array<float, 5> kDbGridDiff   { -30.0f, -15.0f, 0.0f, 15.0f, 30.0f };
    const auto& dbGrid = diffMode ? kDbGridDiff : kDbGridNormal;
    for (float db : dbGrid)
    {
        const int y = yForDb (db);
        g.drawLine ((float) plotArea.getX(), (float) y, (float) plotArea.getRight(), (float) y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : dbGrid)
    {
        const int   y          = yForDb (db);
        const int   textHeight = sx (12);
        juce::String text;
        if (db == 0.0f)        text = "0";
        else if (db > 0.0f)    text = "+" + juce::String ((int) db);
        else                   text = juce::String ((int) db);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4), textHeight);
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    const int numBars = juce::jmax (
        juce::jmin (displayed.L.numValidHarmonics, THDMeasurement::kMaxHarmonics),
        juce::jmin (displayed.R.numValidHarmonics, THDMeasurement::kMaxHarmonics));
    if (numBars <= 0) return;

    const float availableWidth = (float) plotArea.getWidth();
    const float slotWidth      = availableWidth / (float) numBars;
    const int   slotInset      = sx (4);
    const int   dbLabelHeight  = sx (12);

    // ------------------------------------------------------------------
    // Diff view: one bipolar bar per harmonic, centred on the zero line.
    // Bar grows up from zero (R-variant colour) when R is louder, or down
    // (L-master colour) when L is louder. Both pre and post values drive
    // their own Diff if the view source is Pre or Post; the Diff source
    // is itself a R - L difference of added energy, so we just plot it.
    // ------------------------------------------------------------------
    if (diffMode)
    {
        const int zeroY = yForDb (0.0f);

        auto diffValueFor = [&] (int harmonicIdx) -> float
        {
            const float l = displayed.L.ratioDb[(size_t) srcIdx][(size_t) harmonicIdx];
            const float r = displayed.R.ratioDb[(size_t) srcIdx][(size_t) harmonicIdx];
            if (l <= THDMeasurement::kNoMeasurementDb + 1.0f
             || r <= THDMeasurement::kNoMeasurementDb + 1.0f)
                return THDMeasurement::kNoMeasurementDb;
            return r - l;
        };

        for (int i = 0; i < numBars; ++i)
        {
            const int harmonicNum = i + 1;
            const int slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
            const int slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (sf (10.0f)));
            juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                           slotRight - slotLeft, labelGutterBottom.getHeight());
            g.drawText ("h" + juce::String (harmonicNum), nameRect,
                        juce::Justification::centredTop, false);

            const float v = diffValueFor (i);
            if (v <= THDMeasurement::kNoMeasurementDb + 1.0f) continue;

            const float vClamped = juce::jlimit (axisMinDb, axisMaxDb, v);
            const int   vY       = yForDb (vClamped);

            const int barLeft  = slotLeft  + slotInset;
            const int barRight = slotRight - slotInset;
            const int barTop   = juce::jmin (zeroY, vY);
            const int barBot   = juce::jmax (zeroY, vY);

            g.setColour (v >= 0.0f ? cols.R : cols.L);
            g.fillRect (juce::Rectangle<int> (barLeft, barTop,
                                              juce::jmax (1, barRight - barLeft),
                                              juce::jmax (1, barBot - barTop)));

            // dB readout above the bar tip (or below if bar grows downward).
            const int labelY = (v >= 0.0f)
                ? juce::jmax (plotArea.getY() + sx (1), vY - dbLabelHeight - sx (1))
                : juce::jmin (plotArea.getBottom() - dbLabelHeight, vY + sx (1));
            juce::Rectangle<int> dbRect (slotLeft, labelY,
                                         slotRight - slotLeft, dbLabelHeight);
            g.setColour (juce::Colours::whitesmoke);
            g.drawText ((v >= 0.0f ? "+" : "") + juce::String (v, 1), dbRect,
                        juce::Justification::centred, false);
        }
        return;
    }

    // ------------------------------------------------------------------
    // Normal L / R view: paired sub-bars per harmonic.
    // ------------------------------------------------------------------
    struct SubBar { char tag; juce::Colour colour; };
    juce::Array<SubBar> visible;
    if (showL) visible.add ({ 'L', cols.L });
    if (showR) visible.add ({ 'R', cols.R });
    if (visible.isEmpty()) return;

    auto valueFor = [&] (char tag, int harmonicIdx) -> float
    {
        const float l = displayed.L.ratioDb[(size_t) srcIdx][(size_t) harmonicIdx];
        const float r = displayed.R.ratioDb[(size_t) srcIdx][(size_t) harmonicIdx];
        switch (tag)
        {
            case 'L': return l;
            case 'R': return r;
        }
        return THDMeasurement::kNoMeasurementDb;
    };

    for (int i = 0; i < numBars; ++i)
    {
        const int harmonicNum = i + 1;
        const int slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
        const int slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (10.0f)));
        juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                       slotRight - slotLeft, labelGutterBottom.getHeight());
        g.drawText ("h" + juce::String (harmonicNum), nameRect,
                    juce::Justification::centredTop, false);

        const int innerLeft  = slotLeft  + slotInset;
        const int innerRight = slotRight - slotInset;
        const int innerWidth = juce::jmax (1, innerRight - innerLeft);
        const int subWidth   = innerWidth / visible.size();

        // Master (L) value drives the dB label above each slot. Falls
        // back to whichever sub-bar is visible.
        float labelDb = THDMeasurement::kNoMeasurementDb;
        for (auto& s : visible)
        {
            const float v = valueFor (s.tag, i);
            if (v > THDMeasurement::kNoMeasurementDb + 1.0f
                && labelDb <= THDMeasurement::kNoMeasurementDb + 1.0f)
                labelDb = v;
            if (s.tag == 'L' && v > THDMeasurement::kNoMeasurementDb + 1.0f)
                labelDb = v;
        }

        for (int sIdx = 0; sIdx < visible.size(); ++sIdx)
        {
            const auto& sb = visible.getReference (sIdx);
            const float v  = valueFor (sb.tag, i);
            if (v <= axisMinDb + 0.5f || v <= THDMeasurement::kNoMeasurementDb + 1.0f)
                continue;

            const float t         = (juce::jlimit (axisMinDb, axisMaxDb, v) - axisMinDb) / dbRange;
            const int   barHeight = juce::roundToInt (t * (float) plotArea.getHeight());

            const int barLeft  = innerLeft + sIdx * subWidth + 1;
            const int barRight = innerLeft + (sIdx + 1) * subWidth - 1;
            const int barTop   = plotArea.getBottom() - barHeight;

            g.setColour (sb.colour);
            g.fillRect (juce::Rectangle<int> (barLeft, barTop,
                                              juce::jmax (1, barRight - barLeft),
                                              barHeight));
        }

        if (labelDb > THDMeasurement::kNoMeasurementDb + 1.0f)
        {
            float maxV = THDMeasurement::kNoMeasurementDb;
            for (auto& s : visible)
            {
                const float v = valueFor (s.tag, i);
                if (v > maxV) maxV = v;
            }
            const float tMax = (juce::jlimit (axisMinDb, axisMaxDb, maxV) - axisMinDb) / dbRange;
            const int   tallestTop = plotArea.getBottom() - juce::roundToInt (tMax * (float) plotArea.getHeight());

            const int labelY = juce::jmax (plotArea.getY() + sx (1),
                                           tallestTop - dbLabelHeight - sx (1));
            juce::Rectangle<int> dbRect (slotLeft, labelY,
                                         slotRight - slotLeft, dbLabelHeight);

            g.setColour (juce::Colours::whitesmoke);
            g.setFont (juce::FontOptions (sf (10.0f)));
            g.drawText (juce::String (labelDb, 1), dbRect,
                        juce::Justification::centred, false);
        }
    }
}
