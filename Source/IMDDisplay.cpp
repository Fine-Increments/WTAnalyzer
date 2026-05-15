/*
  ==============================================================================

    IMDDisplay.cpp

  ==============================================================================
*/

#include "IMDDisplay.h"
#include "Colors.h"

#include <algorithm>
#include <cmath>

IMDDisplay::IMDDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureViewButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (3, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter ("imdBarsView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureViewButton (diffButton, 0);
    configureViewButton (preButton,  1);
    configureViewButton (postButton, 2);

    const juce::Colour engagedFill (0xffcfd2d6);

    auto setEngagedHighlight = [&engagedFill] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonOnColourId, engagedFill);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    };

    addAndMakeVisible (holdButton);
    holdButton.setClickingTogglesState (true);
    setEngagedHighlight (holdButton);
    holdButton.onClick = [this] { isHolding = holdButton.getToggleState(); };

    addAndMakeVisible (freezeButton);
    freezeButton.setClickingTogglesState (true);
    setEngagedHighlight (freezeButton);
    freezeButton.onClick = [this] { isFrozen = freezeButton.getToggleState(); };

    // Layout toggle: "By Order" / "By Hz". The button's own toggle state
    // is the source of truth (via APVTS ButtonAttachment); paint() reads
    // it through isHzLayout(). Label flips on each click via onClick so
    // the user can see what mode they're in.
    addAndMakeVisible (layoutButton);
    layoutButton.setClickingTogglesState (true);
    layoutButton.onClick = [this] { updateLayoutButtonText(); repaint(); };
    layoutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "imdHzLayout", layoutButton);
    updateLayoutButtonText();

    processor.apvts.addParameterListener ("imdBarsView", this);
    syncToggleButtons();

    startTimerHz (30);
}

void IMDDisplay::updateLayoutButtonText()
{
    layoutButton.setButtonText (layoutButton.getToggleState() ? "By Hz" : "By Order");
}

IMDDisplay::~IMDDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("imdBarsView", this);
}

void IMDDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void IMDDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "imdBarsView")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncToggleButtons();
            repaint();
        });
    }
}

void IMDDisplay::syncToggleButtons()
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("imdBarsView");
    diffButton.setToggleState (idx == 0, juce::dontSendNotification);
    preButton .setToggleState (idx == 1, juce::dontSendNotification);
    postButton.setToggleState (idx == 2, juce::dontSendNotification);
}

int IMDDisplay::sourceIndex (IMDMeasurement::Source s) noexcept
{
    switch (s)
    {
        case IMDMeasurement::Source::Diff: return 0;
        case IMDMeasurement::Source::Pre:  return 1;
        case IMDMeasurement::Source::Post: return 2;
    }
    return 0;
}

IMDMeasurement::Source IMDDisplay::currentViewSource() const noexcept
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("imdBarsView");
    switch (idx)
    {
        case 1:  return IMDMeasurement::Source::Pre;
        case 2:  return IMDMeasurement::Source::Post;
        default: return IMDMeasurement::Source::Diff;
    }
}

IMDDisplay::ViewColours IMDDisplay::currentViewColours() const noexcept
{
    switch (currentViewSource())
    {
        case IMDMeasurement::Source::Pre:  return { WTColors::preEffect,  WTColors::preEffect_R  };
        case IMDMeasurement::Source::Post: return { WTColors::postEffect, WTColors::postEffect_R };
        case IMDMeasurement::Source::Diff: return { WTColors::analysis,   WTColors::analysis_R   };
    }
    return { WTColors::analysis, WTColors::analysis_R };
}

IMDDisplay::DisplayFrame IMDDisplay::sampleProcessor() const
{
    DisplayFrame f;
    const auto& imd = processor.imdMeasurement;

    auto fillFromChannel = [&] (ChannelFrame& cf, IMDMeasurement::Channel chSel)
    {
        for (auto& srcArr : cf.ratioDb)
            std::fill (srcArr.begin(), srcArr.end(), IMDMeasurement::kNoMeasurementDb);

        cf.valid = imd.isValid (chSel);
        if (! cf.valid) return;

        cf.imdPercent = imd.getTotalImdPercent (chSel);
        cf.f1Hz       = imd.getF1Hz (chSel);
        cf.f2Hz       = imd.getF2Hz (chSel);
        cf.preF1Db    = imd.getF1Db (IMDMeasurement::Source::Pre,  chSel);
        cf.preF2Db    = imd.getF2Db (IMDMeasurement::Source::Pre,  chSel);
        cf.postF1Db   = imd.getF1Db (IMDMeasurement::Source::Post, chSel);
        cf.postF2Db   = imd.getF2Db (IMDMeasurement::Source::Post, chSel);

        for (int p = 0; p < IMDMeasurement::kNumProducts; ++p)
            cf.productHz[(size_t) p] = imd.getProductHz (p, chSel);

        static constexpr IMDMeasurement::Source kSources[3] = {
            IMDMeasurement::Source::Diff,
            IMDMeasurement::Source::Pre,
            IMDMeasurement::Source::Post
        };

        for (int s = 0; s < 3; ++s)
            for (int p = 0; p < IMDMeasurement::kNumProducts; ++p)
                cf.ratioDb[(size_t) s][(size_t) p]
                    = imd.getProductRatioDb (kSources[s], p, chSel);
    };

    fillFromChannel (f.L, IMDMeasurement::Channel::L);
    fillFromChannel (f.R, IMDMeasurement::Channel::R);
    return f;
}

void IMDDisplay::timerCallback()
{
    if (isFrozen)
    {
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
            for (size_t p = 0; p < heldArr.size(); ++p)
            {
                const float curVal = curArr[p];
                if (curVal <= IMDMeasurement::kNoMeasurementDb + 1.0f) continue;
                heldArr[p] = (heldArr[p] <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                                ? curVal
                                : std::max (heldArr[p], curVal);
            }
        }
        held.imdPercent = std::max (held.imdPercent, cur.imdPercent);
        held.f1Hz       = cur.f1Hz;
        held.f2Hz       = cur.f2Hz;
        held.preF1Db    = cur.preF1Db;
        held.preF2Db    = cur.preF2Db;
        held.postF1Db   = cur.postF1Db;
        held.postF2Db   = cur.postF2Db;
        held.productHz  = cur.productHz;
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

void IMDDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header   = bounds.removeFromTop (sx (72));
    auto topRow   = header.removeFromTop (sx (46));
    auto botRow   = header;

    auto topL = topRow.removeFromLeft (topRow.getWidth() / 2);
    auto topR = topRow;
    juce::ignoreUnused (topL, topR);

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

    layoutRow (botL, { &holdButton, &freezeButton, &layoutButton }, sx (60), sx (22), sx (6));
    layoutRow (botR, { &diffButton, &preButton,   &postButton   }, sx (56), sx (22), sx (6));
}

void IMDDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    if (! displayed.anyValid())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (14.0f)));
        g.drawText ("Play two distinct sine tones to measure IMD",
                    bounds, juce::Justification::centred, false);
        return;
    }

    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (46));

    auto topL = topRow.removeFromLeft (topRow.getWidth() / 2);
    auto topR = topRow;

    auto formatPct = [] (float p) -> juce::String
    {
        if (p < 0.01f)        return juce::String (p, 4) + "%";
        if (p < 1.0f)         return juce::String (p, 3) + "%";
        if (p < 100.0f)       return juce::String (p, 2) + "%";
        return                       juce::String (p, 1) + "%";
    };

    auto topLTop = topL.withHeight (topL.getHeight() / 2);
    auto topLBot = topL.withTrimmedTop (topL.getHeight() / 2);

    g.setFont (juce::FontOptions (sf (18.0f)));
    if (displayed.L.valid)
    {
        g.setColour (WTColors::analysis);
        g.drawText (formatPct (displayed.L.imdPercent) + " L  IMD",
                    topLTop, juce::Justification::centred, false);
    }
    if (displayed.R.valid)
    {
        g.setColour (WTColors::analysis_R);
        g.drawText (formatPct (displayed.R.imdPercent) + " R  IMD",
                    topLBot, juce::Justification::centred, false);
    }

    auto formatFreq = [] (float hz) -> juce::String
    {
        if      (hz < 1000.0f)  return juce::String ((int) std::round (hz)) + " Hz";
        else if (hz < 10000.0f) return juce::String (hz / 1000.0f, 2) + " kHz";
        else                    return juce::String (hz / 1000.0f, 1) + " kHz";
    };

    const auto& primary = displayed.L.valid ? displayed.L : displayed.R;
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("f1 " + formatFreq (primary.f1Hz) + "   f2 " + formatFreq (primary.f2Hz),
                topR, juce::Justification::centred, false);

    drawProductBars (g, bounds.reduced (sx (24), sx (12)));
}

void IMDDisplay::drawProductBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto src    = currentViewSource();
    const int  srcIdx = sourceIndex (src);
    const auto cols   = currentViewColours();

    const bool showL    = *processor.apvts.getRawParameterValue ("showChannelL")    > 0.5f;
    const bool showR    = *processor.apvts.getRawParameterValue ("showChannelR")    > 0.5f;
    const bool showDiff = *processor.apvts.getRawParameterValue ("showChannelDiff") > 0.5f;
    const bool diffMode = showDiff;

    juce::Array<SubBar> visible;
    if (! diffMode)
    {
        if (showL) visible.add ({ 'L', cols.L });
        if (showR) visible.add ({ 'R', cols.R });
    }

    // Y axis range. In L/R view: normal -100..0 dB. In Diff view: bipolar
    // -30..+30 centred at zero so the single signed bar can grow either way.
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

    g.setColour (juce::Colour (0xff2a2d32));
    const std::array<float, 5> kDbGridNormal { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f };
    const std::array<float, 5> kDbGridDiff   { -30.0f, -15.0f, 0.0f, 15.0f, 30.0f };
    const auto& dbGrid = diffMode ? kDbGridDiff : kDbGridNormal;
    for (float db : dbGrid)
    {
        const float t = (db - axisMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());
        g.drawLine ((float) plotArea.getX(), (float) y, (float) plotArea.getRight(), (float) y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (float db : dbGrid)
    {
        const float t = (db - axisMinDb) / dbRange;
        const int   y = plotArea.getY() + juce::roundToInt ((1.0f - t) * (float) plotArea.getHeight());

        const int textHeight = sx (12);
        juce::String text;
        if (db == 0.0f)     text = "0";
        else if (db > 0.0f) text = "+" + juce::String ((int) db);
        else                text = juce::String ((int) db);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4), textHeight);
        g.drawText (text, r, juce::Justification::centredRight, false);
    }

    if (! diffMode && visible.isEmpty()) return;

    BarAxisConfig axis { diffMode, axisMinDb, axisMaxDb };

    if (isHzLayout())
        drawProductBarsByHz    (g, plotArea, labelGutterBottom, srcIdx, visible, cols, axis);
    else
        drawProductBarsByOrder (g, plotArea, labelGutterBottom, srcIdx, visible, cols, axis);
}

void IMDDisplay::drawProductBarsByOrder (juce::Graphics& g,
                                         juce::Rectangle<int> plotArea,
                                         juce::Rectangle<int> labelGutterBottom,
                                         int sourceIdx,
                                         const juce::Array<SubBar>& visible,
                                         ViewColours cols,
                                         BarAxisConfig axis)
{
    const float kMaxDb  = axis.axisMaxDb;
    const float kMinDb  = axis.axisMinDb;
    const float dbRange = kMaxDb - kMinDb;

    const int numBars = IMDMeasurement::kNumProducts;

    const float availableWidth = (float) plotArea.getWidth();
    const float slotWidth      = availableWidth / (float) numBars;
    const int   slotInset      = sx (3);
    const int   dbLabelHeight  = sx (12);

    auto yForDb = [&] (float db) -> int
    {
        return plotArea.getY()
             + juce::roundToInt ((1.0f - (db - kMinDb) / dbRange) * (float) plotArea.getHeight());
    };

    // ----- Diff mode: one bipolar bar per product ------------------------
    if (axis.diffMode)
    {
        const int zeroY = yForDb (0.0f);

        for (int i = 0; i < numBars; ++i)
        {
            const int slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
            const int slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (sf (9.0f)));
            juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                           slotRight - slotLeft, labelGutterBottom.getHeight());
            g.drawText (processor.imdMeasurement.getProductLabel (i), nameRect,
                        juce::Justification::centredTop, false);

            const float l = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) i];
            const float r = displayed.R.ratioDb[(size_t) sourceIdx][(size_t) i];
            if (l <= IMDMeasurement::kNoMeasurementDb + 1.0f
             || r <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                continue;

            const float v = r - l;
            const int   vY      = yForDb (juce::jlimit (kMinDb, kMaxDb, v));
            const int   barTop  = juce::jmin (zeroY, vY);
            const int   barBot  = juce::jmax (zeroY, vY);
            const int   barLeft = slotLeft  + slotInset;
            const int   barRight = slotRight - slotInset;

            g.setColour (v >= 0.0f ? cols.R : cols.L);
            g.fillRect (juce::Rectangle<int> (barLeft, barTop,
                                              juce::jmax (1, barRight - barLeft),
                                              juce::jmax (1, barBot - barTop)));

            const int labelY = (v >= 0.0f)
                ? juce::jmax (plotArea.getY() + sx (1), vY - dbLabelHeight - sx (1))
                : juce::jmin (plotArea.getBottom() - dbLabelHeight, vY + sx (1));
            juce::Rectangle<int> dbRect (slotLeft, labelY,
                                         slotRight - slotLeft, dbLabelHeight);
            g.setColour (juce::Colours::whitesmoke);
            g.setFont (juce::FontOptions (sf (9.0f)));
            g.drawText ((v >= 0.0f ? "+" : "") + juce::String (v, 1), dbRect,
                        juce::Justification::centred, false);
        }
        return;
    }

    auto valueFor = [&] (char tag, int productIdx) -> float
    {
        const float l = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) productIdx];
        const float r = displayed.R.ratioDb[(size_t) sourceIdx][(size_t) productIdx];
        switch (tag)
        {
            case 'L': return l;
            case 'R': return r;
        }
        return IMDMeasurement::kNoMeasurementDb;
    };

    for (int i = 0; i < numBars; ++i)
    {
        const int slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
        const int slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (9.0f)));
        juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                       slotRight - slotLeft, labelGutterBottom.getHeight());
        g.drawText (processor.imdMeasurement.getProductLabel (i), nameRect,
                    juce::Justification::centredTop, false);

        const int innerLeft  = slotLeft  + slotInset;
        const int innerRight = slotRight - slotInset;
        const int innerWidth = juce::jmax (1, innerRight - innerLeft);
        const int subWidth   = innerWidth / visible.size();

        float maxV = IMDMeasurement::kNoMeasurementDb;
        for (auto& s : visible)
        {
            const float v = valueFor (s.tag, i);
            if (v > maxV) maxV = v;
        }

        for (int sIdx = 0; sIdx < visible.size(); ++sIdx)
        {
            const auto& sb = visible.getReference (sIdx);
            const float v  = valueFor (sb.tag, i);
            if (v <= kMinDb + 0.5f || v <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                continue;

            const float t         = (juce::jlimit (kMinDb, kMaxDb, v) - kMinDb) / dbRange;
            const int   barHeight = juce::roundToInt (t * (float) plotArea.getHeight());

            const int barLeft  = innerLeft + sIdx * subWidth + 1;
            const int barRight = innerLeft + (sIdx + 1) * subWidth - 1;
            const int barTop   = plotArea.getBottom() - barHeight;

            g.setColour (sb.colour);
            g.fillRect (juce::Rectangle<int> (barLeft, barTop,
                                              juce::jmax (1, barRight - barLeft),
                                              barHeight));
        }

        if (maxV > IMDMeasurement::kNoMeasurementDb + 1.0f)
        {
            // Master (L) value labels the slot when available; falls back
            // to the first visible sub-bar otherwise.
            float labelDb = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) i];
            if (labelDb <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                labelDb = valueFor (visible.getReference (0).tag, i);

            const float tMax       = (juce::jlimit (kMinDb, kMaxDb, maxV) - kMinDb) / dbRange;
            const int   tallestTop = plotArea.getBottom() - juce::roundToInt (tMax * (float) plotArea.getHeight());

            const int labelY = juce::jmax (plotArea.getY() + sx (1),
                                           tallestTop - dbLabelHeight - sx (1));
            juce::Rectangle<int> dbRect (slotLeft, labelY,
                                         slotRight - slotLeft, dbLabelHeight);

            g.setColour (juce::Colours::whitesmoke);
            g.setFont (juce::FontOptions (sf (9.0f)));
            g.drawText (juce::String (labelDb, 1), dbRect,
                        juce::Justification::centred, false);
        }
    }
}

void IMDDisplay::drawProductBarsByHz (juce::Graphics& g,
                                      juce::Rectangle<int> plotArea,
                                      juce::Rectangle<int> labelGutterBottom,
                                      int sourceIdx,
                                      const juce::Array<SubBar>& visible,
                                      ViewColours cols,
                                      BarAxisConfig axis)
{
    const float kMaxDb     = axis.axisMaxDb;
    const float kMinDb     = axis.axisMinDb;
    constexpr float kViewMinHz =    20.0f;
    constexpr float kViewMaxHz = 20000.0f;
    const float     dbRange    = kMaxDb - kMinDb;
    const float     logMin     = std::log10 (kViewMinHz);
    const float     logMax     = std::log10 (kViewMaxHz);
    const float     logRange   = logMax - logMin;

    auto freqToX = [&] (float freq) -> int
    {
        const float clampedFreq = juce::jlimit (kViewMinHz, kViewMaxHz, freq);
        const float t = (std::log10 (clampedFreq) - logMin) / logRange;
        return plotArea.getX() + juce::roundToInt (t * (float) plotArea.getWidth());
    };

    auto yForDb = [&] (float db) -> int
    {
        return plotArea.getY()
             + juce::roundToInt ((1.0f - (db - kMinDb) / dbRange) * (float) plotArea.getHeight());
    };

    // Frequency axis labels along the bottom gutter (same anchor freqs
    // as the spectrum view so they read consistently across modes).
    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 10> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,   "100" },
        { 200.0f,   "200" }, { 500.0f,   "500"  }, { 1000.0f,  "1k"  },
        { 2000.0f,  "2k"  }, { 5000.0f,  "5k"   }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    }};

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (9.0f)));
    for (auto label : kFreqLabels)
    {
        if (label.hz < kViewMinHz || label.hz > kViewMaxHz) continue;
        const int x = freqToX (label.hz);
        const int textWidth = sx (28);
        juce::Rectangle<int> r (x - textWidth / 2,
                                labelGutterBottom.getY(),
                                textWidth,
                                labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    const int numBars       = IMDMeasurement::kNumProducts;
    const int dbLabelHeight = sx (12);
    const int productLabelH = sx (10);

    // ----- Diff mode: single bipolar bar per product at its Hz position --
    if (axis.diffMode)
    {
        const int zeroY        = yForDb (0.0f);
        const int diffBarWidth = sx (8);

        for (int i = 0; i < numBars; ++i)
        {
            const float hz = displayed.L.valid
                                ? displayed.L.productHz[(size_t) i]
                                : displayed.R.productHz[(size_t) i];
            if (hz < kViewMinHz || hz > kViewMaxHz) continue;

            const float l = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) i];
            const float r = displayed.R.ratioDb[(size_t) sourceIdx][(size_t) i];
            if (l <= IMDMeasurement::kNoMeasurementDb + 1.0f
             || r <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                continue;

            const float v       = r - l;
            const int   centerX = freqToX (hz);
            const int   slotLeft  = centerX - sx (24);
            const int   slotRight = centerX + sx (24);
            const int   vY      = yForDb (juce::jlimit (kMinDb, kMaxDb, v));
            const int   barTop  = juce::jmin (zeroY, vY);
            const int   barBot  = juce::jmax (zeroY, vY);
            const int   barLeft = centerX - diffBarWidth / 2;

            g.setColour (v >= 0.0f ? cols.R : cols.L);
            g.fillRect (juce::Rectangle<int> (barLeft, barTop,
                                              diffBarWidth,
                                              juce::jmax (1, barBot - barTop)));

            const int labelY = (v >= 0.0f)
                ? juce::jmax (plotArea.getY() + sx (1), vY - dbLabelHeight - sx (1))
                : juce::jmin (plotArea.getBottom() - dbLabelHeight, vY + sx (1));
            juce::Rectangle<int> dbRect (slotLeft, labelY, slotRight - slotLeft, dbLabelHeight);
            g.setColour (juce::Colours::whitesmoke);
            g.setFont (juce::FontOptions (sf (9.0f)));
            g.drawText ((v >= 0.0f ? "+" : "") + juce::String (v, 1), dbRect,
                        juce::Justification::centred, false);

            const int productLabelY = juce::jmax (plotArea.getY() + sx (1),
                                                  labelY - productLabelH);
            juce::Rectangle<int> productRect (slotLeft, productLabelY,
                                              slotRight - slotLeft, productLabelH);
            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (sf (8.0f)));
            g.drawText (processor.imdMeasurement.getProductLabel (i), productRect,
                        juce::Justification::centred, false);
        }
        return;
    }

    const int subBarWidth   = juce::jmax (2, sx (6) / juce::jmax (1, visible.size()));
    const int subBarGap     = 1;

    auto valueFor = [&] (char tag, int productIdx) -> float
    {
        const float l = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) productIdx];
        const float r = displayed.R.ratioDb[(size_t) sourceIdx][(size_t) productIdx];
        switch (tag)
        {
            case 'L': return l;
            case 'R': return r;
        }
        return IMDMeasurement::kNoMeasurementDb;
    };

    for (int i = 0; i < numBars; ++i)
    {
        // Use L's product Hz when available; fall back to R if L isn't valid.
        const float hz = displayed.L.valid
                            ? displayed.L.productHz[(size_t) i]
                            : displayed.R.productHz[(size_t) i];
        if (hz < kViewMinHz || hz > kViewMaxHz) continue;

        const int centerX   = freqToX (hz);
        const int slotLeft  = centerX - sx (24);
        const int slotRight = centerX + sx (24);

        const int totalWidth = visible.size() * subBarWidth
                             + juce::jmax (0, visible.size() - 1) * subBarGap;
        int subX = centerX - totalWidth / 2;

        float maxV = IMDMeasurement::kNoMeasurementDb;
        for (auto& s : visible)
        {
            const float v = valueFor (s.tag, i);
            if (v > maxV) maxV = v;
        }

        for (int sIdx = 0; sIdx < visible.size(); ++sIdx)
        {
            const auto& sb = visible.getReference (sIdx);
            const float v  = valueFor (sb.tag, i);
            if (v <= kMinDb + 0.5f || v <= IMDMeasurement::kNoMeasurementDb + 1.0f)
            {
                subX += subBarWidth + subBarGap;
                continue;
            }

            const float t         = (juce::jlimit (kMinDb, kMaxDb, v) - kMinDb) / dbRange;
            const int   barHeight = juce::roundToInt (t * (float) plotArea.getHeight());
            const int   barTop    = plotArea.getBottom() - barHeight;

            g.setColour (sb.colour);
            g.fillRect (juce::Rectangle<int> (subX, barTop, subBarWidth, barHeight));

            subX += subBarWidth + subBarGap;
        }

        if (maxV <= IMDMeasurement::kNoMeasurementDb + 1.0f) continue;

        float labelDb = displayed.L.ratioDb[(size_t) sourceIdx][(size_t) i];
        if (labelDb <= IMDMeasurement::kNoMeasurementDb + 1.0f)
            labelDb = valueFor (visible.getReference (0).tag, i);

        const float tMax       = (juce::jlimit (kMinDb, kMaxDb, maxV) - kMinDb) / dbRange;
        const int   tallestTop = plotArea.getBottom() - juce::roundToInt (tMax * (float) plotArea.getHeight());

        const int dbLabelY = juce::jmax (plotArea.getY() + sx (1),
                                         tallestTop - dbLabelHeight - sx (1));
        juce::Rectangle<int> dbRect (slotLeft, dbLabelY, slotRight - slotLeft, dbLabelHeight);
        g.setColour (juce::Colours::whitesmoke);
        g.setFont (juce::FontOptions (sf (9.0f)));
        g.drawText (juce::String (labelDb, 1), dbRect,
                    juce::Justification::centred, false);

        const int productLabelY = juce::jmax (plotArea.getY() + sx (1),
                                              dbLabelY - productLabelH);
        juce::Rectangle<int> productRect (slotLeft, productLabelY,
                                          slotRight - slotLeft, productLabelH);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (8.0f)));
        g.drawText (processor.imdMeasurement.getProductLabel (i), productRect,
                    juce::Justification::centred, false);
    }
}
