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

juce::Colour IMDDisplay::currentViewColour() const noexcept
{
    switch (currentViewSource())
    {
        case IMDMeasurement::Source::Pre:  return WTColors::preEffect;
        case IMDMeasurement::Source::Post: return WTColors::postEffect;
        case IMDMeasurement::Source::Diff: return WTColors::analysis;
    }
    return WTColors::analysis;
}

IMDDisplay::DisplayFrame IMDDisplay::sampleProcessor() const
{
    DisplayFrame f;
    const auto& imd = processor.imdMeasurement;
    f.valid = imd.isValid();

    for (auto& srcArr : f.ratioDb)
        std::fill (srcArr.begin(), srcArr.end(), IMDMeasurement::kNoMeasurementDb);

    if (! f.valid) return f;

    f.imdPercent = imd.getTotalImdPercent();
    f.f1Hz       = imd.getF1Hz();
    f.f2Hz       = imd.getF2Hz();
    f.preF1Db    = imd.getF1Db (IMDMeasurement::Source::Pre);
    f.preF2Db    = imd.getF2Db (IMDMeasurement::Source::Pre);
    f.postF1Db   = imd.getF1Db (IMDMeasurement::Source::Post);
    f.postF2Db   = imd.getF2Db (IMDMeasurement::Source::Post);

    static constexpr IMDMeasurement::Source kSources[3] = {
        IMDMeasurement::Source::Diff,
        IMDMeasurement::Source::Pre,
        IMDMeasurement::Source::Post
    };

    for (int s = 0; s < 3; ++s)
        for (int p = 0; p < IMDMeasurement::kNumProducts; ++p)
            f.ratioDb[(size_t) s][(size_t) p] = imd.getProductRatioDb (kSources[s], p);

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

    if (isHolding && displayed.valid && current.valid)
    {
        for (size_t s = 0; s < displayed.ratioDb.size(); ++s)
        {
            auto& heldArr = displayed.ratioDb[s];
            const auto& curArr = current.ratioDb[s];
            for (size_t p = 0; p < heldArr.size(); ++p)
            {
                const float curVal = curArr[p];
                if (curVal <= IMDMeasurement::kNoMeasurementDb + 1.0f) continue;
                heldArr[p] = (heldArr[p] <= IMDMeasurement::kNoMeasurementDb + 1.0f)
                                ? curVal
                                : std::max (heldArr[p], curVal);
            }
        }

        displayed.imdPercent = std::max (displayed.imdPercent, current.imdPercent);
        displayed.f1Hz       = current.f1Hz;
        displayed.f2Hz       = current.f2Hz;
        displayed.preF1Db    = current.preF1Db;
        displayed.preF2Db    = current.preF2Db;
        displayed.postF1Db   = current.postF1Db;
        displayed.postF2Db   = current.postF2Db;
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

    if (! displayed.valid)
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

    const float imdPct = displayed.imdPercent;
    juce::String imdText;
    if (imdPct < 0.01f)         imdText = juce::String (imdPct, 4) + "%";
    else if (imdPct < 1.0f)     imdText = juce::String (imdPct, 3) + "%";
    else if (imdPct < 100.0f)   imdText = juce::String (imdPct, 2) + "%";
    else                        imdText = juce::String (imdPct, 1) + "%";

    g.setColour (WTColors::analysis);
    g.setFont (juce::FontOptions (sf (30.0f)));
    g.drawText (imdText + " IMD", topL, juce::Justification::centred, false);

    auto formatFreq = [] (float hz) -> juce::String
    {
        if      (hz < 1000.0f)  return juce::String ((int) std::round (hz)) + " Hz";
        else if (hz < 10000.0f) return juce::String (hz / 1000.0f, 2) + " kHz";
        else                    return juce::String (hz / 1000.0f, 1) + " kHz";
    };

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("f1 " + formatFreq (displayed.f1Hz) + "   f2 " + formatFreq (displayed.f2Hz),
                topR, juce::Justification::centred, false);

    drawProductBars (g, bounds.reduced (sx (24), sx (12)));
}

void IMDDisplay::drawProductBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto  src       = currentViewSource();
    const auto  barColour = currentViewColour();
    const int   srcIdx    = sourceIndex (src);
    const auto& ratios    = displayed.ratioDb[(size_t) srcIdx];

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

    if (isHzLayout())
        drawProductBarsByHz    (g, plotArea, labelGutterBottom, ratios, barColour);
    else
        drawProductBarsByOrder (g, plotArea, labelGutterBottom, ratios, barColour);
}

void IMDDisplay::drawProductBarsByOrder (juce::Graphics& g,
                                         juce::Rectangle<int> plotArea,
                                         juce::Rectangle<int> labelGutterBottom,
                                         const std::array<float, IMDMeasurement::kNumProducts>& ratios,
                                         juce::Colour barColour)
{
    constexpr float kMaxDb =   0.0f;
    constexpr float kMinDb = -100.0f;
    const float     dbRange = kMaxDb - kMinDb;

    const int numBars = IMDMeasurement::kNumProducts;

    const float availableWidth = (float) plotArea.getWidth();
    const float slotWidth      = availableWidth / (float) numBars;
    const int   barInset       = sx (4);
    const int   dbLabelHeight  = sx (12);

    for (int i = 0; i < numBars; ++i)
    {
        const float ratioDb = ratios[(size_t) i];

        const int slotLeft  = plotArea.getX() + juce::roundToInt (i * slotWidth);
        const int slotRight = plotArea.getX() + juce::roundToInt ((i + 1) * slotWidth);

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (9.0f)));
        juce::Rectangle<int> nameRect (slotLeft, labelGutterBottom.getY(),
                                       slotRight - slotLeft, labelGutterBottom.getHeight());
        g.drawText (processor.imdMeasurement.getProductLabel (i), nameRect,
                    juce::Justification::centredTop, false);

        if (ratioDb <= kMinDb + 0.5f
            || ratioDb <= IMDMeasurement::kNoMeasurementDb + 1.0f) continue;

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
        g.setFont (juce::FontOptions (sf (9.0f)));
        g.drawText (juce::String (ratioDb, 1), dbRect,
                    juce::Justification::centred, false);
    }
}

void IMDDisplay::drawProductBarsByHz (juce::Graphics& g,
                                      juce::Rectangle<int> plotArea,
                                      juce::Rectangle<int> labelGutterBottom,
                                      const std::array<float, IMDMeasurement::kNumProducts>& ratios,
                                      juce::Colour barColour)
{
    constexpr float kMaxDb     =   0.0f;
    constexpr float kMinDb     = -100.0f;
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
    const int barWidth      = sx (6);
    const int dbLabelHeight = sx (12);
    const int productLabelH = sx (10);

    for (int i = 0; i < numBars; ++i)
    {
        const float ratioDb = ratios[(size_t) i];
        const float hz      = processor.imdMeasurement.getProductHz (i);
        if (hz < kViewMinHz || hz > kViewMaxHz) continue;

        const int centerX = freqToX (hz);
        const int slotLeft  = centerX - sx (24);
        const int slotRight = centerX + sx (24);

        if (ratioDb <= kMinDb + 0.5f
            || ratioDb <= IMDMeasurement::kNoMeasurementDb + 1.0f) continue;

        const float t          = (juce::jlimit (kMinDb, kMaxDb, ratioDb) - kMinDb) / dbRange;
        const int   barHeight  = juce::roundToInt (t * (float) plotArea.getHeight());
        const int   barLeft    = centerX - barWidth / 2;
        const int   barTop     = plotArea.getBottom() - barHeight;

        juce::Rectangle<int> bar (barLeft, barTop, barWidth, barHeight);
        g.setColour (barColour);
        g.fillRect (bar);

        // dB value above the bar.
        const int dbLabelY = juce::jmax (plotArea.getY() + sx (1),
                                         barTop - dbLabelHeight - sx (1));
        juce::Rectangle<int> dbRect (slotLeft, dbLabelY, slotRight - slotLeft, dbLabelHeight);
        g.setColour (juce::Colours::whitesmoke);
        g.setFont (juce::FontOptions (sf (9.0f)));
        g.drawText (juce::String (ratioDb, 1), dbRect,
                    juce::Justification::centred, false);

        // Product formula just below the dB value. Closely-spaced products
        // will have overlapping labels - that's the inherent cost of
        // frequency-positioned bars; users can switch to By Order for an
        // uncluttered identification view.
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
