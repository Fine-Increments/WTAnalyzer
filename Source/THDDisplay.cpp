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

juce::Colour THDDisplay::currentViewColour() const noexcept
{
    switch (currentViewSource())
    {
        case THDMeasurement::Source::Pre:  return WTColors::preEffect;
        case THDMeasurement::Source::Post: return WTColors::postEffect;
        case THDMeasurement::Source::Diff: return WTColors::frequencyResponse;
    }
    return WTColors::frequencyResponse;
}

THDDisplay::DisplayFrame THDDisplay::sampleProcessor() const
{
    DisplayFrame f;

    const auto& thd = processor.thdMeasurement;
    f.valid = thd.isValid();

    // Always seed all cells with kNoMeasurementDb so callers don't need a
    // "valid" probe per cell.
    for (auto& srcArr : f.ratioDb)
        std::fill (srcArr.begin(), srcArr.end(), THDMeasurement::kNoMeasurementDb);

    if (! f.valid) return f;

    f.thdPercent        = thd.getTotalThdPercent();
    f.fundamentalHz     = thd.getFundamentalHz();
    f.preFundamentalDb  = thd.getPreFundamentalDb();
    f.postFundamentalDb = thd.getPostFundamentalDb();
    f.numValidHarmonics = thd.getNumValidHarmonics();

    static constexpr THDMeasurement::Source kSources[3] = {
        THDMeasurement::Source::Diff,
        THDMeasurement::Source::Pre,
        THDMeasurement::Source::Post
    };

    for (int s = 0; s < 3; ++s)
        for (int h = 1; h <= THDMeasurement::kMaxHarmonics; ++h)
            f.ratioDb[(size_t) s][(size_t) (h - 1)]
                = thd.getHarmonicRatioDb (kSources[s], h);

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

    if (isHolding && displayed.valid && current.valid)
    {
        // Peak-hold per harmonic per source. Live reference values
        // (fundamental Hz / dB) track current so the readout stays accurate
        // even while bars hold their peaks.
        for (size_t s = 0; s < displayed.ratioDb.size(); ++s)
        {
            auto& heldArr = displayed.ratioDb[s];
            const auto& curArr = current.ratioDb[s];
            for (size_t h = 0; h < heldArr.size(); ++h)
            {
                const float curVal = curArr[h];
                if (curVal <= THDMeasurement::kNoMeasurementDb + 1.0f) continue;
                heldArr[h] = (heldArr[h] <= THDMeasurement::kNoMeasurementDb + 1.0f)
                                ? curVal
                                : std::max (heldArr[h], curVal);
            }
        }

        displayed.thdPercent        = std::max (displayed.thdPercent, current.thdPercent);
        displayed.numValidHarmonics = std::max (displayed.numValidHarmonics, current.numValidHarmonics);
        displayed.fundamentalHz     = current.fundamentalHz;
        displayed.preFundamentalDb  = current.preFundamentalDb;
        displayed.postFundamentalDb = current.postFundamentalDb;
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

    if (! displayed.valid)
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

    // ---- THD% (always differential) -----------------------------------------
    const float thdPct = displayed.thdPercent;
    juce::String thdText;
    if (thdPct < 0.01f)         thdText = juce::String (thdPct, 4) + "%";
    else if (thdPct < 1.0f)     thdText = juce::String (thdPct, 3) + "%";
    else if (thdPct < 100.0f)   thdText = juce::String (thdPct, 2) + "%";
    else                        thdText = juce::String (thdPct, 1) + "%";

    g.setColour (WTColors::frequencyResponse);
    g.setFont (juce::FontOptions (sf (30.0f)));
    g.drawText (thdText + " THD", topL, juce::Justification::centred, false);

    // ---- Fundamental subline ------------------------------------------------
    const float f0     = displayed.fundamentalHz;
    const auto  src    = currentViewSource();
    const float fundDb = (src == THDMeasurement::Source::Post)
                            ? displayed.postFundamentalDb
                            : displayed.preFundamentalDb;

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
    const auto  src       = currentViewSource();
    const auto  barColour = currentViewColour();
    const int   srcIdx    = sourceIndex (src);
    const auto& ratios    = displayed.ratioDb[(size_t) srcIdx];

    // Y axis: dB ratio relative to the fundamental. h1 sits at 0 dB at the
    // top, harmonics extend down by their attenuation. The Diff view uses
    // the same axis since added energy is also expressed as a dB ratio to
    // pre's fundamental.
    constexpr float kMaxDb =   0.0f;
    constexpr float kMinDb = -100.0f;
    const float     dbRange = kMaxDb - kMinDb;

    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (32));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    g.setColour (juce::Colour (0xff2a2d32));
    const std::array<float, 5> kDbGrid { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f };
    for (float db : kDbGrid)
    {
        const float t = (db - kMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());
        g.drawLine ((float) plotArea.getX(), (float) y, (float) plotArea.getRight(), (float) y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : kDbGrid)
    {
        const float t = (db - kMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());

        const int textHeight = sx (12);
        const juce::String text = (db == 0.0f) ? juce::String ("0") : juce::String ((int) db);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4), textHeight);
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    const int numBars = juce::jmin (displayed.numValidHarmonics,
                                    THDMeasurement::kMaxHarmonics);
    if (numBars <= 0) return;

    const float availableWidth = (float) plotArea.getWidth();
    const float slotWidth      = availableWidth / (float) numBars;
    const int   barInset       = sx (6);
    const int   dbLabelHeight  = sx (12);

    for (int i = 0; i < numBars; ++i)
    {
        const int   harmonicNum = i + 1;
        const float ratioDb     = ratios[(size_t) i];

        const int   slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
        const int   slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

        // Harmonic name label below the bar slot (h1, h2, ...). Drawn even
        // when the bar itself is below the floor so the user can see which
        // harmonics are "missing".
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (10.0f)));
        juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                       slotRight - slotLeft, labelGutterBottom.getHeight());
        g.drawText ("h" + juce::String (harmonicNum), nameRect,
                    juce::Justification::centredTop, false);

        if (ratioDb <= kMinDb + 0.5f
            || ratioDb <= THDMeasurement::kNoMeasurementDb + 1.0f) continue;

        const float t          = (juce::jlimit (kMinDb, kMaxDb, ratioDb) - kMinDb) / dbRange;
        const int   barHeight  = juce::roundToInt (t * (float) plotArea.getHeight());

        const int   barLeft    = slotLeft  + barInset;
        const int   barRight   = slotRight - barInset;
        const int   barTop     = plotArea.getBottom() - barHeight;

        juce::Rectangle<int> bar (barLeft, barTop, barRight - barLeft, barHeight);

        g.setColour (barColour);
        g.fillRect (bar);

        const int labelY = juce::jmax (plotArea.getY() + sx (1),
                                       barTop - dbLabelHeight - sx (1));
        juce::Rectangle<int> dbRect (slotLeft, labelY,
                                     slotRight - slotLeft, dbLabelHeight);

        g.setColour (juce::Colours::whitesmoke);
        g.setFont (juce::FontOptions (sf (10.0f)));
        g.drawText (juce::String (ratioDb, 1), dbRect,
                    juce::Justification::centred, false);
    }
}
