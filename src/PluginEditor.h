#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SoundParams.h"
#include "ElxarLook.h"

// The plugin window. Original layout and look (src/ElxarLook.h), not a copy of the hardware panel. The window is
// drawn at one fixed layout size and scaled as a whole, so it can be resized.

// One voice panel: name, Mute / Solo / To-effect buttons, a "Hit" button, and five (hi-hat: six) sliders.
class VoiceStrip : public juce::Component
{
public:
    VoiceStrip (ELXARTwoProcessor&, int stripIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setAudible (bool);   // false = muted, or another voice is soloed: the strip is shown dimmed

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ELXARTwoProcessor& proc;
    int strip;
    bool audible = true;
    juce::TextButton mute { "Mute" }, solo { "Solo" }, toFx { "To FX" }, hit { "Hit" };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    std::unique_ptr<ButtonAttachment> muteAttachment, soloAttachment, fxAttachment;
};

// The step grid: one row per sound, 16 steps (one bar) at a time. Click a square to switch a step on or off;
// the square clicked last is the "selected step" that the step editor below the grid works on.
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (ELXARTwoProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    void setBar (int b) { if (b != bar) { bar = juce::jlimit (0, ELXARTwoProcessor::kNumBars - 1, b); repaint(); } }
    int  getBar() const { return bar; }
    int  getSelectedRow() const { return selectedRow; }
    int  getSelectedStep() const { return selectedStep; }   // 0..63
    void updatePositions();                                  // call from the timer
    juce::Rectangle<int> rowBounds (int row) const;
    std::function<void()> onSelectionChanged, onEdited;

    static constexpr int kLabelWidth = 92;

private:
    bool cellAt (juce::Point<int>, int& row, int& step) const;
    juce::Rectangle<int> cellBounds (int row, int column) const;
    void select (int row, int step);

    ELXARTwoProcessor& proc;
    int bar = 0, selectedRow = 0, selectedStep = 0;
    std::array<int, (size_t) ELXARTwoProcessor::kNumRows> shownPosition {};
    bool paintValue = true, painting = false;
    int lastRow = -1, lastStep = -1;
};

// Press on this box and drag it onto a Logic track to drop the current pattern as a MIDI region.
class DragMidiBox : public juce::Component
{
public:
    explicit DragMidiBox (ELXARTwoProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;
    std::function<int()> getBars;   // 0 = automatic

private:
    ELXARTwoProcessor& proc;
    bool dragStarted = false;
};

// The "Pattern" page: bar buttons, shuffle, the step grid with a length per row, the step editor, the pattern
// generator and the MIDI file box.
class PatternPage : public juce::Component
{
public:
    explicit PatternPage (ELXARTwoProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();           // read everything again from the processor (pattern changed, project loaded)
    void timerTick();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    void refreshStepEditor();
    void colourStepEditor (int row);
    void writeStepFromEditor();
    void fillTargetBox (juce::ComboBox&);
    void setUpSmallSlider (juce::Slider&, juce::Label&, const juce::String& text, double lo, double hi, double value);

    ELXARTwoProcessor& proc;
    int shownPattern = -1, colouredRow = -1;
    bool updating = false;

    juce::Label barCaption, shuffleLabel, lengthCaption;
    std::array<juce::TextButton, (size_t) ELXARTwoProcessor::kNumBars> barButtons;
    juce::Slider shuffle;
    juce::TextButton copyButton { "Copy pattern" }, pasteButton { "Paste" }, clearButton { "Clear pattern" };
    StepGrid grid;
    std::array<juce::Slider, (size_t) ELXARTwoProcessor::kNumRows> lengthSliders;

    // step editor
    juce::Label stepCaption, velLabel, probLabel, noteLabel, flamLabel, shiftLabel, auto1Label, auto2Label;
    juce::Slider velocity, probability, note, auto1Value, auto2Value;
    juce::ComboBox flam, shift, auto1Target, auto2Target;

    // generator and MIDI file
    juce::Label genCaption, densityLabel, variationLabel, kickLabel, euclidCaption, eLenLabel, eHitsLabel, eRotLabel, midiCaption, barsLabel, dragStatus;
    juce::Slider density, variation, kickHits, eLen, eHits, eRot;
    std::unique_ptr<SliderAttachment> densityAttachment, variationAttachment, kickAttachment;
    juce::TextButton generate { "Generate beat" }, fillRow { "Fill selected row" }, revealMidi { "Show in Finder" };
    juce::ComboBox midiBars;
    DragMidiBox dragBox;
};

// One sound parameter on the "Sound" page: a name and a slider, a list or an on/off button.
class KitControl : public juce::Component
{
public:
    KitControl (ELXARTwoProcessor&, const elxar::ParamInfo&, int engineVoice, juce::Colour accent);
    void resized() override;
    void refresh();   // show the processor's current value

private:
    ELXARTwoProcessor& proc;
    elxar::ParamInfo info;
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    bool updating = false;
};

// The "Sound" page: every sound parameter of one voice, and the kit slots.
class SoundPage : public juce::Component
{
public:
    explicit SoundPage (ELXARTwoProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh (bool alsoKitSlots);

private:
    void showVoice (int stripIndex);
    void updateSlotBox();

    ELXARTwoProcessor& proc;
    int shownStrip = -1;
    std::array<juce::TextButton, (size_t) ELXARTwoProcessor::kNumStrips> voiceButtons;
    juce::Label kitCaption, kitStatus;
    juce::ComboBox slotBox;
    juce::TextButton storeButton { "Store kit" }, recallButton { "Recall kit" }, initButton { "Init kit" };
    juce::OwnedArray<KitControl> controls;
    struct Header { juce::String text; juce::Rectangle<int> bounds; };
    std::vector<Header> headers;
};

class ELXARTwoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ELXARTwoEditor (ELXARTwoProcessor&);
    ~ELXARTwoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 1100, kBaseHeight = 820;   // the layout size; the window scales this

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Everything on screen lives in this one component, which is scaled to fit the window.
    struct Content : public juce::Component
    {
        explicit Content (ELXARTwoEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void resized() override { owner.layoutContent(); }
        ELXARTwoEditor& owner;
    };

    void timerCallback() override;
    void layoutContent();
    void updateFxLabels();
    void setUpHorizontalSlider (juce::Slider&, juce::Label&, const juce::String& text, const juce::String& paramId,
                                std::unique_ptr<SliderAttachment>&);

    ELXARTwoProcessor& proc;
    elxarlook::Look look;     // declared before everything that is drawn with it
    Content content { *this };
    juce::TooltipWindow tooltipWindow { this };

    // top bar
    juce::Label title, tempoLabel, statusLabel, patternCaption, playingLabel, runCaption, sizeCaption;
    juce::ComboBox patternBox, runBox, sizeBox;
    std::unique_ptr<ComboAttachment> runAttachment;

    juce::OwnedArray<VoiceStrip> strips;
    PatternPage patternPage;
    SoundPage soundPage;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // effect and master
    juce::Label fxCaption, fxTypeLabel, fxMixLabel, fxKindLabel, masterLabel;
    juce::ComboBox fxType, fxKind;
    std::unique_ptr<ComboAttachment> fxTypeAttachment;
    juce::Slider fxMix, master;
    std::unique_ptr<SliderAttachment> fxMixAttachment, masterAttachment;
    std::array<juce::Slider, 4> fxKnobs;
    std::array<juce::Label, 4> fxKnobLabels;
    std::array<std::unique_ptr<SliderAttachment>, 4> fxKnobAttachments;
    int shownFxType = -1, fxKindParam = -1;
    juce::Rectangle<int> fxPanel, pagePanel;
    std::array<std::atomic<float>*, (size_t) ELXARTwoProcessor::kNumStrips> muteValues {}, soloValues {};
    bool updatingFxKind = false;

    uint32_t seenModelVersion = 0;
    int tick = 0;
    bool settingSize = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ELXARTwoEditor)
};
