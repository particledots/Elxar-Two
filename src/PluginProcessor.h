#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// ELXAR-Two, Milestone 0: an empty instrument shell. It loads in Logic as an Audio Unit instrument
// and plays a simple placeholder tone on MIDI notes so loading and MIDI routing can be checked.
// The LXR-02 sound engine is not connected yet.
class ELXARTwoProcessor : public juce::AudioProcessor
{
public:
    ELXARTwoProcessor();
    ~ELXARTwoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ELXAR-Two"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    double sampleRateHz = 44100.0;
    double phase = 0.0;
    double phaseInc = 0.0;
    float level = 0.0f;
    bool noteOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoProcessor)
};
