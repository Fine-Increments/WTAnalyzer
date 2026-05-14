/*
  ==============================================================================

    SpectrumDisplay.cpp

  ==============================================================================
*/

#include "SpectrumDisplay.h"
#include "Colors.h"
#include "Analyses/FrequencyResponse.h"

SpectrumDisplay::SpectrumDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);
    startTimerHz (30);

    // Initialise viewMaxFreq to the processor's current sample rate / 2.
    // It will be re-clamped on every paint in case the host changes sample
    // rate while the editor is open.
    viewMaxFreq = processor.currentSampleRate.load (std::memory_order_relaxed) * 0.5f;
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
}

void SpectrumDisplay::setUiScale (float newScale) noexcept
{
    uiScale = newScale;
}

void SpectrumDisplay::timerCallback()
{
    repaint();
}

juce::Rectangle<int> SpectrumDisplay::getPlotArea() const noexcept
{
    auto fullArea = getLocalBounds();
    const int dbGutter   = sx (34);
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
    const int dbGutterRight  = sx (34);
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

    const int dbGutter   = sx (34);
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
    const float dbRange  = viewMaxDb - viewMinDb;

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

    const std::array<float, 6> kFreqGrid { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f };
    for (float f : kFreqGrid)
    {
        if (f < viewMinFreq || f > viewMaxFreq) continue;
        const float x = freqToX (f);
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
    }

    // Uniform 12 dB stepping throughout - matches the convention used by
    // EQ-focused analyzers (Pro-Q, Plugin Doctor) and the "doubling rule"
    // mental model audio engineers think in.
    const std::array<float, 11> kDbGrid {
        -72.0f, -60.0f, -48.0f, -36.0f, -24.0f, -12.0f,
          0.0f,  12.0f,  24.0f,  36.0f,  48.0f
    };
    for (float db : kDbGrid)
    {
        if (db < viewMinDb || db > viewMaxDb) continue;
        const float y = dbToY (db);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    // Axis labels.
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 10> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,   "100" },
        { 200.0f,   "200" }, { 500.0f,   "500"  }, { 1000.0f,  "1k"  },
        { 2000.0f,  "2k"  }, { 5000.0f,  "5k"   }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    }};
    for (auto label : kFreqLabels)
    {
        if (label.hz < viewMinFreq || label.hz > viewMaxFreq) continue;
        const float x = freqToX (label.hz);
        const int textWidth = sx (32);
        juce::Rectangle<int> r ((int) x - textWidth / 2,
                                labelGutterBottom.getY(),
                                textWidth,
                                labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    struct DbLabel { float db; const char* text; };
    const std::array<DbLabel, 11> kDbLabels {{
        { 48.0f,  "+48" }, { 36.0f,  "+36" }, { 24.0f,  "+24" },
        { 12.0f,  "+12" }, { 0.0f,    "0"  }, { -12.0f, "-12" },
        { -24.0f, "-24" }, { -36.0f, "-36" }, { -48.0f, "-48" },
        { -60.0f, "-60" }, { -72.0f, "-72" }
    }};
    for (auto label : kDbLabels)
    {
        if (label.db < viewMinDb || label.db > viewMaxDb) continue;
        const float y = dbToY (label.db);
        const int textHeight = sx (12);
        juce::Rectangle<int> r (labelGutterLeft.getX(),
                                (int) y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4),
                                textHeight);
        g.drawText (label.text, r, juce::Justification::centredRight, false);
    }

    // Restrict trace drawing to the plot area so curves outside the visible
    // dB range get clipped rather than drawn over the label gutters.
    {
        juce::Graphics::ScopedSaveState clipScope (g);
        g.reduceClipRegion (plotArea);

        auto plotTrace = [&] (const std::array<float, WTAnalyzerAudioProcessor::kSpectrumBins>& spec,
                              juce::Colour colour)
        {
            const int   N            = WTAnalyzerAudioProcessor::kSpectrumBins;
            const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

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

        plotTrace (processor.preSpectrumDb,  WTColors::preEffect);
        plotTrace (processor.postSpectrumDb, WTColors::postEffect);

        // When FrequencyResponse mode is active, overlay the transfer function
        // trace. Bins flagged as "no measurement" (pre too quiet) break the path
        // so the curve doesn't fake a value where none exists.
        const int activeAnalysisIdx = (int) *processor.apvts.getRawParameterValue ("activeAnalysis");
        if (activeAnalysisIdx == (int) WTAnalyzerAudioProcessor::AnalysisMode::FrequencyResponse)
        {
            const auto& response = processor.frequencyResponse.getResponseDb();
            const int   N            = (int) response.size();
            const float binFreqScale = sr / (float) WTAnalyzerAudioProcessor::kSpectrumFftSize;

            juce::Path path;
            bool       hasOpenSubPath = false;

            for (int bin = 1; bin < N; ++bin)
            {
                const float f = (float) bin * binFreqScale;
                if (f < viewMinFreq) continue;
                if (f > viewMaxFreq) break;

                const float db = response[(size_t) bin];

                if (db <= FrequencyResponse::kNoMeasurementDb + 0.5f)
                {
                    hasOpenSubPath = false;
                    continue;
                }

                const float clampedDb = juce::jlimit (viewMinDb, viewMaxDb, db);
                const float x = freqToX (f);
                const float y = dbToY (clampedDb);

                if (! hasOpenSubPath) { path.startNewSubPath (x, y); hasOpenSubPath = true; }
                else                  { path.lineTo          (x, y); }
            }

            g.setColour (WTColors::frequencyResponse);
            g.strokePath (path, juce::PathStrokeType (sf (1.6f)));
        }
    }
}
