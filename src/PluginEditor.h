#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Milestone 1: a rough, plain interface (original layout, not a copy of the hardware panel). Styling comes later.

// One voice panel: name, Mute / Solo / To-effect buttons, a "Hit" button, and five (hi-hat: six) sliders.
class VoiceStrip : public juce::Component
{
public:
    VoiceStrip (ELXARTwoProcessor&, int stripIndex);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ELXARTwoProcessor& proc;
    int strip;
    juce::TextButton mute { "Mute" }, solo { "Solo" }, toFx { "To FX" }, hit { "Hit" };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment, fxAttachment;
};

// The 16-step grid: one row per sound, click a square to switch a step on or off.
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (ELXARTwoProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void setPlayingStep (int s) { if (s != playingStep) { playingStep = s; repaint(); } }

private:
    static constexpr int kLabelWidth = 92;
    bool cellAt (juce::Point<int>, int& row, int& step) const;
    juce::Rectangle<int> cellBounds (int row, int step) const;

    ELXARTwoProcessor& proc;
    int playingStep = -1;
    bool paintValue = true;
    int lastRow = -1, lastStep = -1;
};

// The drag test: press on this box and drag it onto a Logic track to drop the current pattern as a MIDI region.
class DragMidiBox : public juce::Component
{
public:
    explicit DragMidiBox (ELXARTwoProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;

private:
    ELXARTwoProcessor& proc;
    bool dragStarted = false;
};

class ELXARTwoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ELXARTwoEditor (ELXARTwoProcessor&);
    ~ELXARTwoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void updateFxLabels();
    void setUpHorizontalSlider (juce::Slider&, juce::Label&, const juce::String& text, const juce::String& paramId,
                                std::unique_ptr<SliderAttachment>&);

    ELXARTwoProcessor& proc;
    juce::TooltipWindow tooltipWindow { this };

    // top bar
    juce::Label title, tempoLabel, statusLabel, patternCaption, runCaption;
    juce::ComboBox patternBox, runBox;
    std::unique_ptr<ComboAttachment> runAttachment;

    juce::OwnedArray<VoiceStrip> strips;
    StepGrid grid;

    // pattern generator
    juce::Label genCaption, densityLabel, variationLabel, euclidLabel, dragStatus;
    juce::Slider density, variation, euclid;
    std::unique_ptr<SliderAttachment> densityAttachment, variationAttachment, euclidAttachment;
    juce::TextButton generate { "Generate" }, clear { "Clear" }, revealMidi { "Show MIDI file in Finder" };
    DragMidiBox dragBox;

    // effect and master
    juce::Label fxCaption, fxTypeLabel, fxMixLabel, masterLabel;
    juce::ComboBox fxType;
    std::unique_ptr<ComboAttachment> fxTypeAttachment;
    juce::Slider fxMix, master;
    std::unique_ptr<SliderAttachment> fxMixAttachment, masterAttachment;
    std::array<juce::Slider, 4> fxKnobs;
    std::array<juce::Label, 4> fxKnobLabels;
    std::array<std::unique_ptr<SliderAttachment>, 4> fxKnobAttachments;
    int shownFxType = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoEditor)
};
