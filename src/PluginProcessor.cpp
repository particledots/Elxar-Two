#include "PluginProcessor.h"
#include "PluginEditor.h"

ELXARTwoProcessor::ELXARTwoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ELXARTwoProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    phase = 0.0;
    level = 0.0f;
    noteOn = false;
}

bool ELXARTwoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ELXARTwoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        for (; pos < end; ++pos)
        {
            const float target = noteOn ? 0.2f : 0.0f;
            level += (target - level) * 0.002f;   // short click-free fade in/out
            const float s = level * (float) std::sin (phase);
            phase += phaseInc;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            left[pos] = s;
            if (right != nullptr)
                right[pos] = s;
        }
    };

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        renderTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        if (msg.isNoteOn())
        {
            phaseInc = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber()) / sampleRateHz;
            noteOn = true;
        }
        else if (msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            noteOn = false;
        }
    }
    renderTo (numSamples);
}

juce::AudioProcessorEditor* ELXARTwoProcessor::createEditor()
{
    return new ELXARTwoEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ELXARTwoProcessor();
}
