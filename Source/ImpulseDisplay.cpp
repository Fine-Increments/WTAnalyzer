/*
  ==============================================================================

    ImpulseDisplay.cpp

  ==============================================================================
*/

#include "ImpulseDisplay.h"
#include "Colors.h"
#include "IRExport.h"

#include <algorithm>
#include <cmath>

ImpulseDisplay::ImpulseDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc),
      csdView   (proc)
{
    setOpaque (true);

    // Waveform / CSD Heatmap / CSD 3D view selector (radio group),
    // backed by the shared `irView` parameter.
    auto configureViewButton = [this] (juce::TextButton& b, int idx)
    {
        addAndMakeVisible (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (8, juce::dontSendNotification);
        b.onClick = [this, idx]
        {
            if (auto* p = processor.apvts.getParameter ("irView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
        };
    };
    configureViewButton (waveformButton,   0);
    configureViewButton (csdHeatmapButton, 1);
    configureViewButton (csd3DButton,      2);

    addChildComponent (csdView);
    syncViewButtons();

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        processor.impulseResponse.requestClear();   // deferred to the audio thread
        repaint();
    };

    addAndMakeVisible (exportButton);
    exportButton.onClick = [this] { exportIRToWav(); };

    auto setupSlider = [this] (juce::Slider& s, int minVal, int maxVal, juce::Label& label, const juce::String& suffix)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setRange ((double) minVal, (double) maxVal, 1.0);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, sx (64), sx (20));
        s.setTextValueSuffix (suffix);

        addAndMakeVisible (label);
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredRight);
    };

    setupSlider (windowSlider,   ImpulseResponse::kMinWindowMs, ImpulseResponse::kMaxWindowMs,
                 windowLabel,    " ms");
    setupSlider (averagesSlider, ImpulseResponse::kMinAverages, ImpulseResponse::kMaxAverages,
                 averagesLabel,  "");

    windowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "irWindowMs", windowSlider);
    averagesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "irAverageCount", averagesSlider);

    // 120-second range is huge; without skew the slider's lower half is
    // useless. Skew so 1000 ms lands roughly mid-track.
    windowSlider.setSkewFactorFromMidPoint (1000.0);

    startTimerHz (30);
}

ImpulseDisplay::~ImpulseDisplay()
{
    stopTimer();
}

void ImpulseDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void ImpulseDisplay::syncViewButtons()
{
    const int v = (int) *processor.apvts.getRawParameterValue ("irView");
    waveformButton  .setToggleState (v == 0, juce::dontSendNotification);
    csdHeatmapButton.setToggleState (v == 1, juce::dontSendNotification);
    csd3DButton     .setToggleState (v == 2, juce::dontSendNotification);
}

void ImpulseDisplay::timerCallback()
{
    syncViewButtons();

    const bool csdActive = (int) *processor.apvts.getRawParameterValue ("irView") != 0;
    csdView.setVisible (csdActive);

    if (csdActive)
    {
        const auto& ir = processor.impulseResponse;
        using Ch = ImpulseResponse::Channel;
        csdView.updateSource (ir.getAveragedBuffer (Ch::L).data(), ir.getDisplayLength (Ch::L),
                              ir.getAveragedBuffer (Ch::R).data(), ir.getDisplayLength (Ch::R),
                              ir.getSampleRate(),
                              ir.getCompletedCaptures (Ch::L) + ir.getCompletedCaptures (Ch::R));
    }

    repaint();
}

void ImpulseDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (28));
    auto botRow = header;

    juce::ignoreUnused (topRow);   // paint() draws the text readouts directly

    // View-selector band between the header and the plot.
    auto viewBand = bounds.removeFromTop (sx (24));
    {
        const int vbW = sx (64), vbH = sx (20), vbGap = sx (4);
        int       vx  = viewBand.getCentreX() - (vbW * 3 + vbGap * 2) / 2;
        const int vy  = viewBand.getCentreY() - vbH / 2;
        waveformButton  .setBounds (vx, vy, vbW, vbH); vx += vbW + vbGap;
        csdHeatmapButton.setBounds (vx, vy, vbW, vbH); vx += vbW + vbGap;
        csd3DButton     .setBounds (vx, vy, vbW, vbH);
    }

    // The CSD view covers the same rect as the waveform plot.
    csdView.setUiScale (uiScale);
    csdView.setBounds (bounds.reduced (sx (24), sx (8)));

    // Bottom row: Clear (top) and Export (bottom) buttons stacked at the
    // far left, then Window and Averages slider rows on the right half.
    // The stack is centred in botRow so Clear keeps clear of the readout
    // text in the row above.
    const int buttonW   = sx (60);
    const int buttonH   = sx (17);
    const int buttonGap = sx (3);
    const int stackTop  = botRow.getY()
                        + (botRow.getHeight() - (buttonH * 2 + buttonGap)) / 2;
    clearButton .setBounds (botRow.getX() + sx (16), stackTop,
                            buttonW, buttonH);
    exportButton.setBounds (botRow.getX() + sx (16), stackTop + buttonH + buttonGap,
                            buttonW, buttonH);

    auto controlArea = botRow.removeFromRight (botRow.getWidth() - sx (84));
    controlArea.removeFromLeft (sx (16));

    const int labelW   = sx (60);
    const int sliderH  = sx (18);
    const int sliderY1 = controlArea.getY() + sx (3);
    const int sliderY2 = controlArea.getY() + sx (25);

    juce::ignoreUnused (sliderY2);

    // Stack the two sliders vertically inside the 44 px bot row.
    const int rowHeight = botRow.getHeight() / 2;
    auto winRow = controlArea.removeFromTop (rowHeight);
    auto avgRow = controlArea;

    windowLabel  .setBounds (winRow.removeFromLeft (labelW));
    windowSlider .setBounds (winRow);
    averagesLabel.setBounds (avgRow.removeFromLeft (labelW));
    averagesSlider.setBounds (avgRow);

    juce::ignoreUnused (sliderH, sliderY1);
}

void ImpulseDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    const auto& ir = processor.impulseResponse;
    const int   completedL = ir.getCompletedCaptures (ImpulseResponse::Channel::L);
    const int   completedR = ir.getCompletedCaptures (ImpulseResponse::Channel::R);
    const int   goal       = ir.getAverageGoal();
    const int   windowMs   = (int) *processor.apvts.getRawParameterValue ("irWindowMs");

    // Header text: per-channel averaging progress on left, window on right.
    auto header = bounds.removeFromTop (sx (72));
    auto topRow = header.removeFromTop (sx (28));
    bounds.removeFromTop (sx (24));   // view-selector band, owned by the buttons

    auto progressLine = [&] (int completed) -> juce::String
    {
        return completed == 0
            ? juce::String ("waiting")
            : (juce::String (completed) + " / " + juce::String (goal) + " averaged");
    };

    g.setFont (juce::FontOptions (sf (13.0f)));
    auto progressArea = topRow.withTrimmedLeft (sx (16));
    auto progTop = progressArea.withHeight (progressArea.getHeight() / 2);
    auto progBot = progressArea.withTrimmedTop (progressArea.getHeight() / 2);
    g.setColour (WTColors::analysis);
    g.drawText ("L: " + progressLine (completedL), progTop,
                juce::Justification::centredLeft, false);
    g.setColour (WTColors::analysis_R);
    g.drawText ("R: " + progressLine (completedR), progBot,
                juce::Justification::centredLeft, false);

    juce::String windowText;
    if (windowMs >= 1000)
        windowText = juce::String (windowMs / 1000.0f, 2) + " s";
    else
        windowText = juce::String (windowMs) + " ms";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (12.0f)));
    g.drawText ("Window  " + windowText,
                topRow.withTrimmedRight (sx (16)),
                juce::Justification::centredRight, false);

    // Plot area starts below the header + view band. In a CSD view the
    // csdView child component covers the plot rect and paints itself.
    if ((int) *processor.apvts.getRawParameterValue ("irView") == 0)
        drawWaveform (g, bounds.reduced (sx (24), sx (8)));
}

void ImpulseDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& ir = processor.impulseResponse;
    const int   nL = ir.getDisplayLength (ImpulseResponse::Channel::L);
    const int   nR = ir.getDisplayLength (ImpulseResponse::Channel::R);
    const int   N  = juce::jmax (nL, nR);
    const float sr = ir.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Play an impulse train through the device under test",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const float windowMs = (float) N * 1000.0f / sr;
    const auto& bufL = ir.getAveragedBuffer (ImpulseResponse::Channel::L);
    const auto& bufR = ir.getAveragedBuffer (ImpulseResponse::Channel::R);

    const bool showL    = *processor.apvts.getRawParameterValue ("showChannelL")    > 0.5f;
    const bool showR    = *processor.apvts.getRawParameterValue ("showChannelR")    > 0.5f;
    const bool showDiff = *processor.apvts.getRawParameterValue ("showChannelDiff") > 0.5f;
    const bool diffMode = showDiff;

    // Shared Y range: max abs amplitude across the visible series so the
    // overlay is visually comparable. In Diff mode only the R - L trace
    // is shown, so the range comes from its envelope.
    float peak = 0.0f;
    auto scanPeak = [&] (const std::vector<float>& buf, int len)
    {
        for (int i = 0; i < len; ++i)
            peak = std::max (peak, std::abs (buf[(size_t) i]));
    };
    if (diffMode)
    {
        const int diffLen = juce::jmin (nL, nR);
        for (int i = 0; i < diffLen; ++i)
            peak = std::max (peak, std::abs (bufR[(size_t) i] - bufL[(size_t) i]));
    }
    else
    {
        if (showL) scanPeak (bufL, nL);
        if (showR) scanPeak (bufR, nR);
    }
    if (peak < 1.0e-6f) peak = 1.0e-6f;
    const float yRange = peak * 1.1f;

    auto sampleToY = [&] (float v) -> float
    {
        const float t = 0.5f - 0.5f * (juce::jlimit (-yRange, yRange, v) / yRange);
        return (float) plotArea.getY() + t * (float) plotArea.getHeight();
    };

    g.setColour (juce::Colour (0xff2a2d32));
    {
        const float yZero = sampleToY (0.0f);
        g.drawLine ((float) plotArea.getX(), yZero, (float) plotArea.getRight(), yZero, sf (1.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));
    auto drawYLabel = [&] (float v, const juce::String& text)
    {
        const int y = (int) sampleToY (v);
        juce::Rectangle<int> r (labelGutterLeft.getX(), y - sx (6),
                                labelGutterLeft.getWidth() - sx (4), sx (12));
        g.drawText (text, r, juce::Justification::centredRight, false);
    };
    drawYLabel ( peak, juce::String (peak, 3));
    drawYLabel ( 0.0f, "0");
    drawYLabel (-peak, "-" + juce::String (peak, 3));

    auto drawXLabel = [&] (float tNorm, float ms)
    {
        const int x = plotArea.getX() + juce::roundToInt (tNorm * (float) plotArea.getWidth());
        const int textW = sx (44);
        juce::Rectangle<int> r (x - textW / 2,
                                labelGutterBottom.getY(),
                                textW,
                                labelGutterBottom.getHeight());
        juce::String text;
        if      (ms >= 1000.0f) text = juce::String (ms / 1000.0f, 2) + " s";
        else if (ms >= 10.0f)   text = juce::String ((int) std::round (ms)) + " ms";
        else                    text = juce::String (ms, 1) + " ms";
        g.drawText (text, r, juce::Justification::centredTop, false);
    };
    drawXLabel (0.00f, 0.0f);
    drawXLabel (0.25f, windowMs * 0.25f);
    drawXLabel (0.50f, windowMs * 0.50f);
    drawXLabel (0.75f, windowMs * 0.75f);
    drawXLabel (1.00f, windowMs);

    const int plotW = plotArea.getWidth();

    // Per-channel waveform renderer. `sampler(i)` returns the value at
    // sample index i (handles Diff via on-the-fly subtraction).
    auto drawTrace = [&] (auto sampler, int sampleCount, juce::Colour colour)
    {
        if (sampleCount <= 0) return;
        const float samplesPerPixel = (float) sampleCount / (float) std::max (1, plotW);

        g.setColour (colour);
        if (samplesPerPixel <= 1.0f)
        {
            juce::Path path;
            for (int i = 0; i < sampleCount; ++i)
            {
                const float tNorm = (float) i / (float) std::max (1, sampleCount - 1);
                const float x = (float) plotArea.getX() + tNorm * (float) plotW;
                const float y = sampleToY (sampler (i));
                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo          (x, y);
            }
            g.strokePath (path, juce::PathStrokeType (sf (1.2f)));
        }
        else
        {
            for (int px = 0; px < plotW; ++px)
            {
                const int i0 = (int) ((float) px * samplesPerPixel);
                const int i1 = std::min (sampleCount, (int) ((float) (px + 1) * samplesPerPixel));
                if (i1 <= i0) continue;
                float minV =  std::numeric_limits<float>::infinity();
                float maxV = -std::numeric_limits<float>::infinity();
                for (int i = i0; i < i1; ++i)
                {
                    const float v = sampler (i);
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }
                const float x  = (float) (plotArea.getX() + px);
                const float y0 = sampleToY (maxV);
                const float y1 = sampleToY (minV);
                g.drawLine (x, y0, x, std::max (y1, y0 + 1.0f), sf (1.0f));
            }
        }
    };

    if (diffMode)
    {
        // Diff view: bipolar R - L trace, sign-coloured by which channel
        // is louder at each sample. Two clipped passes - upper half in
        // R-variant analysis colour, lower half in L-master analysis
        // colour. L and R traces are hidden in this view.
        const int   diffLen = juce::jmin (nL, nR);
        const int   zeroY   = juce::roundToInt (sampleToY (0.0f));

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plotArea.getX(), plotArea.getY(),
                                plotArea.getWidth(),
                                juce::jmax (0, zeroY - plotArea.getY()));
            drawTrace ([&] (int i) { return bufR[(size_t) i] - bufL[(size_t) i]; },
                       diffLen, WTColors::analysis_R);
        }
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plotArea.getX(), zeroY,
                                plotArea.getWidth(),
                                juce::jmax (0, plotArea.getBottom() - zeroY));
            drawTrace ([&] (int i) { return bufR[(size_t) i] - bufL[(size_t) i]; },
                       diffLen, WTColors::analysis);
        }
    }
    else
    {
        // Normal view. R drawn first (underneath), L on top.
        if (showR)
            drawTrace ([&] (int i) { return bufR[(size_t) i]; }, nR, WTColors::analysis_R);
        if (showL)
            drawTrace ([&] (int i) { return bufL[(size_t) i]; }, nL, WTColors::analysis);
    }
}

void ImpulseDisplay::exportIRToWav()
{
    const auto& ir = processor.impulseResponse;
    const int nL = ir.getDisplayLength (ImpulseResponse::Channel::L);
    const int nR = ir.getDisplayLength (ImpulseResponse::Channel::R);

    if (nL <= 0 && nR <= 0)
    {
        // Nothing captured yet - no-op rather than write a silent file.
        return;
    }

    // Default destination: Desktop with a descriptive timestamped name so
    // multiple captures don't quietly overwrite each other.
    const juce::String stamp = juce::Time::getCurrentTime()
                                  .formatted ("%Y%m%d_%H%M%S");
    juce::File suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                              .getChildFile ("WTAnalyzer_DirectIR_" + stamp + ".wav");

    exportChooser = std::make_shared<juce::FileChooser> (
        "Export Impulse Response", suggested, "*.wav");

    exportChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            juce::File chosen = fc.getResult();
            if (chosen == juce::File()) return;

            if (! chosen.hasFileExtension ("wav"))
                chosen = chosen.withFileExtension ("wav");

            const auto& irSnapshot = processor.impulseResponse;
            const int snapL = irSnapshot.getDisplayLength (ImpulseResponse::Channel::L);
            const int snapR = irSnapshot.getDisplayLength (ImpulseResponse::Channel::R);
            const double sr = (double) irSnapshot.getSampleRate();

            const bool ok = IRExport::writeStereoWav (
                chosen,
                irSnapshot.getAveragedBuffer (ImpulseResponse::Channel::L), snapL,
                irSnapshot.getAveragedBuffer (ImpulseResponse::Channel::R), snapR,
                sr);

            if (! ok)
            {
                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Export failed")
                        .withMessage ("Could not write the IR to:\n" + chosen.getFullPathName())
                        .withButton ("OK"),
                    nullptr);
            }
        });
}
