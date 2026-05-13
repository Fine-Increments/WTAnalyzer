/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class WTAnalyzerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    WTAnalyzerAudioProcessorEditor (WTAnalyzerAudioProcessor&);
    ~WTAnalyzerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void commitDelayFromEditor();
    void applyMeasuredLatency (int samples);

    WTAnalyzerAudioProcessor& audioProcessor;

    juce::Label      preDelayLabel;
    juce::TextEditor preDelayEditor;
    juce::Label      preDelaySuffix;
    juce::TextButton autoMeasureButton { "Auto" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WTAnalyzerAudioProcessorEditor)
};
