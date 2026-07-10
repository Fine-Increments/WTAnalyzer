/*
  ==============================================================================

    SpectrumDisplay.cpp

  ==============================================================================
*/

#include "SpectrumDisplay.h"
#include "Colors.h"
#include "Analyses/FrequencyResponse.h"
#include "Analyses/AliasingDetection.h"

#include <vector>

SpectrumDisplay::SpectrumDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);
    startTimerHz (30);

    // Initialise viewMaxFreq to the processor's current sample rate / 2.
    // It will be re-clamped on every paint in case the host changes sample
    // rate while the editor is open.
    viewMaxFreq = processor.currentSampleRate.load (std::memory_order_relaxed) * 0.5f;

    auto configureAliasingViewButton = [this] (juce::TextButton& b, int paramIndex)
    {
        addChildComponent (b);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (2, juce::dontSendNotification);
        b.onClick = [this, paramIndex]
        {
            if (auto* p = processor.apvts.getParameter ("aliasingView"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) paramIndex));
        };
    };

    configureAliasingViewButton (aliasingCompositeButton, 0);
    configureAliasingViewButton (aliasingPreButton,       1);
    configureAliasingViewButton (aliasingPostButton,      2);

    // Hold toggles peak-hold rendering for whichever view is active.
    // Neutral light-grey engaged colour, matching the THD pattern.
    const juce::Colour engagedFill (0xffcfd2d6);
    addChildComponent (aliasingHoldButton);
    aliasingHoldButton.setClickingTogglesState (true);
    aliasingHoldButton.setColour (juce::TextButton::buttonOnColourId, engagedFill);
    aliasingHoldButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    aliasingHoldButton.onClick = [this]
    {
        isAliasingHolding = aliasingHoldButton.getToggleState();
        repaint();
    };

    // Clear is momentary; wipes the peak-held arrays so a fresh sweep
    // can be captured.
    addChildComponent (aliasingClearButton);
    aliasingClearButton.onClick = [this]
    {
        processor.aliasingDetection.clearPeaks();
        repaint();
    };

    processor.apvts.addParameterListener ("aliasingView", this);
    syncAliasingViewButtons();
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
    processor.apvts.removeParameterListener ("aliasingView", this);
}

void SpectrumDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
    resized();
}

void SpectrumDisplay::resized()
{
    layoutAliasingViewButtons (getPlotArea());
}

void SpectrumDisplay::timerCallback()
{
    repaint();
}

void SpectrumDisplay::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID == "aliasingView")
    {
        juce::MessageManager::callAsync ([this]
        {
            syncAliasingViewButtons();
            repaint();
        });
    }
}

void SpectrumDisplay::syncAliasingViewButtons()
{
    const int idx = aliasingViewIndex();
    aliasingCompositeButton.setToggleState (idx == 0, juce::dontSendNotification);
    aliasingPreButton      .setToggleState (idx == 1, juce::dontSendNotification);
    aliasingPostButton     .setToggleState (idx == 2, juce::dontSendNotification);

    // Hold/Clear apply only to the Composite view; hide them on Pre/Post.
    // Stay invisible entirely when the view-toggle buttons themselves are
    // hidden (i.e. when we're not in AliasingDetection mode).
    const bool inAliasingMode = aliasingCompositeButton.isVisible()
                             || aliasingPreButton.isVisible()
                             || aliasingPostButton.isVisible();
    aliasingHoldButton .setVisible (inAliasingMode && idx == 0);
    aliasingClearButton.setVisible (inAliasingMode && idx == 0);
}

int SpectrumDisplay::aliasingViewIndex() const noexcept
{
    return (int) *processor.apvts.getRawParameterValue ("aliasingView");
}

void SpectrumDisplay::setAliasingViewButtonsVisible (bool shouldBeVisible)
{
    aliasingCompositeButton.setVisible (shouldBeVisible);
    aliasingPreButton      .setVisible (shouldBeVisible);
    aliasingPostButton     .setVisible (shouldBeVisible);

    // Hold/Clear are meaningful only in Composite view (they operate on
    // the green differential trace, which only that view draws). The
    // parameter listener and syncAliasingViewButtons() pick up the
    // additional Composite-vs-not check.
    const bool wantsCaptureControls = shouldBeVisible && aliasingViewIndex() == 0;
    aliasingHoldButton .setVisible (wantsCaptureControls);
    aliasingClearButton.setVisible (wantsCaptureControls);
}

void SpectrumDisplay::layoutAliasingViewButtons (juce::Rectangle<int> plotArea)
{
    const int viewW    = sx (66);
    const int ctrlW    = sx (58);
    const int buttonH  = sx (20);
    const int spacing  = sx (6);
    const int groupGap = sx (20);

    const int startX = plotArea.getX() + sx (8);
    const int buttonY = plotArea.getY() + sx (8);

    aliasingCompositeButton.setBounds (startX,                          buttonY, viewW, buttonH);
    aliasingPreButton      .setBounds (startX +     (viewW + spacing), buttonY, viewW, buttonH);
    aliasingPostButton     .setBounds (startX + 2 * (viewW + spacing), buttonY, viewW, buttonH);

    const int controlX = startX + 3 * viewW + 2 * spacing + groupGap;
    aliasingHoldButton .setBounds (controlX,                   buttonY, ctrlW, buttonH);
    aliasingClearButton.setBounds (controlX + ctrlW + spacing, buttonY, ctrlW, buttonH);
}

juce::Rectangle<int> SpectrumDisplay::getPlotArea() const noexcept
{
    auto fullArea = getLocalBounds();
    const int dbGutter   = sx (42);
    const int freqGutter = sx (16);
    fullArea.removeFromBottom (freqGutter);
    fullArea.removeFromLeft  (dbGutter);
    return fullArea;
}

void SpectrumDisplay::resetDbView() noexcept
{
    viewMinDb = kDefaultMinDb;
    viewMaxDb = kDefaultMaxDb;
    repaint();
}

void SpectrumDisplay::resetFreqView() noexcept
{
    viewMinFreq = kDefaultMinFreq;
    viewMaxFreq = processor.currentSampleRate.load (std::memory_order_relaxed) * 0.5f;
    repaint();
}

void SpectrumDisplay::resetView() noexcept
{
    resetDbView();
    resetFreqView();
}

SpectrumDisplay::DragZone SpectrumDisplay::zoneFromPoint (juce::Point<int> p) const noexcept
{
    const int dbGutterRight  = sx (42);
    const int freqGutterTop  = getHeight() - sx (16);

    if (p.getX() < dbGutterRight)  return DragZone::DbAxis;
    if (p.getY() > freqGutterTop)  return DragZone::FreqAxis;
    return DragZone::Plot;
}

void SpectrumDisplay::updateHoverFromPoint (juce::Point<int> p) noexcept
{
    const auto plotArea = getPlotArea();
    if (plotArea.isEmpty() || ! plotArea.contains (p))
    {
        hoverActive = false;
        return;
    }

    const float fy = (float) (p.getY() - plotArea.getY()) / (float) plotArea.getHeight();
    hoverDb = viewMaxDb - fy * (viewMaxDb - viewMinDb);

    const float fx = (float) (p.getX() - plotArea.getX()) / (float) plotArea.getWidth();
    const float logFreq = std::log10 (viewMinFreq)
                          + fx * (std::log10 (viewMaxFreq) - std::log10 (viewMinFreq));
    hoverFreq = std::pow (10.0f, logFreq);

    hoverActive = true;
}

void SpectrumDisplay::clampViewToLimits() noexcept
{
    const float currentNyquist = processor.currentSampleRate.load (std::memory_order_relaxed) * 0.5f;

    viewMinDb   = juce::jmax (viewMinDb,   kHardMinDb);
    viewMaxDb   = juce::jmin (viewMaxDb,   kHardMaxDb);
    viewMinFreq = juce::jmax (viewMinFreq, kHardMinFreq);
    viewMaxFreq = juce::jmin (viewMaxFreq, currentNyquist);

    // Enforce a minimum visible range so the user can't zoom past the
    // point where the chart becomes unreadable.
    if (viewMaxDb - viewMinDb < kMinDbRange)
    {
        const float centreDb = (viewMaxDb + viewMinDb) * 0.5f;
        viewMinDb = centreDb - kMinDbRange * 0.5f;
        viewMaxDb = centreDb + kMinDbRange * 0.5f;
    }

    const float currLogRange = std::log10 (viewMaxFreq) - std::log10 (viewMinFreq);
    if (currLogRange < kMinLogFreqRange)
    {
        const float logCentre = (std::log10 (viewMaxFreq) + std::log10 (viewMinFreq)) * 0.5f;
        viewMinFreq = std::pow (10.0f, logCentre - kMinLogFreqRange * 0.5f);
        viewMaxFreq = std::pow (10.0f, logCentre + kMinLogFreqRange * 0.5f);
    }
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragZone         = zoneFromPoint (e.getPosition());
    dragStartPos     = e.getPosition();
    dragStartMinDb   = viewMinDb;
    dragStartMaxDb   = viewMaxDb;
    dragStartMinFreq = viewMinFreq;
    dragStartMaxFreq = viewMaxFreq;

    updateHoverFromPoint (e.getPosition());
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    updateHoverFromPoint (e.getPosition());
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent& /*e*/)
{
    hoverActive = false;
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragZone == DragZone::None)
        return;

    const auto plotArea = getPlotArea();
    if (plotArea.isEmpty())
        return;

    // Vertical drag distance controls the zoom factor: up = in, down = out.
    // 100 px of drag corresponds to one octave of zoom.
    const int   dy         = e.getPosition().getY() - dragStartPos.getY();
    const float zoomFactor = std::pow (2.0f, -(float) dy / 100.0f);

    // The focal point uses the drag-start position projected into the plot
    // area (cursor coordinate may be in a gutter, but the data axis still
    // extends across the full plot width/height; we clamp the coordinate
    // to the plot rect before computing the focal value).
    const int clampedY = juce::jlimit (plotArea.getY(), plotArea.getBottom() - 1, dragStartPos.getY());
    const int clampedX = juce::jlimit (plotArea.getX(), plotArea.getRight()  - 1, dragStartPos.getX());

    if (dragZone == DragZone::DbAxis || dragZone == DragZone::Plot)
    {
        const float fy = (float) (clampedY - plotArea.getY()) / (float) plotArea.getHeight();
        const float focalDb = dragStartMaxDb - fy * (dragStartMaxDb - dragStartMinDb);

        const float oldRangeDb = dragStartMaxDb - dragStartMinDb;
        const float newRangeDb = oldRangeDb / zoomFactor;
        const float tDb        = (focalDb - dragStartMinDb) / oldRangeDb;
        viewMinDb = focalDb - tDb * newRangeDb;
        viewMaxDb = focalDb + (1.0f - tDb) * newRangeDb;
    }

    if (dragZone == DragZone::FreqAxis || dragZone == DragZone::Plot)
    {
        const float fx = (float) (clampedX - plotArea.getX()) / (float) plotArea.getWidth();
        const float logStartMin  = std::log10 (dragStartMinFreq);
        const float logStartMax  = std::log10 (dragStartMaxFreq);
        const float focalLogFreq = logStartMin + fx * (logStartMax - logStartMin);

        const float oldLogRangeFreq = logStartMax - logStartMin;
        const float newLogRangeFreq = oldLogRangeFreq / zoomFactor;
        const float tFreq           = (focalLogFreq - logStartMin) / oldLogRangeFreq;
        viewMinFreq = std::pow (10.0f, focalLogFreq - tFreq * newLogRangeFreq);
        viewMaxFreq = std::pow (10.0f, focalLogFreq + (1.0f - tFreq) * newLogRangeFreq);
    }

    clampViewToLimits();
    updateHoverFromPoint (e.getPosition());
    repaint();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto zone = zoneFromPoint (e.getPosition());
    if      (zone == DragZone::DbAxis)   resetDbView();
    else if (zone == DragZone::FreqAxis) resetFreqView();
    else                                 resetView();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto fullArea = getLocalBounds();

    g.setColour (juce::Colour (0xff111213));
    g.fillRect (fullArea);

    const int dbGutter   = sx (42);
    const int freqGutter = sx (16);

    auto labelGutterBottom = fullArea.removeFromBottom (freqGutter);
    auto labelGutterLeft   = fullArea.removeFromLeft  (dbGutter);
    const auto plotArea    = fullArea;

    // Re-clamp to current Nyquist on every paint so a sample-rate change
    // doesn't leave the view in an invalid state.
    const float currentNyquist = processor.currentSampleRate.load (std::memory_order_relaxed) * 0.5f;
    if (viewMaxFreq > currentNyquist)
        viewMaxFreq = currentNyquist;

    const float sr       = currentNyquist * 2.0f;
    const float logMin   = std::log10 (viewMinFreq);
    const float logMax   = std::log10 (viewMaxFreq);
    const float logRange = logMax - logMin;

    // L / R channel toggles. The spectrum modes (FR, Aliasing) show
    // per-channel traces; stereo difference analysis lives in its own
    // dedicated Stereo Image mode, not here.
    const bool showL = *processor.apvts.getRawParameterValue ("showChannelL") > 0.5f;
    const bool showR = *processor.apvts.getRawParameterValue ("showChannelR") > 0.5f;

    const float dbRange = viewMaxDb - viewMinDb;

    // Dynamic dB-axis tick step: a "nice" 1 / 2 / 5 value sized to the
    // current (possibly zoomed) range, so gridlines and labels span the
    // whole visible range. A fixed grid would leave the axis unlabelled
    // wherever the view is zoomed or panned off the hardcoded values.
    const float dbTickStep = [dbRange]
    {
        const float rough = juce::jmax (dbRange, 1.0e-3f) / 7.0f;
        const float mag   = std::pow (10.0f, std::floor (std::log10 (rough)));
        const float norm  = rough / mag;
        const float s = (norm < 1.5f) ? 1.0f : (norm < 3.5f) ? 2.0f
                      : (norm < 7.5f) ? 5.0f : 10.0f;
        return s * mag;
    }();
    const int dbTickDecimals = dbTickStep < 0.1f ? 2 : (dbTickStep < 1.0f ? 1 : 0);

    // Dynamic frequency-axis ticks: log-correct decade ticks normally,
    // finer decade subdivisions when zoomed in, and a linear nice-step
    // fallback for very narrow zooms (where log ~= linear). A fixed
    // 20..20k set would leave the axis unlabelled inside a zoomed band.
    std::vector<float> freqTicks;
    {
        const int dLo = (int) std::floor (logMin);
        const int dHi = (int) std::ceil  (logMax);
        auto addMantissas = [&] (std::initializer_list<float> ms)
        {
            freqTicks.clear();
            for (int d = dLo; d <= dHi; ++d)
                for (float m : ms)
                {
                    const float f = m * std::pow (10.0f, (float) d);
                    if (f >= viewMinFreq && f <= viewMaxFreq) freqTicks.push_back (f);
                }
        };
        addMantissas ({ 1.0f, 2.0f, 5.0f });
        if (freqTicks.size() < 4)
            addMantissas ({ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f });
        if (freqTicks.size() < 3)
        {
            freqTicks.clear();
            const float rough = juce::jmax (viewMaxFreq - viewMinFreq, 1.0f) / 6.0f;
            const float mag   = std::pow (10.0f, std::floor (std::log10 (rough)));
            const float norm  = rough / mag;
            const float step  = ((norm < 1.5f) ? 1.0f : (norm < 3.5f) ? 2.0f
                                : (norm < 7.5f) ? 5.0f : 10.0f) * mag;
            for (float f = std::ceil (viewMinFreq / step) * step;
                 f <= viewMaxFreq + step * 0.001f; f += step)
                freqTicks.push_back (f);
        }
    }

    auto formatHz = [] (float hz) -> juce::String
    {
        if (hz < 1000.0f)
            return juce::String (juce::roundToInt (hz));
        const float k = hz / 1000.0f;
        if (std::abs (k - std::round (k)) < 0.01f)
            return juce::String (juce::roundToInt (k)) + "k";
        return juce::String (k, 1) + "k";
    };

    auto freqToX = [&] (float freq) -> float
    {
        const float t = (std::log10 (freq) - logMin) / logRange;
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };

    auto dbToY = [&] (float db) -> float
    {
        const float t = (db - viewMinDb) / dbRange;
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    // Gridlines - skip any whose value is outside the current view.
    g.setColour (juce::Colour (0xff2a2d32));

    for (float f : freqTicks)
    {
        const float x = freqToX (f);
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
    }

    // dB gridlines at the dynamic tick step, spanning the view range.
    for (float db = std::ceil (viewMinDb / dbTickStep) * dbTickStep;
         db <= viewMaxDb + dbTickStep * 0.001f;
         db += dbTickStep)
    {
        const float y = dbToY (db);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    // Axis labels.
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    for (float f : freqTicks)
    {
        const float x = freqToX (f);
        const int textWidth = sx (40);
        juce::Rectangle<int> r ((int) x - textWidth / 2,
                                labelGutterBottom.getY(),
                                textWidth,
                                labelGutterBottom.getHeight());
        g.drawText (formatHz (f), r, juce::Justification::centredTop, false);
    }

    // dB labels at the dynamic tick step, matching the gridlines.
    for (float db = std::ceil (viewMinDb / dbTickStep) * dbTickStep;
         db <= viewMaxDb + dbTickStep * 0.001f;
         db += dbTickStep)
    {
        const float y = dbToY (db);
        const int textHeight = sx (12);
        juce::Rectangle<int> r (labelGutterLeft.getX(),
                                (int) y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (11),
                                textHeight);
        juce::String txt;
        if (std::abs (db) < dbTickStep * 0.5f)
            txt = "0";
        else
            txt = (db > 0.0f ? "+" : "") + juce::String (db, dbTickDecimals);
        g.drawText (txt, r, juce::Justification::centredRight, false);
    }

    // Restrict trace drawing to the plot area so curves outside the visible
    // dB range get clipped rather than drawn over the label gutters.
    {
        juce::Graphics::ScopedSaveState clipScope (g);
        g.reduceClipRegion (plotArea);

        const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;
        const int   N            = WTAnalyzerAudioProcessor::kSpectrumBins;

        auto plotTrace = [&] (const std::array<float, WTAnalyzerAudioProcessor::kSpectrumBins>& spec,
                              juce::Colour colour)
        {
            juce::Path path;
            bool       first = true;

            for (int bin = 1; bin < N; ++bin)
            {
                const float f = (float) bin * binFreqScale;
                if (f < viewMinFreq) continue;
                if (f > viewMaxFreq) break;

                const float clampedDb = juce::jlimit (viewMinDb, viewMaxDb, spec[bin]);
                const float x = freqToX (f);
                const float y = dbToY (clampedDb);

                if (first) { path.startNewSubPath (x, y); first = false; }
                else       { path.lineTo          (x, y); }
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (sf (1.2f)));
        };

        // Plots data that uses kNoMeasurementDb as a sentinel for "no value
        // here" - the path breaks at those bins instead of drawing through
        // them as a floor value. Used for peak-held arrays and the live
        // differential, all of which can be sparse.
        auto plotSparseTrace = [&] (const float* values, int count,
                                    juce::Colour colour, float strokeW)
        {
            juce::Path path;
            bool       hasOpenSubPath = false;

            for (int bin = 1; bin < count; ++bin)
            {
                const float f = (float) bin * binFreqScale;
                if (f < viewMinFreq) continue;
                if (f > viewMaxFreq) break;

                const float v = values[bin];
                if (v <= AliasingDetection::kNoMeasurementDb + 0.5f)
                {
                    hasOpenSubPath = false;
                    continue;
                }

                const float clampedDb = juce::jlimit (viewMinDb, viewMaxDb, v);
                const float x = freqToX (f);
                const float y = dbToY (clampedDb);

                if (! hasOpenSubPath) { path.startNewSubPath (x, y); hasOpenSubPath = true; }
                else                  { path.lineTo          (x, y); }
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (sf (strokeW)));
        };

        const int activeAnalysisIdx = (int) *processor.apvts.getRawParameterValue ("activeAnalysis");
        const bool inAliasingMode  = activeAnalysisIdx
                                  == (int) WTAnalyzerAudioProcessor::AnalysisMode::AliasingDetection;
        const int aliasView = inAliasingMode ? aliasingViewIndex() : 0;

        if (! inAliasingMode)
        {
            // Draw order: R first (underneath), L on top.
            // See feedback-stereo-lr-diff-convention memory.
            if (showR)
            {
                plotTrace (processor.preSpectrumDb_R,  WTColors::preEffect_R);
                plotTrace (processor.postSpectrumDb_R, WTColors::postEffect_R);
            }
            if (showL)
            {
                plotTrace (processor.preSpectrumDb,    WTColors::preEffect);
                plotTrace (processor.postSpectrumDb,   WTColors::postEffect);
            }
        }
        else if (aliasView == 1)   // Pre - always live, Hold doesn't apply
        {
            if (showR) plotTrace (processor.preSpectrumDb_R, WTColors::preEffect_R);
            if (showL) plotTrace (processor.preSpectrumDb,   WTColors::preEffect);
        }
        else if (aliasView == 2)   // Post - always live, Hold doesn't apply
        {
            if (showR) plotTrace (processor.postSpectrumDb_R, WTColors::postEffect_R);
            if (showL) plotTrace (processor.postSpectrumDb,   WTColors::postEffect);
        }

        // When FrequencyResponse mode is active, overlay the
        // transfer-function trace per channel.
        // Draw order: R first (underneath), L on top.
        if (activeAnalysisIdx == (int) WTAnalyzerAudioProcessor::AnalysisMode::FrequencyResponse)
        {
            const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

            // Hybrid rendering: outside the signal range (frequencies
            // where pre never had energy) the trace sits at 0 dB.
            // Inside the signal range, between-harmonic invalid bins are
            // SKIPPED so the trace connects valid bins directly - sparse
            // harmonic content reads as a smooth EQ curve through the
            // harmonic peaks instead of a fast 0-to-measurement-to-0
            // comb. (Aliasing residue keeps its own sparse-trace
            // behaviour - its data is intentionally regional.)
            auto drawFrTrace = [&] (const std::vector<float>& response,
                                    juce::Colour colour)
            {
                const int M = (int) response.size();
                const float sentFloor = FrequencyResponse::kNoMeasurementDb + 0.5f;

                int loBin = -1, hiBin = -1;
                for (int bin = 1; bin < M; ++bin)
                {
                    if (response[(size_t) bin] > sentFloor)
                    {
                        if (loBin < 0) loBin = bin;
                        hiBin = bin;
                    }
                }

                juce::Path path;
                bool       hasOpenSubPath = false;

                for (int bin = 1; bin < M; ++bin)
                {
                    const float f = (float) bin * binFreqScale;
                    if (f < viewMinFreq) continue;
                    if (f > viewMaxFreq) break;

                    const float db = response[(size_t) bin];
                    const bool  valid = db > sentFloor;
                    const bool  inSignalRange = loBin >= 0 && bin >= loBin && bin <= hiBin;

                    float drawDb;
                    if (valid)                drawDb = db;
                    else if (! inSignalRange) drawDb = 0.0f;
                    else                      continue;   // gap inside signal range - interpolate

                    const float clampedDb = juce::jlimit (viewMinDb, viewMaxDb, drawDb);
                    const float x = freqToX (f);
                    const float y = dbToY (clampedDb);

                    if (! hasOpenSubPath) { path.startNewSubPath (x, y); hasOpenSubPath = true; }
                    else                  { path.lineTo          (x, y); }
                }

                g.setColour (colour);
                g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
            };

            if (showR)
                drawFrTrace (processor.frequencyResponse.getResponseDb_R(), WTColors::analysis_R);
            if (showL)
                drawFrTrace (processor.frequencyResponse.getResponseDb(),   WTColors::analysis);
        }

        // AliasingDetection composite view: the input signal (pre)
        // drawn as-is in the pre channel colour, with the green
        // differential overlay sitting on top to mark only what the
        // device added. For a transparent device the green is empty
        // and the composite visually matches the Pre view; any green
        // that appears is strictly the effect-under-test's
        // contribution because pre's own aliasing is subtracted out
        // by the (post^2 - pre^2) math.
        //
        // Hold swaps both traces to their peak-held counterparts so
        // a sweep accumulates the full picture.
        if (inAliasingMode && aliasView == 0)
        {
            const auto& liveL = processor.aliasingDetection.getLiveDifferentialDb();
            const auto& liveR = processor.aliasingDetection.getLiveDifferentialDb_R();
            const auto& peakL = processor.aliasingDetection.getPeakDifferentialDb();
            const auto& peakR = processor.aliasingDetection.getPeakDifferentialDb_R();

            // Pre trace is always live - Hold only applies to the alias
            // residue, since the user-relevant accumulated measurement is
            // "what aliasing did the device produce across the sweep",
            // not "where was the input signal".
            if (showR) plotTrace (processor.preSpectrumDb_R, WTColors::preEffect_R);
            if (showL) plotTrace (processor.preSpectrumDb,   WTColors::preEffect);

            const float* aliasR = (isAliasingHolding ? peakR : liveR).data();
            const float* aliasL = (isAliasingHolding ? peakL : liveL).data();

            if (showR) plotSparseTrace (aliasR, N, WTColors::analysis_R, 1.6f);
            if (showL) plotSparseTrace (aliasL, N, WTColors::analysis,   1.6f);
        }

        // HUD: peak alias readout in the top-right of the plot, shown
        // in every aliasing view so the user can confirm at a glance
        // whether anything has been detected even while looking at the
        // Pre / Post sanity-check traces. Peak is across all frames
        // since the last reset / Clear.
        if (inAliasingMode)
        {
            auto formatHz = [] (float hz) -> juce::String
            {
                if (hz < 1000.0f)  return juce::String ((int) std::round (hz)) + " Hz";
                if (hz < 10000.0f) return juce::String (hz / 1000.0f, 2) + " kHz";
                return juce::String (hz / 1000.0f, 1) + " kHz";
            };

            auto formatLine = [&] (float db, float hz) -> juce::String
            {
                if (db <= AliasingDetection::kNoMeasurementDb + 1.0f)
                    return "no residue";
                return juce::String (db, 1) + " dB FS at " + formatHz (hz);
            };

            const float lDb = processor.aliasingDetection.getPeakResidueDb();
            const float lHz = processor.aliasingDetection.getPeakResidueHz();
            const float rDb = processor.aliasingDetection.getPeakResidueDb_R();
            const float rHz = processor.aliasingDetection.getPeakResidueHz_R();

            const juce::String lLine = "L: " + formatLine (lDb, lHz);
            const juce::String rLine = "R: " + formatLine (rDb, rHz);

            g.setFont (juce::FontOptions (sf (11.0f)));
            auto hudRect = plotArea.reduced (sx (8));
            auto lRect   = hudRect.removeFromTop (sx (16));
            auto rRect   = hudRect.removeFromTop (sx (16));
            g.setColour (WTColors::analysis);
            g.drawText (lLine, lRect, juce::Justification::topRight, false);
            g.setColour (WTColors::analysis_R);
            g.drawText (rLine, rRect, juce::Justification::topRight, false);
        }
    }
}

// (FR sweep capture now renders through the shared SweepView panel.)
