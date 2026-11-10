#include "PluginEditor.h"

namespace
{
const juce::Colour kBackground (0xff15171a), kPanel (0xff1f2327), kLine (0xff3a3f45), kText (0xffd7dade), kDim (0xff9aa0a6),
                   kAccent (0xffe0a030), kStepOn (0xffe0a030), kStepOff (0xff2b3036), kStepBeat (0xff353b42), kPlay (0xff6fb7ff);

void styleCaption (juce::Label& l, const juce::String& text, float size = 13.0f, bool bold = false)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, kText);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setInterceptsMouseClicks (false, false);
}

void styleToggle (juce::TextButton& b, juce::Colour onColour)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, kStepOff);
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId, kText);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}
}

// ================================================================ VoiceStrip
VoiceStrip::VoiceStrip (ELXARTwoProcessor& p, int stripIndex) : proc (p), strip (stripIndex)
{
    const juce::String prefix = "v" + juce::String (strip + 1) + "_";
    styleToggle (mute, juce::Colour (0xffd9534f));
    styleToggle (solo, juce::Colour (0xfff0d040));
    styleToggle (toFx, juce::Colour (0xff6fb7ff));
    mute.setTooltip ("Silence this sound");
    solo.setTooltip ("Hear only the soloed sounds");
    toFx.setTooltip ("Send this whole sound through the effect");
    hit.setTooltip ("Play this sound once");
    for (auto* b : { &mute, &solo, &toFx, &hit })
        addAndMakeVisible (*b);
    muteAttachment = std::make_unique<ButtonAttachment> (proc.apvts, prefix + "mute", mute);
    soloAttachment = std::make_unique<ButtonAttachment> (proc.apvts, prefix + "solo", solo);
    fxAttachment = std::make_unique<ButtonAttachment> (proc.apvts, prefix + "fx", toFx);

    // strip 4 is the hi-hat: "Hit" plays the closed hat (row 4); other strips map to their own row
    const int row = strip < 4 ? strip : (strip == 4 ? 4 : 6);
    hit.onClick = [this, row] { proc.auditionRow (row); };

    struct Def { const char* name; juce::String id; };
    std::vector<Def> defs = { { "Pitch", prefix + "pitch" }, { "Decay", prefix + "decay" }, { "Tone", prefix + "tone" },
                              { "Level", prefix + "level" }, { "Pan", prefix + "pan" } };
    if (strip == 4)
        defs.insert (defs.begin() + 2, { "Open", "hat_open_decay" });   // decay of the open hi-hat
    for (const auto& d : defs)
    {
        auto* s = sliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow));
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 34, 16);
        s->setColour (juce::Slider::thumbColourId, kAccent);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxTextColourId, kDim);
        addAndMakeVisible (*s);
        sliderAttachments.add (new SliderAttachment (proc.apvts, d.id, *s));
        auto* l = labels.add (new juce::Label());
        styleCaption (*l, d.name, 11.0f);
        l->setJustificationType (juce::Justification::centred);
        l->setMinimumHorizontalScale (0.5f);
        l->setBorderSize (juce::BorderSize<int> (0));
        addAndMakeVisible (*l);
    }
}

void VoiceStrip::paint (juce::Graphics& g)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (ELXARTwoProcessor::kStripNames[(size_t) strip], getLocalBounds().removeFromTop (24), juce::Justification::centred);
}

void VoiceStrip::resized()
{
    auto area = getLocalBounds().reduced (5);
    area.removeFromTop (20);
    auto buttons = area.removeFromTop (22);
    const int bw = buttons.getWidth() / 4;
    mute.setBounds (buttons.removeFromLeft (bw).reduced (1));
    solo.setBounds (buttons.removeFromLeft (bw).reduced (1));
    toFx.setBounds (buttons.removeFromLeft (bw).reduced (1));
    hit.setBounds (buttons.reduced (1));
    area.removeFromTop (4);
    auto labelRow = area.removeFromTop (14);
    const int n = sliders.size();
    const int sw = area.getWidth() / juce::jmax (1, n);
    for (int i = 0; i < n; ++i)
    {
        labels[i]->setBounds (labelRow.removeFromLeft (sw));
        sliders[i]->setBounds (area.removeFromLeft (sw));
    }
}

// ================================================================ StepGrid
juce::Rectangle<int> StepGrid::cellBounds (int row, int step) const
{
    const int w = (getWidth() - kLabelWidth) / ELXARTwoProcessor::kNumSteps;
    const int h = getHeight() / ELXARTwoProcessor::kNumRows;
    return { kLabelWidth + step * w, row * h, w, h };
}

bool StepGrid::cellAt (juce::Point<int> p, int& row, int& step) const
{
    const int w = (getWidth() - kLabelWidth) / ELXARTwoProcessor::kNumSteps;
    const int h = getHeight() / ELXARTwoProcessor::kNumRows;
    if (w <= 0 || h <= 0 || p.x < kLabelWidth || p.y < 0) return false;
    step = (p.x - kLabelWidth) / w;
    row = p.y / h;
    return step >= 0 && step < ELXARTwoProcessor::kNumSteps && row >= 0 && row < ELXARTwoProcessor::kNumRows;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int pattern = proc.getCurrentPattern();
    g.setFont (juce::FontOptions (12.0f));
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
    {
        g.setColour (kText);
        g.drawText (ELXARTwoProcessor::kRowNames[(size_t) row], 0, cellBounds (row, 0).getY(), kLabelWidth - 6, cellBounds (row, 0).getHeight(),
                    juce::Justification::centredRight);
        for (int step = 0; step < ELXARTwoProcessor::kNumSteps; ++step)
        {
            const auto c = cellBounds (row, step).reduced (2).toFloat();
            const bool on = proc.getStep (pattern, row, step);
            g.setColour (on ? kStepOn : ((step % 4) == 0 ? kStepBeat : kStepOff));
            g.fillRoundedRectangle (c, 3.0f);
            if (step == playingStep)
            {
                g.setColour (kPlay);
                g.drawRoundedRectangle (c, 3.0f, 2.0f);
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int row = 0, step = 0;
    if (! cellAt (e.getPosition(), row, step)) return;
    paintValue = ! proc.getStep (proc.getCurrentPattern(), row, step);
    proc.setStep (proc.getCurrentPattern(), row, step, paintValue);
    lastRow = row; lastStep = step;
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)   // drag across squares to switch several on or off
{
    int row = 0, step = 0;
    if (! cellAt (e.getPosition(), row, step) || (row == lastRow && step == lastStep)) return;
    proc.setStep (proc.getCurrentPattern(), row, step, paintValue);
    lastRow = row; lastStep = step;
    repaint();
}

// ================================================================ DragMidiBox
void DragMidiBox::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff2d4a66));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (kPlay);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 1.5f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText ("DRAG MIDI OUT (test)\npress here and drag onto a Logic track", getLocalBounds().reduced (4), juce::Justification::centred, 2);
}

void DragMidiBox::mouseDown (const juce::MouseEvent&) { dragStarted = false; }

void DragMidiBox::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto file = proc.writePatternMidiFile (proc.getCurrentPattern());
    if (! file.existsAsFile())
    {
        if (onStatus) onStatus ("Drag test: could not write the MIDI file.");
        return;
    }
    if (onStatus) onStatus ("Drag test: dragging " + file.getFileName() + " ...");
    auto safeThis = juce::Component::SafePointer<DragMidiBox> (this);
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
        [safeThis] { if (safeThis != nullptr && safeThis->onStatus) safeThis->onStatus ("Drag test: drag finished."); });
    if (! started && onStatus) onStatus ("Drag test: the Mac refused to start the drag.");
}

void DragMidiBox::mouseUp (const juce::MouseEvent&) { dragStarted = false; }

// ================================================================ Editor
void ELXARTwoEditor::setUpHorizontalSlider (juce::Slider& s, juce::Label& l, const juce::String& text, const juce::String& paramId,
                                            std::unique_ptr<SliderAttachment>& attachment)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    s.setColour (juce::Slider::thumbColourId, kAccent);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, kDim);
    addAndMakeVisible (s);
    styleCaption (l, text, 12.0f);
    addAndMakeVisible (l);
    attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, s);
}

ELXARTwoEditor::ELXARTwoEditor (ELXARTwoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), grid (p), dragBox (p)
{
    // ---- top bar ----
    styleCaption (title, juce::String ("ELXAR-Two  ") + ELXAR_VERSION_STRING + "  (Milestone 1, rough interface)", 16.0f, true);
    addAndMakeVisible (title);
    styleCaption (tempoLabel, "Tempo: -", 13.0f);
    addAndMakeVisible (tempoLabel);
    styleCaption (statusLabel, "", 12.0f);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (statusLabel);
    styleCaption (patternCaption, "Pattern", 13.0f);
    addAndMakeVisible (patternCaption);
    for (int i = 1; i <= ELXARTwoProcessor::kNumPatterns; ++i) patternBox.addItem (juce::String (i), i);
    patternBox.setSelectedId (proc.getCurrentPattern() + 1, juce::dontSendNotification);
    patternBox.onChange = [this] { proc.setCurrentPattern (patternBox.getSelectedId() - 1); grid.repaint(); };
    addAndMakeVisible (patternBox);
    styleCaption (runCaption, "Step grid runs", 13.0f);
    addAndMakeVisible (runCaption);
    runBox.addItem ("With Logic transport", 1);
    runBox.addItem ("Always", 2);
    addAndMakeVisible (runBox);
    runAttachment = std::make_unique<ComboAttachment> (proc.apvts, "run_mode", runBox);

    // ---- voices and grid ----
    for (int s = 0; s < ELXARTwoProcessor::kNumStrips; ++s)
        addAndMakeVisible (strips.add (new VoiceStrip (proc, s)));
    addAndMakeVisible (grid);

    // ---- pattern generator ----
    styleCaption (genCaption, "PATTERN GENERATOR", 12.0f, true);
    addAndMakeVisible (genCaption);
    setUpHorizontalSlider (density, densityLabel, "Density", "gen_density", densityAttachment);
    setUpHorizontalSlider (variation, variationLabel, "Variation", "gen_variation", variationAttachment);
    setUpHorizontalSlider (euclid, euclidLabel, "Euclid hits (Drum 1)", "gen_euclid", euclidAttachment);
    generate.onClick = [this]
    {
        proc.generatePattern (proc.getCurrentPattern(), (float) density.getValue() / 100.0f, (float) variation.getValue() / 100.0f, (int) euclid.getValue());
        grid.repaint();
    };
    clear.onClick = [this] { proc.clearPattern (proc.getCurrentPattern()); grid.repaint(); };
    revealMidi.onClick = [this]
    {
        const auto file = proc.writePatternMidiFile (proc.getCurrentPattern());
        if (file.existsAsFile()) { file.revealToUser(); dragStatus.setText ("MIDI file written: " + file.getFileName(), juce::dontSendNotification); }
        else dragStatus.setText ("Could not write the MIDI file.", juce::dontSendNotification);
    };
    for (auto* b : { &generate, &clear, &revealMidi }) addAndMakeVisible (*b);
    dragBox.onStatus = [this] (const juce::String& s) { dragStatus.setText (s, juce::dontSendNotification); };
    addAndMakeVisible (dragBox);
    styleCaption (dragStatus, "", 12.0f);
    dragStatus.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (dragStatus);

    // ---- effect and master ----
    styleCaption (fxCaption, "EFFECT (one at a time, for the sounds with \"To FX\" on)  AND MASTER", 12.0f, true);
    addAndMakeVisible (fxCaption);
    styleCaption (fxTypeLabel, "Effect", 12.0f);
    addAndMakeVisible (fxTypeLabel);
    fxType.addItemList ({ "Off", "Drive", "Ring mod", "Compressor", "Delay", "Reverb" }, 1);
    addAndMakeVisible (fxType);
    fxTypeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "fx_type", fxType);
    setUpHorizontalSlider (fxMix, fxMixLabel, "Dry / wet", "fx_mix", fxMixAttachment);
    for (int i = 0; i < 4; ++i)
        setUpHorizontalSlider (fxKnobs[(size_t) i], fxKnobLabels[(size_t) i], "-", "fx_p" + juce::String (i + 1), fxKnobAttachments[(size_t) i]);
    setUpHorizontalSlider (master, masterLabel, "Master level", "master", masterAttachment);

    updateFxLabels();
    setSize (1040, 720);
    startTimerHz (30);
}

ELXARTwoEditor::~ELXARTwoEditor() { stopTimer(); }

void ELXARTwoEditor::updateFxLabels()
{
    static const char* const names[6][4] = {
        { "-", "-", "-", "-" },
        { "Drive", "Color", "Tone", "Volume" },
        { "Frequency", "-", "-", "-" },
        { "Attack", "Release", "Threshold", "Gain" },
        { "Time", "Range", "Tone", "Feedback" },
        { "Pre-delay", "Decay", "Tone", "Reflection" } };
    const int type = juce::jlimit (0, 5, fxType.getSelectedId() - 1);
    if (type == shownFxType) return;
    shownFxType = type;
    for (int i = 0; i < 4; ++i)
    {
        const juce::String n (names[type][i]);
        fxKnobLabels[(size_t) i].setText (n, juce::dontSendNotification);
        fxKnobs[(size_t) i].setEnabled (n != "-");
        fxKnobs[(size_t) i].setAlpha (n != "-" ? 1.0f : 0.35f);
    }
    fxMix.setEnabled (type != 0);
    fxMix.setAlpha (type != 0 ? 1.0f : 0.35f);
}

void ELXARTwoEditor::timerCallback()
{
    grid.setPlayingStep (proc.getPlayingStep());
    updateFxLabels();
    if (patternBox.getSelectedId() != proc.getCurrentPattern() + 1)
    {
        patternBox.setSelectedId (proc.getCurrentPattern() + 1, juce::dontSendNotification);
        grid.repaint();
    }
    juce::String tempo = "Tempo: " + juce::String (proc.getDisplayBpm(), 1) + (proc.hasHostTempo() ? " (from Logic)" : " (Logic gave no tempo, using 120)");
    tempo << (proc.isHostPlaying() ? "   Logic: playing" : "   Logic: stopped");
    if (tempoLabel.getText() != tempo) tempoLabel.setText (tempo, juce::dontSendNotification);

    const juce::String status = proc.engineAvailable()
        ? "Keys: C1 Drum 1, D1 Drum 2, E1 Drum 3, F1 Snare, G1 Hat closed, A1 Hat open, B1 Cymbal"
        : "ANOTHER ELXAR-Two IS ALREADY OPEN. Only the first one makes sound in this version.";
    if (statusLabel.getText() != status)
    {
        statusLabel.setColour (juce::Label::textColourId, proc.engineAvailable() ? kDim : juce::Colour (0xffff6b6b));
        statusLabel.setText (status, juce::dontSendNotification);
    }
}

void ELXARTwoEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Unofficial, modified port of the LXR-02 sound engine. Not affiliated with or approved by Erica Synths.",
                getLocalBounds().removeFromBottom (18).reduced (10, 0), juce::Justification::centredLeft);
}

void ELXARTwoEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromBottom (14);

    // top bar (two lines)
    auto top = area.removeFromTop (26);
    title.setBounds (top.removeFromLeft (420));
    runBox.setBounds (top.removeFromRight (190).reduced (0, 1));
    runCaption.setBounds (top.removeFromRight (100));
    top.removeFromRight (16);
    patternBox.setBounds (top.removeFromRight (64).reduced (0, 1));
    patternCaption.setBounds (top.removeFromRight (56));
    auto info = area.removeFromTop (20);
    tempoLabel.setBounds (info.removeFromLeft (360));
    statusLabel.setBounds (info);
    area.removeFromTop (6);

    // six voice strips
    auto stripArea = area.removeFromTop (250);
    const int gap = 6;
    const float unit = (float) (stripArea.getWidth() - gap * (strips.size() - 1)) / 31.0f;   // 5 sliders per strip, 6 on the hi-hat
    float x = (float) stripArea.getX();
    for (int i = 0; i < strips.size(); ++i)
    {
        const float w = unit * (i == 4 ? 6.0f : 5.0f);
        strips[i]->setBounds (juce::roundToInt (x), stripArea.getY(), juce::roundToInt (w), stripArea.getHeight());
        x += w + (float) gap;
    }
    area.removeFromTop (8);

    // step grid
    grid.setBounds (area.removeFromTop (7 * 24));
    area.removeFromTop (8);

    // pattern generator
    genCaption.setBounds (area.removeFromTop (18));
    auto gen = area.removeFromTop (72);
    auto genLeft = gen.removeFromLeft (430);
    auto row = [] (juce::Rectangle<int>& from, juce::Label& l, juce::Slider& s, int labelWidth)
    {
        auto r = from.removeFromTop (24);
        l.setBounds (r.removeFromLeft (labelWidth));
        s.setBounds (r);
    };
    row (genLeft, densityLabel, density, 140);
    row (genLeft, variationLabel, variation, 140);
    row (genLeft, euclidLabel, euclid, 140);
    gen.removeFromLeft (12);
    auto genButtons = gen.removeFromLeft (110);
    generate.setBounds (genButtons.removeFromTop (34).reduced (0, 2));
    clear.setBounds (genButtons.removeFromTop (34).reduced (0, 2));
    gen.removeFromLeft (12);
    auto dragArea = gen.removeFromLeft (270);
    dragBox.setBounds (dragArea.reduced (0, 4));
    gen.removeFromLeft (12);
    revealMidi.setBounds (gen.removeFromTop (34).reduced (0, 2));
    dragStatus.setBounds (gen);
    area.removeFromTop (8);

    // effect and master
    fxCaption.setBounds (area.removeFromTop (18));
    auto fx = area;
    auto fxLeft = fx.removeFromLeft (330);
    {
        auto r = fxLeft.removeFromTop (24);
        fxTypeLabel.setBounds (r.removeFromLeft (80));
        fxType.setBounds (r.removeFromLeft (160).reduced (0, 1));
    }
    row (fxLeft, fxMixLabel, fxMix, 80);
    fxLeft.removeFromTop (8);
    row (fxLeft, masterLabel, master, 80);
    fx.removeFromLeft (20);
    auto fxMid = fx.removeFromLeft (330);
    row (fxMid, fxKnobLabels[0], fxKnobs[0], 80);
    row (fxMid, fxKnobLabels[1], fxKnobs[1], 80);
    fx.removeFromLeft (20);
    row (fx, fxKnobLabels[2], fxKnobs[2], 80);
    row (fx, fxKnobLabels[3], fxKnobs[3], 80);
}
