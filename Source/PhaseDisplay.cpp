/*
  ==============================================================================

    PhaseDisplay.cpp

  ==============================================================================
*/

#include "PhaseDisplay.h"
#include "Colors.h"
#include "Analyses/PhaseResponse.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr float kViewMinHz = 20.0f;

    // Anything above this is a real measurement; PhaseResponse::kNoMeasurement
    // (-1e9) sits well below it.
    constexpr float kValidFloor = -1.0e8f;

    // Rounds a positive value up to the next "nice" axis maximum from the
    // 1 / 2 / 5 sequence.
    float niceCeil (float v)
    {
        if (v <= 0.0f) return 1.0f;
        const float e    = std::floor (std::log10 (v));
        const float base = std::pow (10.0f, e);
        const float frac = v / base;
        float niceFrac;
        if      (frac <= 1.0f) niceFrac = 1.0f;
        else if (frac <= 2.0f) niceFrac = 2.0f;
        else if (frac <= 5.0f) niceFrac = 5.0f;
        else                   niceFrac = 10.0f;
        return niceFrac * base;
    }
}

PhaseDisplay::PhaseDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

    auto configureButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter ("phaseView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureButton (phaseButton,      0);
    configureButton (groupDelayButton, 1);

    processor.apvts.addParameterListener ("phaseView", this);
    syncViewButtons();

    startTimerHz (30);
}

PhaseDisplay::~PhaseDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("phaseView", this);
}

void PhaseDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void PhaseDisplay::timerCallback()
{
    repaint();
}

void PhaseDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "phaseView")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncViewButtons();
            repaint();
        });
    }
}

void PhaseDisplay::syncViewButtons()
{
    const int idx = (int) *processor.apvts.getRawParameterValue ("phaseView");
    phaseButton     .setToggleState (idx == 0, juce::dontSendNotification);
    groupDelayButton.setToggleState (idx == 1, juce::dontSendNotification);
}

PhaseDisplay::View PhaseDisplay::currentView() const noexcept
{
    return ((int) *processor.apvts.getRawParameterValue ("phaseView") == 1)
               ? View::GroupDelay : View::Phase;
}

void PhaseDisplay::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (sx (36));

    const int buttonW = sx (96);
    const int buttonH = sx (22);
    const int spacing = sx (6);
    const int totalW  = buttonW * 2 + spacing;
    const int startX  = header.getCentreX() - totalW / 2;
    const int buttonY = header.getCentreY() - buttonH / 2;

    phaseButton     .setBounds (startX,                     buttonY, buttonW, buttonH);
    groupDelayButton.setBounds (startX + buttonW + spacing, buttonY, buttonW, buttonH);
}

void PhaseDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    bounds.removeFromTop (sx (36));   // header band - owned by the view buttons
    drawCurve (g, bounds.reduced (sx (8), sx (8)));
}

void PhaseDisplay::drawCurve (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft   (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (16));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const float sr        = processor.currentSampleRate.load (std::memory_order_relaxed);
    const float nyquist   = sr * 0.5f;
    const float viewMaxHz = juce::jmax (kViewMinHz * 2.0f, nyquist);
    const float logMin    = std::log10 (kViewMinHz);
    const float logMax    = std::log10 (viewMaxHz);
    const float logRange  = logMax - logMin;

    auto freqToX = [&] (float hz) -> float
    {
        const float t = (std::log10 (juce::jlimit (kViewMinHz, viewMaxHz, hz)) - logMin) / logRange;
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };

    const bool gd = (currentView() == View::GroupDelay);
    using Ch = PhaseResponse::Channel;
    const auto& dataL = gd ? processor.phaseResponse.getGroupDelayMs (Ch::L)
                           : processor.phaseResponse.getPhaseDegrees (Ch::L);
    const auto& dataR = gd ? processor.phaseResponse.getGroupDelayMs (Ch::R)
                           : processor.phaseResponse.getPhaseDegrees (Ch::R);
    const int N = (int) dataL.size();

    // Shared L / R channel toggles (the editor enforces at least one on).
    const bool showL = *processor.apvts.getRawParameterValue ("showChannelL") > 0.5f;
    const bool showR = *processor.apvts.getRawParameterValue ("showChannelR") > 0.5f;

    // ---- Y range -------------------------------------------------------
    // Phase is fixed at +/-180 degrees; group delay auto-ranges in ms to
    // the visible channels only.
    float yMin = -180.0f, yMax = 180.0f;
    if (gd)
    {
        float dmin = 1.0e30f, dmax = -1.0e30f;
        for (int i = 0; i < N; ++i)
        {
            const float vl = dataL[(size_t) i];
            const float vr = dataR[(size_t) i];
            if (showL && vl > kValidFloor) { dmin = juce::jmin (dmin, vl); dmax = juce::jmax (dmax, vl); }
            if (showR && vr > kValidFloor) { dmin = juce::jmin (dmin, vr); dmax = juce::jmax (dmax, vr); }
        }
        if (dmax < dmin)        { yMin = -1.0f; yMax = 1.0f; }
        else if (dmin >= 0.0f)  { yMin =  0.0f; yMax = niceCeil (juce::jmax (dmax, 1.0e-4f)); }
        else
        {
            yMax = niceCeil (juce::jmax (std::abs (dmin), std::abs (dmax)));
            yMin = -yMax;
        }
    }
    const float yRange = juce::jmax (1.0e-6f, yMax - yMin);

    auto valToY = [&] (float v) -> float
    {
        const float t = (juce::jlimit (yMin, yMax, v) - yMin) / yRange;
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    auto formatY = [&] (float v) -> juce::String
    {
        if (! gd) return juce::String (juce::roundToInt (v));
        if (std::abs (v) < 1.0e-4f) return "0";
        const int decimals = yRange < 1.0f ? 3 : (yRange < 10.0f ? 2 : 1);
        return juce::String (v, decimals);
    };

    // ---- Grid + axis labels -------------------------------------------
    g.setColour (juce::Colour (0xff2a2d32));
    for (int i = 0; i <= 4; ++i)
    {
        const float y = valToY (yMin + yRange * (float) i / 4.0f);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    for (int i = 0; i <= 4; ++i)
    {
        const float v = yMin + yRange * (float) i / 4.0f;
        const int   y = juce::roundToInt (valToY (v));
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (formatY (v), r, juce::Justification::centredRight, false);
    }

    g.drawText (gd ? "ms" : "deg",
                juce::Rectangle<int> (labelGutterLeft.getX(), plotArea.getY() - sx (2),
                                      labelGutterLeft.getWidth() - sx (4), sx (12)),
                juce::Justification::centredRight, false);

    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 10> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,   "100" },
        { 200.0f,   "200" }, { 500.0f,   "500"  }, { 1000.0f,  "1k"  },
        { 2000.0f,  "2k"  }, { 5000.0f,  "5k"   }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    }};
    for (auto label : kFreqLabels)
    {
        if (label.hz < kViewMinHz || label.hz > viewMaxHz) continue;
        const int x = juce::roundToInt (freqToX (label.hz));
        const int textWidth = sx (30);
        juce::Rectangle<int> r (x - textWidth / 2, labelGutterBottom.getY(),
                                textWidth, labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    // Zero reference line, drawn brighter than the grid when in range.
    if (yMin < 0.0f && yMax > 0.0f)
    {
        const float zeroY = valToY (0.0f);
        g.setColour (juce::Colour (0xff44484f));
        g.drawLine ((float) plotArea.getX(), zeroY, (float) plotArea.getRight(), zeroY, sf (1.0f));
    }

    // ---- Traces --------------------------------------------------------
    // The no-sidechain case is handled by the editor's shared
    // SidechainNotice overlay, consistently across every mode.
    if (N <= 0) return;

    const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

    auto strokeChannel = [&] (const std::vector<float>& data, juce::Colour colour)
    {
        juce::Path path;
        bool open = false;
        for (int bin = 1; bin < N; ++bin)
        {
            const float hz = (float) bin * binFreqScale;
            if (hz < kViewMinHz) continue;
            if (hz > viewMaxHz)  break;

            const float v = data[(size_t) bin];
            if (v <= kValidFloor) { open = false; continue; }

            const float x = freqToX (hz);
            const float y = valToY (v);
            if (! open) { path.startNewSubPath (x, y); open = true; }
            else        { path.lineTo          (x, y); }
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
    };

    if (showR) strokeChannel (dataR, WTColors::analysis_R);   // R drawn first
    if (showL) strokeChannel (dataL, WTColors::analysis);     // L on top
}
