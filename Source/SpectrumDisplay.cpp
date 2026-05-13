/*
  ==============================================================================

    SpectrumDisplay.cpp

  ==============================================================================
*/

#include "SpectrumDisplay.h"

SpectrumDisplay::SpectrumDisplay (WTAnalyzerAudioProcessor& proc)
    : processor (proc)
{
    setOpaque (true);
    startTimerHz (30);
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

    const float sr = processor.currentSampleRate.load (std::memory_order_relaxed);
    constexpr float kMinFreq = 20.0f;
    const float     kMaxFreq = sr * 0.5f;
    const float     logMin   = std::log10 (kMinFreq);
    const float     logMax   = std::log10 (kMaxFreq);
    const float     logRange = logMax - logMin;

    constexpr float kMinDb = -80.0f;
    constexpr float kMaxDb = 6.0f;
    const float     dbRange = kMaxDb - kMinDb;

    auto freqToX = [&] (float freq) -> float
    {
        const float t = (std::log10 (freq) - logMin) / logRange;
        return (float) plotArea.getX() + t * (float) plotArea.getWidth();
    };

    auto dbToY = [&] (float db) -> float
    {
        const float t = (db - kMinDb) / dbRange;
        return (float) plotArea.getY() + (1.0f - t) * (float) plotArea.getHeight();
    };

    // Grid lines.
    g.setColour (juce::Colour (0xff2a2d32));
    const std::array<float, 6> kFreqGrid { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f };
    for (float f : kFreqGrid)
    {
        if (f < kMinFreq || f > kMaxFreq) continue;
        const float x = freqToX (f);
        g.drawLine (x, (float) plotArea.getY(), x, (float) plotArea.getBottom(), sf (1.0f));
    }
    const std::array<float, 5> kDbGrid { -60.0f, -40.0f, -20.0f, 0.0f, 6.0f };
    for (float db : kDbGrid)
    {
        const float y = dbToY (db);
        g.drawLine ((float) plotArea.getX(), y, (float) plotArea.getRight(), y, sf (1.0f));
    }

    // Axis labels.
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (sf (10.0f)));

    struct FreqLabel { float hz; const char* text; };
    const std::array<FreqLabel, 8> kFreqLabels {{
        { 20.0f,    "20"  }, { 50.0f,    "50"   }, { 100.0f,  "100" },
        { 500.0f,   "500" }, { 1000.0f,  "1k"   }, { 5000.0f, "5k"  },
        { 10000.0f, "10k" }, { 20000.0f, "20k"  }
    }};
    for (auto label : kFreqLabels)
    {
        if (label.hz < kMinFreq || label.hz > kMaxFreq) continue;
        const float x = freqToX (label.hz);
        const int textWidth = sx (32);
        juce::Rectangle<int> r ((int) x - textWidth / 2,
                                labelGutterBottom.getY(),
                                textWidth,
                                labelGutterBottom.getHeight());
        g.drawText (label.text, r, juce::Justification::centredTop, false);
    }

    struct DbLabel { float db; const char* text; };
    const std::array<DbLabel, 5> kDbLabels {{
        { 0.0f,   "0"   }, { -20.0f, "-20" }, { -40.0f, "-40" },
        { -60.0f, "-60" }, { -80.0f, "-80" }
    }};
    for (auto label : kDbLabels)
    {
        const float y = dbToY (label.db);
        const int textHeight = sx (12);
        juce::Rectangle<int> r (labelGutterLeft.getX(),
                                (int) y - textHeight / 2,
                                labelGutterLeft.getWidth() - sx (4),
                                textHeight);
        g.drawText (label.text, r, juce::Justification::centredRight, false);
    }

    // Spectrum traces.
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
            if (f < kMinFreq) continue;
            if (f > kMaxFreq) break;

            const float clampedDb = juce::jlimit (kMinDb, kMaxDb, spec[bin]);
            const float x = freqToX (f);
            const float y = dbToY (clampedDb);

            if (first) { path.startNewSubPath (x, y); first = false; }
            else       { path.lineTo          (x, y); }
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (sf (1.2f)));
    };

    plotTrace (processor.preSpectrumDb,  juce::Colour (0xffe0a050));  // pre  = amber
    plotTrace (processor.postSpectrumDb, juce::Colour (0xff5cd4e8));  // post = cyan
}
