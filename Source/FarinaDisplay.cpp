/*
  ==============================================================================

    FarinaDisplay.cpp

  ==============================================================================
*/

#include "FarinaDisplay.h"
#include "Colors.h"
#include "IRExport.h"

#include <algorithm>
#include <cmath>
#include <limits>

FarinaDisplay::FarinaDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);

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

    setupSlider (f0Slider,    f0Label,    " Hz");
    setupSlider (f1Slider,    f1Label,    " Hz");
    setupSlider (sweepSlider, sweepLabel, " s");
    setupSlider (tailSlider,  tailLabel,  " s");

    addAndMakeVisible (exportButton);
    exportButton.onClick = [this] { exportIRToWav(); };

    f0Attachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaF0Hz",     f0Slider);
    f1Attachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaF1Hz",     f1Slider);
    sweepAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaSweepSec", sweepSlider);
    tailAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "farinaTailSec",  tailSlider);

    startTimerHz (10);   // poll FarinaIR state + nudge deconvolution
}

FarinaDisplay::~FarinaDisplay()
{
    stopTimer();
}

void FarinaDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void FarinaDisplay::timerCallback()
{
    // Run any pending deconvolution. tryProcessCapture() is a no-op if
    // the audio thread hasn't finished a capture yet, so this is cheap.
    processor.farinaIR.tryProcessCapture();
    repaint();
}

juce::String FarinaDisplay::statusText() const
{
    const auto& f = processor.farinaIR;
    switch (f.getState())
    {
        case FarinaIR::State::Idle:           return "Idle - click Capture to arm";
        case FarinaIR::State::Armed:          return "Waiting for sweep onset";
        case FarinaIR::State::Capturing:
        {
            // Show the worst-case progress across L and R so the status
            // doesn't pretend to be done before both channels have caught
            // their sweeps.
            const int progL = f.getCaptureProgress (FarinaIR::Channel::L);
            const int progR = f.getCaptureProgress (FarinaIR::Channel::R);
            const int len   = f.getCaptureLength   (FarinaIR::Channel::L);
            const int prog  = juce::jmin (progL, progR);
            return "Capturing " + juce::String (prog) + " / " + juce::String (len);
        }
        case FarinaIR::State::ReadyToProcess: return "Processing...";
        case FarinaIR::State::IRReady:        return "IR ready";
    }
    return {};
}

void FarinaDisplay::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (sx (88));
    auto topRow = header.removeFromTop (sx (28));
    auto botRow = header;

    // Export button lives on the far right of the top row. The status
    // text and sweep summary in paint() reserve the same right-margin
    // width so they don't overlap.
    const int exportW = sx (78);
    const int exportH = sx (20);
    exportButton.setBounds (topRow.getRight() - sx (16) - exportW,
                            topRow.getCentreY() - exportH / 2,
                            exportW, exportH);

    // Bottom row: four slider rows stacked across the full header width.
    // The Capture / Clear buttons now live in the plugin header (see
    // PluginEditor) so this whole row is available for the sweep params.
    botRow.removeFromLeft (sx (16));
    auto sliderArea = botRow;
    sliderArea.removeFromRight (sx (12));

    const int labelW   = sx (60);
    const int rowCount = 4;
    const int rowH     = sliderArea.getHeight() / rowCount;

    auto layoutSliderRow = [&] (juce::Slider& s, juce::Label& label)
    {
        auto row = sliderArea.removeFromTop (rowH);
        label.setBounds (row.removeFromLeft (labelW));
        s    .setBounds (row);
    };

    layoutSliderRow (f0Slider,    f0Label);
    layoutSliderRow (f1Slider,    f1Label);
    layoutSliderRow (sweepSlider, sweepLabel);
    layoutSliderRow (tailSlider,  tailLabel);
}

void FarinaDisplay::paint (juce::Graphics& g)
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

    // Sweep summary on the right - quick-glance reference for what
    // the captured IR is measuring.
    const auto& f = processor.farinaIR;
    auto formatHz = [] (float hz) -> juce::String
    {
        if      (hz < 1000.0f)  return juce::String ((int) std::round (hz)) + " Hz";
        else if (hz < 10000.0f) return juce::String (hz / 1000.0f, 2) + " kHz";
        else                    return juce::String (hz / 1000.0f, 1) + " kHz";
    };

    const juce::String summary = formatHz (f.getF0Hz()) + "  to  "
                               + formatHz (f.getF1Hz()) + "  over  "
                               + juce::String (f.getSweepSec(), 1) + " s"
                               + "   tail " + juce::String (f.getTailSec(), 2) + " s";

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (11.0f)));
    // Reserve the right-edge area for the Export button so the summary
    // text doesn't draw beneath it. 78 px button + 16 px right margin +
    // 8 px gap before the text = 102 px trimmed.
    g.drawText (summary,
                topRow.withTrimmedRight (sx (102)),
                juce::Justification::centredRight, false);

    drawWaveform (g, bounds.reduced (sx (24), sx (12)));
}

void FarinaDisplay::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto plotArea = area;
    auto labelGutterLeft   = plotArea.removeFromLeft (sx (44));
    auto labelGutterBottom = plotArea.removeFromBottom (sx (18));

    g.setColour (juce::Colour (0xff181a1d));
    g.fillRect (plotArea);

    const auto& f  = processor.farinaIR;
    const int   nL = f.getIRLength (FarinaIR::Channel::L);
    const int   nR = f.getIRLength (FarinaIR::Channel::R);
    const int   N  = juce::jmax (nL, nR);
    const float sr = f.getSampleRate();

    if (N <= 0 || sr <= 0.0f)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (sf (12.0f)));
        g.drawText ("Set sweep parameters, click Capture, then play a log sine sweep through the device",
                    plotArea, juce::Justification::centred, false);
        return;
    }

    const auto& bufL = f.getIR (FarinaIR::Channel::L);
    const auto& bufR = f.getIR (FarinaIR::Channel::R);
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
        // Diff view: sign-coloured bipolar R - L trace. Two clipped passes
        // - upper half in analysis_R, lower half in analysis (master).
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

void FarinaDisplay::exportIRToWav()
{
    const auto& f  = processor.farinaIR;
    const int nL = f.getIRLength (FarinaIR::Channel::L);
    const int nR = f.getIRLength (FarinaIR::Channel::R);

    if (nL <= 0 && nR <= 0)
    {
        // Deconvolution hasn't produced a result yet - silently no-op.
        return;
    }

    const juce::String stamp = juce::Time::getCurrentTime()
                                  .formatted ("%Y%m%d_%H%M%S");
    juce::File suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                              .getChildFile ("WTAnalyzer_FarinaIR_" + stamp + ".wav");

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

            const auto& f = processor.farinaIR;
            const int snapL = f.getIRLength (FarinaIR::Channel::L);
            const int snapR = f.getIRLength (FarinaIR::Channel::R);
            const double sr = (double) f.getSampleRate();

            const bool ok = IRExport::writeStereoWav (
                chosen,
                f.getIR (FarinaIR::Channel::L), snapL,
                f.getIR (FarinaIR::Channel::R), snapR,
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
