#include "PluginEditor.h"

ELXARTwoEditor::ELXARTwoEditor (ELXARTwoProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (520, 260);
}

void ELXARTwoEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("ELXAR-Two", getLocalBounds().removeFromTop (110), juce::Justification::centredBottom);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText (juce::String ("Unofficial, modified port based on the Erica Synths LXR-02 firmware (GPL-3.0).\n"
                                    "Not affiliated with or approved by Erica Synths.\n\n"
                                    "Milestone 0 shell, version ") + ELXAR_VERSION_STRING
                      + ".\nPlays a placeholder tone on MIDI notes; the LXR-02 engine is not connected yet.",
                      getLocalBounds().reduced (24).withTrimmedTop (120), juce::Justification::centredTop, 6);
}
