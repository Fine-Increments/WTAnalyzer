/*
  ==============================================================================

    MlsDisplay.cpp
    See MlsDisplay.h.

  ==============================================================================
*/

#include "MlsDisplay.h"
#include "Colors.h"
#include "IRExport.h"

#include <algorithm>
#include <cmath>
#include <limits>

MlsDisplay::MlsDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc),
      csdView   (proc)
{
    setOpaque (true);

    // Waveform / CSD Heatmap / CSD 3D selector (radio group), backed by the
    // shared `irView` parameter - identical to FarinaDisplay.
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

    auto setupSlider = [this] (juce::Slider& s, juce::Label& label,
                               const juce::String& suffix)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, sx (60), sx (18));
        s.setTextValueSuffix (suffix);

        addAndMakeVisible (label);
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredRight);
    };

    setupSlider (orderSlider, orderLabel, {});
    setupSlider (tailSlider,  tailLabel,  " s");

    addAndMakeVisible (exportButton);
    exportButton.onClick = [this] { exportIRToWav(); };

    orderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mlsIrOrder", orderSlider);
    tailAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "mlsIrTailSec", tailSlider);

    startTimerHz (10);   // poll MlsIR state + nudge the correlation
}

MlsDisplay::~MlsDisplay()
{
    stopTimer();
}

void MlsDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void MlsDisplay::syncViewButtons()
{
    const int v = (int) *processor.apvts.getRawParameterValue ("irView");
    waveformButton  .setToggleState (v == 0, juce::dontSendNotification);
    csdHeatmapButton.setToggleState (v == 1, juce::dontSendNotification);
    csd3DButton     .setToggleState (v == 2, juce::dontSendNotification);
}

void MlsDisplay::timerCallback()
{
    // Run any pending correlation. tryProcessCapture() is a no-op if the
    // audio thread hasn't finished a capture yet, so this is cheap.
    processor.mlsIR.tryProcessCapture();

    syncViewButtons();

    const bool csdActive = (int) *processor.apvts.getRawParameterValue ("irView") != 0;
    csdView.setVisible (csdActive);

    if (csdActive)
    {
        const auto& m = processor.mlsIR;
        using Ch = MlsIR::Channel;
        csdView.updateSource (m.getIR (Ch::L).data(), m.getIRLength (Ch::L),
                              m.getIR (Ch::R).data(), m.getIRLength (Ch::R),
                              (double) m.getSampleRate(), m.getIRGeneration());
    }

    repaint();
}

juce::String MlsDisplay::statusText() const
{
    const auto& m = processor.mlsIR;
    switch (m.getState())
    {
        case MlsIR::State::Idle:           return "Idle - click Capture to arm";
        case MlsIR::State::Armed:          return "Waiting for MLS";
        case MlsIR::State::Capturing:
        {
            const int progL = m.getCaptureProgress (MlsIR::Channel::L);
            const int progR = m.getCaptureProgress (MlsIR::Channel::R);
            const int len   = m.getCaptureLength   (MlsIR::Channel::L);
            const int prog  = juce::jmin (progL, progR);
            return "Capturing " + juce::String (prog) + " / " + juce::String (len);
        }
        case MlsIR::State::ReadyToProcess: return "Correlating...";
        case MlsIR::State::IRReady:        return "IR ready";
    }
    return {};
}

void MlsDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (88));
    auto topRow = header.removeFromTop (sx (28));

    const int exportW = sx (60);
    const int exportH = sx (17);
    exportButton.setBounds (topRow.getRight() - sx (16) - exportW,
                            topRow.getCentreY() - exportH / 2,
                            exportW, exportH);

    header.removeFromLeft (sx (16));
    auto sliderArea = header;
    sliderArea.removeFromRight (sx (12));

    const int labelW   = sx (60);
    const int rowCount = 2;
    const int rowH     = sliderArea.getHeight() / rowCount;

    auto layoutSliderRow = [&] (juce::Slider& s, juce::Label& label)
    {
        auto row = sliderArea.removeFromTop (rowH);
        label.setBounds (row.removeFromLeft (labelW));
        s    .setBounds (row);
    };

    layoutSliderRow (orderSlider, orderLabel);
    layoutSliderRow (tailSlider,  tailLabel);

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
}

void MlsDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (bounds);

    auto header = bounds.removeFromTop (sx (88));
    auto topRow = header.removeFromTop (sx (28));

    g.setColour (WTColors::analysis);
    g.setFont (juce::FontOptions (sf (16.0f)));
    g.drawText (statusText(),
                topRow.withTrimmedLeft (sx (16)),
                juce::Justification::centredLeft, false);

    // Summary on the right: order, the resulting sequence period, and tail.
    const auto& m       = processor.mlsIR;
    const int   order   = m.getOrder();
    const float sr       = m.getSampleRate();
    const double periodS = (sr > 0.0f) ? (double) ((1 << order) - 1) / (double) sr : 0.0;

    const juce::String summary = "Order " + juce::String (order)
                               + "   period " + juce::String (periodS, 2) + " s"
                               + "   tail " + juce::String (m.getTailSec(), 2) + " s";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));
    g.drawText (summary,
                topRow.withTrimmedRight (sx (84)),
                juce::Justification::centredRight, false);

    // Plot area starts below the header + view band. In a CSD view the
    // csdView child component covers the plot rect and paints itself.
    bounds.removeFromTop (sx (24));   // view-selector band, owned by the buttons
    if ((int) *processor.apvts.getRawParameterValue ("irView") == 0)
        drawWaveform (g, bounds.reduced (sx (24), sx (8)));
}

void MlsDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& m  = processor.mlsIR;
    const int   nL = m.getIRLength (MlsIR::Channel::L);
    const int   nR = m.getIRLength (MlsIR::Channel::R);
    const int   N  = juce::jmax (nL, nR);
    const float sr = m.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Set the MLS order to match WTGenerator, click Capture, "
                    "then play the MLS through the device",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const auto& bufL = m.getIR (MlsIR::Channel::L);
    const auto& bufR = m.getIR (MlsIR::Channel::R);
    const float windowMs = (float) N * 1000.0f / sr;

    const bool showL    = *processor.apvts.getRawParameterValue ("showChannelL")    > 0.5f;
    const bool showR    = *processor.apvts.getRawParameterValue ("showChannelR")    > 0.5f;
    const bool showDiff = *processor.apvts.getRawParameterValue ("showChannelDiff") > 0.5f;
    const bool diffMode = showDiff;

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
        const float tt = 0.5f - 0.5f * (juce::jlimit (-yRange, yRange, v) / yRange);
        return (float) plotArea.getY() + tt * (float) plotArea.getHeight();
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
        const int diffLen = juce::jmin (nL, nR);
        const int zeroY   = juce::roundToInt (sampleToY (0.0f));

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
        if (showR)
            drawTrace ([&] (int i) { return bufR[(size_t) i]; }, nR, WTColors::analysis_R);
        if (showL)
            drawTrace ([&] (int i) { return bufL[(size_t) i]; }, nL, WTColors::analysis);
    }
}

void MlsDisplay::exportIRToWav()
{
    const auto& m  = processor.mlsIR;
    const int nL = m.getIRLength (MlsIR::Channel::L);
    const int nR = m.getIRLength (MlsIR::Channel::R);

    if (nL <= 0 && nR <= 0)
        return;   // nothing captured yet

    const juce::String stamp = juce::Time::getCurrentTime()
                                  .formatted ("%Y%m%d_%H%M%S");
    juce::File suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                              .getChildFile ("WTAnalyzer_MlsIR_" + stamp + ".wav");

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

            const auto& m = processor.mlsIR;
            const bool ok = IRExport::writeStereoWav (
                chosen,
                m.getIR (MlsIR::Channel::L), m.getIRLength (MlsIR::Channel::L),
                m.getIR (MlsIR::Channel::R), m.getIRLength (MlsIR::Channel::R),
                (double) m.getSampleRate());

            if (! ok)
                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Export failed")
                        .withMessage ("Could not write the IR to:\n" + chosen.getFullPathName())
                        .withButton ("OK"),
                    nullptr);
        });
}
