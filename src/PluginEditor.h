#pragma once
#include "PluginProcessor.h"

// Milestone 0 placeholder window. The real (original-design) interface comes later.
class ELXARTwoEditor : public juce::AudioProcessorEditor
{
public:
    explicit ELXARTwoEditor (ELXARTwoProcessor&);
    ~ELXARTwoEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoEditor)
};
