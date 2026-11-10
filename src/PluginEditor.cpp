#include "PluginEditor.h"

ELXARTwoEditor::ELXARTwoEditor (ELXARTwoProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (520, 300);
}

void ELXARTwoEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171a));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("ELXAR-Two", juce::Rectangle<int> (0, 40, getWidth(), 44), juce::Justification::centred);

    g.setColour (juce::Colour (0xffc8ccd0));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("Unofficial port of the Erica Synths LXR-02 engine", juce::Rectangle<int> (0, 90, getWidth(), 22), juce::Justification::centred);

    g.setColour (juce::Colour (0xff3a3f45));
    g.drawHorizontalLine (136, 40.0f, (float) getWidth() - 40.0f);

    g.setColour (juce::Colour (0xff9aa0a6));
    g.setFont (juce::FontOptions (13.0f));
    const juce::String lines[] = {
        "Modified version. Not affiliated with or approved by Erica Synths.",
        juce::String ("Milestone 0 shell, version ") + ELXAR_VERSION_STRING,
        "Plays a placeholder tone on MIDI notes.",
        "The LXR-02 sound engine is not connected yet."
    };
    int y = 152;
    for (const auto& line : lines)
    {
        g.drawText (line, juce::Rectangle<int> (24, y, getWidth() - 48, 20), juce::Justification::centred);
        y += 26;
    }
}
