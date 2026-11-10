#include "PluginEditor.h"

extern "C" {
#include "Parameters.h"
}

namespace
{
using namespace elxarlook;   // the palette and the voice colours (src/ElxarLook.h)

juce::Colour rowColour (int row) { return voiceColour (ELXARTwoProcessor::kRowToStrip[(size_t) juce::jlimit (0, ELXARTwoProcessor::kNumRows - 1, row)]); }

void styleCaption (juce::Label& l, const juce::String& text, float size = 13.0f, bool bold = false)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, kText);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setInterceptsMouseClicks (false, false);
    l.setMinimumHorizontalScale (0.6f);
}

void styleToggle (juce::TextButton& b, juce::Colour onColour)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, kRaised);
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId, kText);
    b.setColour (juce::TextButton::textColourOnId, kOnText);
}

void styleSlider (juce::Slider& s, int textBoxWidth = 40, juce::Colour accent = kNeutral)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, textBoxWidth, 18);
    s.setColour (juce::Slider::thumbColourId, accent);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, kDim);
}

// Fills a list box with the firmware's modulation targets. Item number = position in the firmware's list + 1.
// onlyVoice: 0 = every voice (with a heading per voice), 1..6 = that voice only.
void fillTargets (juce::ComboBox& box, int onlyVoice)
{
    box.clear (juce::dontSendNotification);
    box.addItem ("None", 1);
    int lastVoice = -1;
    for (const auto& t : ELXARTwoProcessor::modTargets())
    {
        if (onlyVoice != 0 && t.voice != onlyVoice) continue;
        if (onlyVoice == 0 && t.voice != lastVoice)
        {
            box.addSectionHeading (t.voice == 0 ? "Effect and all voices" : elxar::voiceName (t.voice - 1));
            lastVoice = t.voice;
        }
        box.addItem (onlyVoice != 0 ? t.name.fromFirstOccurrenceOf (": ", false, false) : t.name, t.index + 1);
    }
}

// Shows the target that a stored list position stands for (several positions can mean the same parameter).
void selectTarget (juce::ComboBox& box, int storedIndex)
{
    const int par = storedIndex > 0 ? elxar_mod_target_param (storedIndex) : 0;
    const int index = par > 0 ? ELXARTwoProcessor::targetIndexForParam (par) : 0;
    box.setSelectedId (box.indexOfItemId (index + 1) >= 0 ? index + 1 : 1, juce::dontSendNotification);
}
}

// ================================================================ VoiceStrip
VoiceStrip::VoiceStrip (ELXARTwoProcessor& p, int stripIndex) : proc (p), strip (stripIndex)
{
    const juce::String prefix = "v" + juce::String (strip + 1) + "_";
    const juce::Colour colour = voiceColour (strip);
    styleToggle (mute, kNeutral);     // Mute lights up white and dims the strip
    styleToggle (solo, colour);
    styleToggle (toFx, colour);
    hit.setColour (juce::TextButton::textColourOffId, colour);
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
        s->setColour (juce::Slider::thumbColourId, colour);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxTextColourId, kDim);
        if (juce::String (d.name) == "Pan") s->getProperties().set (fromCentreId(), true);   // coloured part starts in the middle
        addAndMakeVisible (*s);
        sliderAttachments.add (new SliderAttachment (proc.apvts, d.id, *s));
        auto* l = labels.add (new juce::Label());
        styleCaption (*l, d.name, 11.0f);
        l->setColour (juce::Label::textColourId, kDim);
        l->setJustificationType (juce::Justification::centred);
        l->setMinimumHorizontalScale (0.5f);
        l->setBorderSize (juce::BorderSize<int> (0));
        addAndMakeVisible (*l);
    }
}

void VoiceStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const juce::Colour colour = voiceColour (strip).withMultipliedAlpha (audible ? 1.0f : 0.35f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 6.0f);
    {   // a bar in the voice's colour along the top edge
        juce::Graphics::ScopedSaveState save (g);
        juce::Path outline;
        outline.addRoundedRectangle (bounds, 6.0f);
        g.reduceClipRegion (outline);
        g.setColour (colour);
        g.fillRect (bounds.withHeight (4.0f));
    }
    g.setColour (kLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (ELXARTwoProcessor::kStripNames[(size_t) strip]).toUpperCase(), getLocalBounds().withTrimmedTop (5).removeFromTop (20), juce::Justification::centred);
}

void VoiceStrip::setAudible (bool shouldBeAudible)
{
    if (audible == shouldBeAudible) return;
    audible = shouldBeAudible;
    for (auto* s : sliders) s->setAlpha (audible ? 1.0f : 0.4f);
    for (auto* l : labels) l->setAlpha (audible ? 1.0f : 0.4f);
    repaint();
}

void VoiceStrip::resized()
{
    auto area = getLocalBounds().reduced (5);
    area.removeFromTop (22);
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
juce::Rectangle<int> StepGrid::rowBounds (int row) const
{
    const int h = getHeight() / ELXARTwoProcessor::kNumRows;
    return { 0, row * h, getWidth(), h };
}

juce::Rectangle<int> StepGrid::cellBounds (int row, int column) const
{
    const int w = (getWidth() - kLabelWidth) / ELXARTwoProcessor::kStepsPerBar;
    const int h = getHeight() / ELXARTwoProcessor::kNumRows;
    return { kLabelWidth + column * w, row * h, w, h };
}

bool StepGrid::cellAt (juce::Point<int> p, int& row, int& step) const
{
    const int w = (getWidth() - kLabelWidth) / ELXARTwoProcessor::kStepsPerBar;
    const int h = getHeight() / ELXARTwoProcessor::kNumRows;
    if (w <= 0 || h <= 0 || p.y < 0) return false;
    row = p.y / h;
    if (row < 0 || row >= ELXARTwoProcessor::kNumRows) return false;
    if (p.x < kLabelWidth) { step = -1; return true; }   // the row's name
    const int column = (p.x - kLabelWidth) / w;
    if (column < 0 || column >= ELXARTwoProcessor::kStepsPerBar) return false;
    step = bar * ELXARTwoProcessor::kStepsPerBar + column;
    return true;
}

void StepGrid::select (int row, int step)
{
    if (row == selectedRow && step == selectedStep) return;
    selectedRow = row;
    selectedStep = step;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void StepGrid::updatePositions()
{
    bool changed = false;
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
    {
        const int pos = proc.getRowPosition (row);
        if (pos != shownPosition[(size_t) row]) { shownPosition[(size_t) row] = pos; changed = true; }
    }
    if (changed) repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    const int pattern = proc.getCurrentPattern();
    const bool isPlayingPattern = proc.getPlayingPattern() == pattern;
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
    {
        const auto rb = rowBounds (row);
        const juce::Colour colour = rowColour (row);
        if (row == selectedRow)
        {
            g.setColour (kRaised);
            g.fillRoundedRectangle (rb.withWidth (kLabelWidth - 4).reduced (0, 1).toFloat(), 4.0f);
        }
        g.setColour (colour);   // a dot in the voice's colour, then the row's name
        g.fillEllipse (6.0f, (float) rb.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (row == selectedRow ? juce::Colours::white : kText);
        g.setFont (juce::FontOptions (12.0f, row == selectedRow ? juce::Font::bold : juce::Font::plain));
        g.drawText (ELXARTwoProcessor::kRowNames[(size_t) row], 18, rb.getY(), kLabelWidth - 26, rb.getHeight(), juce::Justification::centredLeft);

        const int length = proc.getTrackLength (pattern, row);
        for (int column = 0; column < ELXARTwoProcessor::kStepsPerBar; ++column)
        {
            const int step = bar * ELXARTwoProcessor::kStepsPerBar + column;
            const auto c = cellBounds (row, column).reduced (2).toFloat();
            const ElxarStep s = proc.getStep (pattern, row, step);
            const bool inside = step < length;
            const bool playingHere = isPlayingPattern && step == shownPosition[(size_t) row];
            juce::Colour fill = s.on ? colour.withMultipliedBrightness (0.45f + 0.55f * (float) s.velocity / 127.0f)
                                     : ((column % 4) == 0 ? kStepBeat : kStepOff);
            if (playingHere) fill = fill.interpolatedWith (juce::Colours::white, s.on ? 0.55f : 0.30f);   // the step being played lights up
            if (! inside) fill = fill.withMultipliedAlpha (0.25f);   // after the end of this row: not played
            g.setColour (fill);
            g.fillRoundedRectangle (c, 3.0f);
            if (s.on)   // small letters for the extras on this step
            {
                juce::String marks;
                if (s.flam != 0) marks << "F";
                if (s.shift < ELXAR_SHIFT_NONE) marks << "<";
                if (s.shift > ELXAR_SHIFT_NONE) marks << ">";
                if (s.probability < 100) marks << "%";
                if (s.note != ELXAR_DEFAULT_NOTE) marks << "N";
                if (s.autoTarget[0] != 0 || s.autoTarget[1] != 0) marks << "A";
                if (marks.isNotEmpty())
                {
                    g.setColour (kOnText);
                    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                    g.drawText (marks, c.toNearestInt(), juce::Justification::centred);
                }
            }
            if (row == selectedRow && step == selectedStep)   // the selected step: a white frame
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (c.expanded (0.5f), 3.5f, 1.6f);
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int row = 0, step = 0;
    painting = false;
    if (! cellAt (e.getPosition(), row, step)) return;
    if (step < 0) { select (row, selectedStep); return; }                 // a click on the name selects the row
    if (e.mods.isPopupMenu() || e.mods.isCtrlDown() || e.mods.isAltDown()) // right-click (or ctrl / option click): select only
    {
        select (row, step);
        return;
    }
    paintValue = proc.getStep (proc.getCurrentPattern(), row, step).on == 0;
    proc.setStepOn (proc.getCurrentPattern(), row, step, paintValue);
    painting = true;
    lastRow = row; lastStep = step;
    selectedRow = row; selectedStep = step;
    repaint();
    if (onSelectionChanged) onSelectionChanged();
    if (onEdited) onEdited();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)   // drag across squares to switch several on or off
{
    int row = 0, step = 0;
    if (! painting || ! cellAt (e.getPosition(), row, step) || step < 0 || (row == lastRow && step == lastStep)) return;
    proc.setStepOn (proc.getCurrentPattern(), row, step, paintValue);
    lastRow = row; lastStep = step;
    repaint();
    if (onEdited) onEdited();
}

// ================================================================ DragMidiBox
void DragMidiBox::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (kRaised);
    g.fillRoundedRectangle (bounds, 5.0f);
    juce::Path frame;
    frame.addRoundedRectangle (bounds, 5.0f);
    juce::Path dashes;
    const float dashLengths[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.4f).createDashedStroke (dashes, frame, dashLengths, 2);
    g.setColour (kNeutral);
    g.fillPath (dashes);
    g.setColour (kText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText ("DRAG MIDI OUT\ndrag this onto a Logic track", getLocalBounds().reduced (4), juce::Justification::centred, 2);
}

void DragMidiBox::mouseDown (const juce::MouseEvent&) { dragStarted = false; }

void DragMidiBox::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto file = proc.writePatternMidiFile (proc.getCurrentPattern(), getBars ? getBars() : 0);
    if (! file.existsAsFile())
    {
        if (onStatus) onStatus ("Could not write the MIDI file.");
        return;
    }
    if (onStatus) onStatus ("Dragging " + file.getFileName() + " ...");
    auto safeThis = juce::Component::SafePointer<DragMidiBox> (this);
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
        [safeThis] { if (safeThis != nullptr && safeThis->onStatus) safeThis->onStatus ("Drag finished."); });
    if (! started && onStatus) onStatus ("The Mac refused to start the drag.");
}

void DragMidiBox::mouseUp (const juce::MouseEvent&) { dragStarted = false; }

// ================================================================ PatternPage
void PatternPage::setUpSmallSlider (juce::Slider& s, juce::Label& l, const juce::String& text, double lo, double hi, double value)
{
    styleSlider (s, 34);
    s.setRange (lo, hi, 1.0);
    s.setValue (value, juce::dontSendNotification);
    addAndMakeVisible (s);
    styleCaption (l, text, 12.0f);
    addAndMakeVisible (l);
}

void PatternPage::fillTargetBox (juce::ComboBox& box)
{
    fillTargets (box, 0);
    box.setTooltip ("Per-step automation: when this step plays, the chosen sound parameter jumps to the value on the right");
    addAndMakeVisible (box);
}

PatternPage::PatternPage (ELXARTwoProcessor& p) : proc (p), grid (p), dragBox (p)
{
    // ---- bar buttons, shuffle, copy / paste / clear ----
    styleCaption (barCaption, "Bar", 12.0f);
    addAndMakeVisible (barCaption);
    for (int i = 0; i < ELXARTwoProcessor::kNumBars; ++i)
    {
        auto& b = barButtons[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        styleToggle (b, kNeutral);
        b.setRadioGroupId (101);
        b.setTooltip ("Show steps " + juce::String (i * 16 + 1) + " to " + juce::String (i * 16 + 16) + " of each row");
        b.onClick = [this, i] { if (barButtons[(size_t) i].getToggleState()) { grid.setBar (i); refreshStepEditor(); } };
        addAndMakeVisible (b);
    }
    barButtons[0].setToggleState (true, juce::dontSendNotification);
    setUpSmallSlider (shuffle, shuffleLabel, "Shuffle", 0.0, 127.0, 0.0);
    shuffle.setTooltip ("Swing: delays every second 16th note. Stored per pattern.");
    shuffle.onValueChange = [this] { if (! updating) proc.setShuffle (proc.getCurrentPattern(), (int) shuffle.getValue()); };
    copyButton.onClick = [this] { proc.copyPattern (proc.getCurrentPattern()); pasteButton.setEnabled (true); };
    pasteButton.onClick = [this] { if (proc.pastePattern (proc.getCurrentPattern())) refresh(); };
    clearButton.onClick = [this] { proc.clearPattern (proc.getCurrentPattern()); refresh(); };
    for (auto* b : { &copyButton, &pasteButton, &clearButton }) addAndMakeVisible (*b);
    pasteButton.setEnabled (proc.hasCopiedPattern());

    // ---- grid and row lengths ----
    grid.setTooltip ("Click a square: step on or off (drag to paint several). Right-click a square: select it without changing it. "
                     "Click a row's name: select the row. Letters on a step: F = flam, < or > = shifted, % = does not always play, "
                     "N = pitch changed, A = automation.");
    grid.onSelectionChanged = [this] { refreshStepEditor(); };
    grid.onEdited = [this] { refreshStepEditor(); };
    addAndMakeVisible (grid);
    styleCaption (lengthCaption, "Row length (steps)", 12.0f);
    lengthCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lengthCaption);
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
    {
        auto& s = lengthSliders[(size_t) row];
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 36, 18);
        s.setColour (juce::Slider::textBoxOutlineColourId, kLine);
        s.setColour (juce::Slider::textBoxBackgroundColourId, kWell);
        s.setColour (juce::Slider::textBoxTextColourId, rowColour (row));
        s.setRange (1.0, (double) ELXARTwoProcessor::kNumSteps, 1.0);
        s.setTooltip ("How many steps this row plays before it starts again (1 to 64). Rows can have different lengths.");
        s.onValueChange = [this, row] { if (! updating) { proc.setTrackLength (proc.getCurrentPattern(), row, (int) lengthSliders[(size_t) row].getValue()); grid.repaint(); } };
        addAndMakeVisible (s);
    }

    // ---- step editor ----
    styleCaption (stepCaption, "", 12.0f, true);
    addAndMakeVisible (stepCaption);
    setUpSmallSlider (velocity, velLabel, "Velocity", 1.0, 127.0, 110.0);
    setUpSmallSlider (probability, probLabel, "Probability %", 0.0, 100.0, 100.0);
    setUpSmallSlider (note, noteLabel, "Pitch", 0.0, 127.0, (double) ELXAR_DEFAULT_NOTE);
    note.setTooltip ("60 = the sound's normal pitch. Other numbers play this step higher or lower, in semitones.");
    probability.setTooltip ("How often this step plays: 100 = always, 50 = about every second time");
    styleCaption (flamLabel, "Flam", 12.0f);
    styleCaption (shiftLabel, "Shift", 12.0f);
    styleCaption (auto1Label, "Automation 1", 12.0f);
    styleCaption (auto2Label, "Automation 2", 12.0f);
    for (auto* l : { &flamLabel, &shiftLabel, &auto1Label, &auto2Label }) addAndMakeVisible (*l);
    flam.addItemList ({ "Off", "x2", "x3", "x4", "x6", "x8" }, 1);
    flam.setTooltip ("Several quick hits inside this one step");
    for (int i = 0; i <= 14; ++i) shift.addItem ((i > 7 ? "+" : "") + juce::String (i - 7), i + 1);
    shift.setTooltip ("Moves this step a little earlier (-) or later (+), in eighths of a step");
    addAndMakeVisible (flam);
    addAndMakeVisible (shift);
    fillTargetBox (auto1Target);
    fillTargetBox (auto2Target);
    styleSlider (auto1Value, 34); auto1Value.setRange (0.0, 127.0, 1.0); addAndMakeVisible (auto1Value);
    styleSlider (auto2Value, 34); auto2Value.setRange (0.0, 127.0, 1.0); addAndMakeVisible (auto2Value);
    for (auto* s : { &velocity, &probability, &note, &auto1Value, &auto2Value })
        s->onValueChange = [this] { writeStepFromEditor(); };
    for (auto* c : { &flam, &shift, &auto1Target, &auto2Target })
        c->onChange = [this] { writeStepFromEditor(); };

    // ---- generator ----
    styleCaption (genCaption, "BEAT GENERATOR (whole pattern)", 12.0f, true);
    genCaption.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (genCaption);
    styleSlider (density); styleSlider (variation); styleSlider (kickHits);
    for (auto* s : { &density, &variation, &kickHits }) addAndMakeVisible (*s);
    styleCaption (densityLabel, "Density", 12.0f);
    styleCaption (variationLabel, "Variation", 12.0f);
    styleCaption (kickLabel, "Drum 1 hits per bar", 12.0f);
    for (auto* l : { &densityLabel, &variationLabel, &kickLabel }) addAndMakeVisible (*l);
    densityAttachment = std::make_unique<SliderAttachment> (proc.apvts, "gen_density", density);
    variationAttachment = std::make_unique<SliderAttachment> (proc.apvts, "gen_variation", variation);
    kickAttachment = std::make_unique<SliderAttachment> (proc.apvts, "gen_euclid", kickHits);
    generate.setTooltip ("Replaces the current pattern with a new one-bar beat. Every rhythm comes from the Euclidean generator.");
    generate.onClick = [this]
    {
        proc.generatePattern (proc.getCurrentPattern(), (float) density.getValue() / 100.0f, (float) variation.getValue() / 100.0f, (int) kickHits.getValue());
        refresh();
    };
    addAndMakeVisible (generate);

    styleCaption (euclidCaption, "EUCLID (one row)", 12.0f, true);
    euclidCaption.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (euclidCaption);
    setUpSmallSlider (eLen, eLenLabel, "Length", 1.0, 16.0, 16.0);
    setUpSmallSlider (eHits, eHitsLabel, "Hits", 0.0, 16.0, 4.0);
    setUpSmallSlider (eRot, eRotLabel, "Rotate", 0.0, 15.0, 0.0);
    fillRow.setTooltip ("Spreads the hits as evenly as possible over the length, for the row that is selected in the grid (click its name). Also sets the row's length.");
    fillRow.onClick = [this]
    {
        proc.euclidFillRow (proc.getCurrentPattern(), grid.getSelectedRow(), (int) eLen.getValue(), (int) eHits.getValue(), (int) eRot.getValue());
        refresh();
    };
    addAndMakeVisible (fillRow);

    // ---- MIDI file ----
    styleCaption (midiCaption, "MIDI FILE", 12.0f, true);
    midiCaption.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (midiCaption);
    styleCaption (barsLabel, "Bars", 12.0f);
    addAndMakeVisible (barsLabel);
    midiBars.addItem ("Auto", 1);
    for (int b : { 1, 2, 4, 8 }) midiBars.addItem (juce::String (b), b + 1);
    midiBars.setSelectedId (1, juce::dontSendNotification);
    midiBars.setTooltip ("How many bars the MIDI file has. Auto = long enough for the longest row.");
    addAndMakeVisible (midiBars);
    dragBox.getBars = [this] { return juce::jmax (0, midiBars.getSelectedId() - 1); };
    dragBox.onStatus = [this] (const juce::String& s) { dragStatus.setText (s, juce::dontSendNotification); };
    addAndMakeVisible (dragBox);
    revealMidi.setButtonText ("Show in Finder");
    revealMidi.setTooltip ("Writes the MIDI file and shows it in Finder (in case dragging does not work)");
    revealMidi.onClick = [this]
    {
        const auto file = proc.writePatternMidiFile (proc.getCurrentPattern(), juce::jmax (0, midiBars.getSelectedId() - 1));
        if (file.existsAsFile()) { file.revealToUser(); dragStatus.setText ("Written: " + file.getFileName(), juce::dontSendNotification); }
        else dragStatus.setText ("Could not write the MIDI file.", juce::dontSendNotification);
    };
    addAndMakeVisible (revealMidi);
    styleCaption (dragStatus, "", 11.0f);
    dragStatus.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (dragStatus);

    refresh();
}

void PatternPage::paint (juce::Graphics& g) { g.fillAll (kPanel); }

void PatternPage::refresh()
{
    const int pattern = proc.getCurrentPattern();
    shownPattern = pattern;
    updating = true;
    shuffle.setValue ((double) proc.getShuffle (pattern), juce::dontSendNotification);
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
        lengthSliders[(size_t) row].setValue ((double) proc.getTrackLength (pattern, row), juce::dontSendNotification);
    updating = false;
    pasteButton.setEnabled (proc.hasCopiedPattern());
    refreshStepEditor();
    grid.repaint();
}

void PatternPage::timerTick()
{
    if (proc.getCurrentPattern() != shownPattern) refresh();
    grid.updatePositions();
}

void PatternPage::refreshStepEditor()
{
    const int row = grid.getSelectedRow(), step = grid.getSelectedStep();
    const ElxarStep s = proc.getStep (proc.getCurrentPattern(), row, step);
    colourStepEditor (row);
    updating = true;
    stepCaption.setText (juce::String ("Selected step: ") + ELXARTwoProcessor::kRowNames[(size_t) row] + ", step " + juce::String (step + 1) + (s.on ? "" : " (off)"),
                         juce::dontSendNotification);
    velocity.setValue ((double) s.velocity, juce::dontSendNotification);
    probability.setValue ((double) s.probability, juce::dontSendNotification);
    note.setValue ((double) s.note, juce::dontSendNotification);
    flam.setSelectedId ((int) s.flam + 1, juce::dontSendNotification);
    shift.setSelectedId ((int) s.shift + 1, juce::dontSendNotification);
    selectTarget (auto1Target, (int) s.autoTarget[0]);
    selectTarget (auto2Target, (int) s.autoTarget[1]);
    auto1Value.setValue ((double) s.autoValue[0], juce::dontSendNotification);
    auto2Value.setValue ((double) s.autoValue[1], juce::dontSendNotification);
    auto1Value.setEnabled (s.autoTarget[0] != 0);
    auto2Value.setEnabled (s.autoTarget[1] != 0);
    updating = false;
}

// The step editor and the "Fill selected row" box take the colour of the row they work on.
void PatternPage::colourStepEditor (int row)
{
    if (row == colouredRow) return;
    colouredRow = row;
    const juce::Colour colour = rowColour (row);
    stepCaption.setColour (juce::Label::textColourId, colour);
    for (auto* s : { &velocity, &probability, &note, &auto1Value, &auto2Value, &eLen, &eHits, &eRot })
        s->setColour (juce::Slider::thumbColourId, colour);
}

void PatternPage::writeStepFromEditor()
{
    if (updating) return;
    const int row = grid.getSelectedRow(), step = grid.getSelectedStep();
    ElxarStep s = proc.getStep (proc.getCurrentPattern(), row, step);
    s.velocity = (uint8_t) velocity.getValue();
    s.probability = (uint8_t) probability.getValue();
    s.note = (uint8_t) note.getValue();
    s.flam = (uint8_t) juce::jmax (0, flam.getSelectedId() - 1);
    s.shift = (uint8_t) (shift.getSelectedId() > 0 ? shift.getSelectedId() - 1 : ELXAR_SHIFT_NONE);
    s.autoTarget[0] = (uint8_t) juce::jmax (0, auto1Target.getSelectedId() - 1);
    s.autoTarget[1] = (uint8_t) juce::jmax (0, auto2Target.getSelectedId() - 1);
    s.autoValue[0] = (uint8_t) auto1Value.getValue();
    s.autoValue[1] = (uint8_t) auto2Value.getValue();
    proc.setStep (proc.getCurrentPattern(), row, step, s);
    auto1Value.setEnabled (s.autoTarget[0] != 0);
    auto2Value.setEnabled (s.autoTarget[1] != 0);
    grid.repaint();
}

void PatternPage::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    auto place = [] (juce::Rectangle<int>& from, juce::Label& l, int labelWidth, juce::Component& c, int width)
    {
        l.setBounds (from.removeFromLeft (labelWidth));
        c.setBounds (from.removeFromLeft (width).reduced (0, 1));
        from.removeFromLeft (10);
    };

    // bar buttons, shuffle, copy / paste / clear, caption of the length column
    auto top = area.removeFromTop (26);
    const int lengthColumn = 112;
    lengthCaption.setBounds (top.removeFromRight (lengthColumn + 20));
    barCaption.setBounds (top.removeFromLeft (30));
    for (auto& b : barButtons) b.setBounds (top.removeFromLeft (34).reduced (1, 2));
    top.removeFromLeft (16);
    place (top, shuffleLabel, 52, shuffle, 220);
    top.removeFromLeft (10);
    copyButton.setBounds (top.removeFromLeft (104).reduced (1, 2));
    pasteButton.setBounds (top.removeFromLeft (64).reduced (1, 2));
    clearButton.setBounds (top.removeFromLeft (104).reduced (1, 2));
    area.removeFromTop (4);

    // grid with one length box per row at the right
    const int gridRow = 27;
    auto gridArea = area.removeFromTop (ELXARTwoProcessor::kNumRows * gridRow);
    auto lengths = gridArea.removeFromRight (lengthColumn);
    gridArea.removeFromRight (8);
    grid.setBounds (gridArea);
    for (int row = 0; row < ELXARTwoProcessor::kNumRows; ++row)
        lengthSliders[(size_t) row].setBounds (lengths.removeFromTop (gridRow).reduced (0, 2));
    area.removeFromTop (6);

    // step editor, two lines
    auto line = area.removeFromTop (24);
    stepCaption.setBounds (line.removeFromLeft (250));
    place (line, velLabel, 56, velocity, 170);
    place (line, probLabel, 84, probability, 170);
    place (line, noteLabel, 38, note, 170);
    line = area.removeFromTop (24);
    place (line, flamLabel, 36, flam, 76);
    place (line, shiftLabel, 36, shift, 76);
    place (line, auto1Label, 84, auto1Target, 190);
    auto1Value.setBounds (line.removeFromLeft (130).reduced (0, 1));
    line.removeFromLeft (14);
    place (line, auto2Label, 84, auto2Target, 190);
    auto2Value.setBounds (line.removeFromLeft (130).reduced (0, 1));
    area.removeFromTop (8);

    // generator | euclid for one row | MIDI file
    auto bottom = area.removeFromTop (18 + 3 * 24);
    auto gen = bottom.removeFromLeft (330);
    genCaption.setBounds (gen.removeFromTop (18));
    auto row3 = [] (juce::Rectangle<int>& from, juce::Label& l, int labelWidth, juce::Slider& s)
    {
        auto r = from.removeFromTop (24);
        l.setBounds (r.removeFromLeft (labelWidth));
        s.setBounds (r.reduced (0, 1));
    };
    row3 (gen, densityLabel, 120, density);
    row3 (gen, variationLabel, 120, variation);
    row3 (gen, kickLabel, 120, kickHits);
    bottom.removeFromLeft (8);
    auto genButton = bottom.removeFromLeft (112);
    genButton.removeFromTop (18);
    generate.setBounds (genButton.removeFromTop (48).reduced (0, 4));
    bottom.removeFromLeft (20);

    auto euclid = bottom.removeFromLeft (230);
    euclidCaption.setBounds (euclid.removeFromTop (18));
    row3 (euclid, eLenLabel, 56, eLen);
    row3 (euclid, eHitsLabel, 56, eHits);
    row3 (euclid, eRotLabel, 56, eRot);
    bottom.removeFromLeft (8);
    auto fillButton = bottom.removeFromLeft (130);
    fillButton.removeFromTop (18);
    fillRow.setBounds (fillButton.removeFromTop (48).reduced (0, 4));
    bottom.removeFromLeft (20);

    auto midi = bottom;
    auto midiTop = midi.removeFromTop (18);
    midiCaption.setBounds (midiTop.removeFromLeft (70));
    dragStatus.setBounds (midiTop);
    auto midiLine = midi.removeFromTop (24);
    barsLabel.setBounds (midiLine.removeFromLeft (34));
    midiBars.setBounds (midiLine.removeFromLeft (64).reduced (0, 1));
    midiLine.removeFromLeft (8);
    revealMidi.setBounds (midiLine.reduced (0, 1));
    dragBox.setBounds (midi.reduced (0, 2));
}

// ================================================================ KitControl
KitControl::KitControl (ELXARTwoProcessor& p, const elxar::ParamInfo& i, int engineVoice, juce::Colour accent) : proc (p), info (i)
{
    styleCaption (label, info.name, 12.0f);
    addAndMakeVisible (label);
    using K = elxar::ParamKind;
    if (info.kind == K::value)
    {
        slider = std::make_unique<juce::Slider>();
        styleSlider (*slider, 30, accent);
        slider->setRange (0.0, 127.0, 1.0);
        slider->onValueChange = [this] { if (! updating) proc.setKitParam (info.par, (int) slider->getValue()); };
        addAndMakeVisible (*slider);
    }
    else if (info.kind == K::onOff)
    {
        toggle = std::make_unique<juce::ToggleButton> ("on");
        toggle->setColour (juce::ToggleButton::tickColourId, accent);
        toggle->onClick = [this] { if (! updating) proc.setKitParam (info.par, toggle->getToggleState() ? 1 : 0); };
        addAndMakeVisible (*toggle);
    }
    else
    {
        combo = std::make_unique<juce::ComboBox>();
        if (info.kind == K::choice)
            for (int n = 0; n < info.numChoices; ++n) combo->addItem (info.choices[n], n + 1);
        else
            fillTargets (*combo, info.kind == K::velocityTarget ? engineVoice + 1 : 0);
        combo->onChange = [this] { if (! updating && combo->getSelectedId() > 0) proc.setKitParam (info.par, combo->getSelectedId() - 1); };
        addAndMakeVisible (*combo);
    }
    refresh();
}

void KitControl::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (100));
    if (slider) slider->setBounds (r.reduced (0, 1));
    if (combo) combo->setBounds (r.reduced (0, 2));
    if (toggle) toggle->setBounds (r.reduced (0, 1));
}

void KitControl::refresh()
{
    const int v = proc.getKitParam (info.par);
    updating = true;
    if (slider && ! slider->isMouseButtonDown() && (int) slider->getValue() != v) slider->setValue ((double) v, juce::dontSendNotification);
    if (toggle && toggle->getToggleState() != (v != 0)) toggle->setToggleState (v != 0, juce::dontSendNotification);
    if (combo && ! combo->isPopupActive())
    {
        if (info.kind == elxar::ParamKind::choice)
        {
            const int id = juce::jlimit (1, juce::jmax (1, info.numChoices), v + 1);
            if (combo->getSelectedId() != id) combo->setSelectedId (id, juce::dontSendNotification);
        }
        else
        {
            selectTarget (*combo, v);
        }
    }
    updating = false;
}

// ================================================================ SoundPage
SoundPage::SoundPage (ELXARTwoProcessor& p) : proc (p)
{
    for (int s = 0; s < ELXARTwoProcessor::kNumStrips; ++s)
    {
        auto& b = voiceButtons[(size_t) s];
        b.setButtonText (ELXARTwoProcessor::kStripNames[(size_t) s]);
        styleToggle (b, voiceColour (s));
        b.setRadioGroupId (202);
        b.onClick = [this, s] { if (voiceButtons[(size_t) s].getToggleState()) showVoice (s); };
        addAndMakeVisible (b);
    }
    styleCaption (kitCaption, "Kit slot", 12.0f);
    addAndMakeVisible (kitCaption);
    slotBox.setTooltip ("Eight places inside this Logic project to keep complete kits (all sound settings of all six voices and the effect)");
    addAndMakeVisible (slotBox);
    storeButton.setTooltip ("Copy the current sound into the chosen slot");
    recallButton.setTooltip ("Replace the current sound with the kit in the chosen slot");
    initButton.setTooltip ("Replace the current sound with the starting kit");
    storeButton.onClick = [this]
    {
        const int slot = slotBox.getSelectedId() - 1;
        proc.storeKitSlot (slot);
        updateSlotBox();
        kitStatus.setText ("Stored in slot " + juce::String (slot + 1), juce::dontSendNotification);
    };
    recallButton.onClick = [this]
    {
        const int slot = slotBox.getSelectedId() - 1;
        const bool ok = proc.recallKitSlot (slot);
        kitStatus.setText (ok ? "Recalled slot " + juce::String (slot + 1) : "Slot " + juce::String (slot + 1) + " is empty", juce::dontSendNotification);
        refresh (false);
    };
    initButton.onClick = [this] { proc.loadInitKit(); kitStatus.setText ("Starting kit loaded", juce::dontSendNotification); refresh (false); };
    for (auto* b : { &storeButton, &recallButton, &initButton }) addAndMakeVisible (*b);
    styleCaption (kitStatus, "", 12.0f);
    kitStatus.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (kitStatus);
    slotBox.setSelectedId (1, juce::dontSendNotification);
    updateSlotBox();
    voiceButtons[0].setToggleState (true, juce::dontSendNotification);
    showVoice (0);
}

void SoundPage::updateSlotBox()
{
    if (slotBox.isPopupActive()) return;
    const int selected = juce::jmax (1, slotBox.getSelectedId());
    slotBox.clear (juce::dontSendNotification);
    for (int i = 0; i < ELXARTwoProcessor::kNumKitSlots; ++i)
        slotBox.addItem (juce::String (i + 1) + (proc.isKitSlotUsed (i) ? "  (kit stored)" : "  (empty)"), i + 1);
    slotBox.setSelectedId (selected, juce::dontSendNotification);
}

void SoundPage::showVoice (int stripIndex)
{
    shownStrip = stripIndex;
    controls.clear();
    const int voice = ELXARTwoProcessor::kStripToVoice[(size_t) stripIndex];
    for (const auto& info : elxar::voiceParams (voice))
        addAndMakeVisible (controls.add (new KitControl (proc, info, voice, voiceColour (stripIndex))));
    resized();
    repaint();
}

void SoundPage::refresh (bool alsoKitSlots)
{
    for (auto* c : controls) c->refresh();
    if (alsoKitSlots) updateSlotBox();
}

void SoundPage::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    for (const auto& h : headers)
    {
        g.setColour (voiceColour (juce::jmax (0, shownStrip)));
        g.drawText (h.text.toUpperCase(), h.bounds, juce::Justification::centredLeft);
        g.setColour (kLine);
        g.fillRect (h.bounds.withY (h.bounds.getBottom() - 2).withHeight (1));
    }
}

void SoundPage::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    auto top = area.removeFromTop (26);
    for (auto& b : voiceButtons) b.setBounds (top.removeFromLeft (80).reduced (1, 2));
    top.removeFromLeft (24);
    kitCaption.setBounds (top.removeFromLeft (52));
    slotBox.setBounds (top.removeFromLeft (150).reduced (0, 2));
    top.removeFromLeft (6);
    storeButton.setBounds (top.removeFromLeft (84).reduced (1, 2));
    recallButton.setBounds (top.removeFromLeft (84).reduced (1, 2));
    initButton.setBounds (top.removeFromLeft (70).reduced (1, 2));
    top.removeFromLeft (8);
    kitStatus.setBounds (top);
    area.removeFromTop (8);

    // The parameters in columns, a heading above each group. A group is never split over two columns.
    headers.clear();
    const int columns = 5, rowHeight = 23, rowsPerColumn = juce::jmax (4, area.getHeight() / rowHeight);
    const int columnWidth = area.getWidth() / columns;
    const auto all = elxar::voiceParams (ELXARTwoProcessor::kStripToVoice[(size_t) juce::jmax (0, shownStrip)]);
    int column = 0, row = 0, i = 0;
    while (i < controls.size() && i < (int) all.size())
    {
        const juce::String group (all[(size_t) i].group);
        int count = 0;
        while (i + count < (int) all.size() && group == all[(size_t) (i + count)].group) ++count;
        if (row > 0 && row + 1 + count > rowsPerColumn) { ++column; row = 0; }
        const int x = area.getX() + column * columnWidth;
        headers.push_back ({ group, { x, area.getY() + row * rowHeight, columnWidth - 12, rowHeight } });
        ++row;
        for (int k = 0; k < count; ++k, ++row)
            controls[i + k]->setBounds (x, area.getY() + row * rowHeight, columnWidth - 12, rowHeight);
        i += count;
    }
}

// ================================================================ Editor
void ELXARTwoEditor::setUpHorizontalSlider (juce::Slider& s, juce::Label& l, const juce::String& text, const juce::String& paramId,
                                            std::unique_ptr<SliderAttachment>& attachment)
{
    styleSlider (s, 44);
    content.addAndMakeVisible (s);
    styleCaption (l, text, 12.0f);
    content.addAndMakeVisible (l);
    attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, s);
}

ELXARTwoEditor::ELXARTwoEditor (ELXARTwoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), patternPage (p), soundPage (p)
{
    setLookAndFeel (&look);
    tooltipWindow.setLookAndFeel (&look);
    addAndMakeVisible (content);
    for (int s = 0; s < ELXARTwoProcessor::kNumStrips; ++s)
    {
        const juce::String prefix = "v" + juce::String (s + 1) + "_";
        muteValues[(size_t) s] = proc.apvts.getRawParameterValue (prefix + "mute");
        soloValues[(size_t) s] = proc.apvts.getRawParameterValue (prefix + "solo");
    }

    // ---- top bar ----
    styleCaption (title, "ELXAR-Two", 18.0f, true);
    content.addAndMakeVisible (title);
    styleCaption (tempoLabel, "Tempo: -", 13.0f);
    content.addAndMakeVisible (tempoLabel);
    styleCaption (statusLabel, "", 12.0f);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    content.addAndMakeVisible (statusLabel);

    styleCaption (patternCaption, "Pattern", 13.0f);
    content.addAndMakeVisible (patternCaption);
    for (int i = 1; i <= ELXARTwoProcessor::kNumPatterns; ++i) patternBox.addItem (juce::String (i), i);
    patternBox.setSelectedId (proc.getCurrentPattern() + 1, juce::dontSendNotification);
    patternBox.setTooltip ("The pattern you see, edit and play. While the sequencer runs, a new choice starts when the playing pattern reaches its end.");
    patternBox.onChange = [this] { proc.setCurrentPattern (patternBox.getSelectedId() - 1); patternPage.refresh(); };
    content.addAndMakeVisible (patternBox);
    styleCaption (playingLabel, "", 12.0f);
    playingLabel.setColour (juce::Label::textColourId, kDim);
    content.addAndMakeVisible (playingLabel);

    styleCaption (runCaption, "Sequencer runs", 13.0f);
    content.addAndMakeVisible (runCaption);
    runBox.addItem ("With Logic transport", 1);
    runBox.addItem ("Always", 2);
    content.addAndMakeVisible (runBox);
    runAttachment = std::make_unique<ComboAttachment> (proc.apvts, "run_mode", runBox);

    styleCaption (sizeCaption, "Window size", 13.0f);
    content.addAndMakeVisible (sizeCaption);
    for (int percent : { 75, 100, 125, 150 }) sizeBox.addItem (juce::String (percent) + " %", percent);
    sizeBox.setTooltip ("Makes the whole window smaller or larger. You can also drag the bottom right corner.");
    sizeBox.onChange = [this]
    {
        if (settingSize || sizeBox.getSelectedId() <= 0) return;
        const float s = (float) sizeBox.getSelectedId() / 100.0f;
        setSize (juce::roundToInt ((float) kBaseWidth * s), juce::roundToInt ((float) kBaseHeight * s));
    };
    content.addAndMakeVisible (sizeBox);

    // ---- voices ----
    for (int s = 0; s < ELXARTwoProcessor::kNumStrips; ++s)
        content.addAndMakeVisible (strips.add (new VoiceStrip (proc, s)));

    // ---- pages ----
    tabs.setTabBarDepth (26);
    tabs.setOutline (0);
    tabs.addTab ("Pattern", kPanel, &patternPage, false);
    tabs.addTab ("Sound", kPanel, &soundPage, false);
    content.addAndMakeVisible (tabs);

    // ---- effect and master ----
    styleCaption (fxCaption, "EFFECT (one at a time, for the sounds with \"To FX\" on)  AND MASTER", 12.0f, true);
    fxCaption.setColour (juce::Label::textColourId, kDim);
    content.addAndMakeVisible (fxCaption);
    styleCaption (fxTypeLabel, "Effect", 12.0f);
    content.addAndMakeVisible (fxTypeLabel);
    fxType.addItemList ({ "Off", "Drive", "Ring mod", "Compressor", "Delay", "Reverb" }, 1);
    content.addAndMakeVisible (fxType);
    fxTypeAttachment = std::make_unique<ComboAttachment> (proc.apvts, "fx_type", fxType);
    styleCaption (fxKindLabel, "Kind", 12.0f);
    content.addAndMakeVisible (fxKindLabel);
    fxKind.setTooltip ("A second choice that belongs to the effect: drive type, ring-mod waveform, compressor ratio or delay type");
    fxKind.onChange = [this]
    {
        if (! updatingFxKind && fxKindParam >= 0 && fxKind.getSelectedId() > 0)
            proc.setKitParam (fxKindParam, fxKind.getSelectedId() - 1);
    };
    content.addAndMakeVisible (fxKind);
    setUpHorizontalSlider (fxMix, fxMixLabel, "Dry / wet", "fx_mix", fxMixAttachment);
    for (int i = 0; i < 4; ++i)
        setUpHorizontalSlider (fxKnobs[(size_t) i], fxKnobLabels[(size_t) i], "-", "fx_p" + juce::String (i + 1), fxKnobAttachments[(size_t) i]);
    setUpHorizontalSlider (master, masterLabel, "Master level", "master", masterAttachment);

    updateFxLabels();
    seenModelVersion = proc.getModelVersion();

    // The window can be resized: with the "Window size" list, or by dragging the bottom right corner. The shape stays the same.
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
        c->setSizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 2, kBaseHeight * 2);
    }
    const float s = juce::jlimit (0.5f, 2.0f, proc.getUiScale());
    setSize (juce::roundToInt ((float) kBaseWidth * s), juce::roundToInt ((float) kBaseHeight * s));
    sendLookAndFeelChange();   // everything made above picks up the look
    startTimerHz (30);
}

ELXARTwoEditor::~ELXARTwoEditor()
{
    stopTimer();
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

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
    if (type != shownFxType)
    {
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

        // the "Kind" list changes its meaning with the effect
        updatingFxKind = true;
        fxKind.clear (juce::dontSendNotification);
        fxKindParam = -1;
        auto fill = [this] (int par, const char* const* list, int count)
        {
            fxKindParam = par;
            for (int n = 0; n < count; ++n) fxKind.addItem (list[n], n + 1);
        };
        if (type == 1) fill (PAR_FX_DIST_TYPE, elxar::lists::distTypes, 3);
        if (type == 2) fill (PAR_FX_WAVE, elxar::lists::waveforms, 6);
        if (type == 3) fill (PAR_FX_RATIO, elxar::lists::compRatios, 6);
        if (type == 4) fill (PAR_FX_DELAY_TYPE, elxar::lists::delayTypes, 2);
        fxKind.setEnabled (fxKindParam >= 0);
        fxKind.setAlpha (fxKindParam >= 0 ? 1.0f : 0.35f);
        updatingFxKind = false;
    }
    if (fxKindParam >= 0 && ! fxKind.isPopupActive())
    {
        const int id = juce::jlimit (1, juce::jmax (1, fxKind.getNumItems()), proc.getKitParam (fxKindParam) + 1);
        if (fxKind.getSelectedId() != id)
        {
            updatingFxKind = true;
            fxKind.setSelectedId (id, juce::dontSendNotification);
            updatingFxKind = false;
        }
    }
}

void ELXARTwoEditor::timerCallback()
{
    ++tick;
    updateFxLabels();
    if (patternBox.getSelectedId() != proc.getCurrentPattern() + 1)
        patternBox.setSelectedId (proc.getCurrentPattern() + 1, juce::dontSendNotification);
    patternPage.timerTick();

    // A strip is dimmed while its voice is silent: muted, or another voice is soloed.
    bool anySolo = false;
    for (auto* v : soloValues) anySolo = anySolo || (v != nullptr && v->load() > 0.5f);
    for (int s = 0; s < strips.size(); ++s)
    {
        const bool muted = muteValues[(size_t) s] != nullptr && muteValues[(size_t) s]->load() > 0.5f;
        const bool solo = soloValues[(size_t) s] != nullptr && soloValues[(size_t) s]->load() > 0.5f;
        strips[s]->setAudible (! (muted || (anySolo && ! solo)));
    }

    // A project was loaded or a kit recalled: show everything again.
    const uint32_t version = proc.getModelVersion();
    if (version != seenModelVersion)
    {
        seenModelVersion = version;
        patternPage.refresh();
        soundPage.refresh (true);
    }
    else if (tick % 6 == 0 && soundPage.isShowing())
    {
        soundPage.refresh (false);   // follows knobs that Logic automates
    }

    juce::String tempo = "Tempo: " + juce::String (proc.getDisplayBpm(), 1) + (proc.hasHostTempo() ? " (from Logic)" : " (Logic gave no tempo, using 120)");
    tempo << (proc.isHostPlaying() ? "   Logic: playing" : "   Logic: stopped");
    if (tempoLabel.getText() != tempo) tempoLabel.setText (tempo, juce::dontSendNotification);

    juce::String playing;
    if (proc.isSequencerRunning())
    {
        playing << "playing pattern " << (proc.getPlayingPattern() + 1);
        if (proc.getPlayingPattern() != proc.getCurrentPattern()) playing << ", next: " << (proc.getCurrentPattern() + 1);
    }
    else
    {
        playing = "sequencer stopped";
    }
    if (playingLabel.getText() != playing) playingLabel.setText (playing, juce::dontSendNotification);

    const juce::String status = proc.engineAvailable()
        ? "Keys: C1 Drum 1, D1 Drum 2, E1 Drum 3, F1 Snare, G1 Hat closed, A1 Hat open, B1 Cymbal"
        : "ANOTHER ELXAR-Two IS ALREADY OPEN. Only the first one makes sound in this version.";
    if (statusLabel.getText() != status)
    {
        statusLabel.setColour (juce::Label::textColourId, proc.engineAvailable() ? kDim : kWarning);
        statusLabel.setText (status, juce::dontSendNotification);
    }
}

void ELXARTwoEditor::paint (juce::Graphics& g) { g.fillAll (kBackground); }

void ELXARTwoEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // six dots in the voice colours, then the name (a label) and the version
    for (int i = 0; i < ELXARTwoProcessor::kNumStrips; ++i)
    {
        g.setColour (voiceColour (i));
        g.fillEllipse (12.0f + 13.0f * (float) i, 18.0f, 9.0f, 9.0f);
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String ("version ") + ELXAR_VERSION_STRING, owner.title.getRight() + 2, owner.title.getY() + 2, 110, owner.title.getHeight(), juce::Justification::centredLeft);

    // panels behind the pages and behind the effect section
    for (const auto& panel : { owner.pagePanel, owner.fxPanel })
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (panel.toFloat(), 6.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Unofficial, modified port of the LXR-02 sound engine and sequencer. Not affiliated with or approved by Erica Synths.",
                getLocalBounds().removeFromBottom (18).reduced (10, 0), juce::Justification::centredLeft);
}

// The window: the whole layout is scaled to the window's width.
void ELXARTwoEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseWidth;
    content.setTransform (juce::AffineTransform());
    content.setBounds (0, 0, kBaseWidth, kBaseHeight);
    content.setTransform (juce::AffineTransform::scale (s));
    proc.setUiScale (s);
    const int percent = juce::roundToInt (s * 100.0f);
    settingSize = true;
    if (sizeBox.indexOfItemId (percent) >= 0) sizeBox.setSelectedId (percent, juce::dontSendNotification);
    else sizeBox.setText (juce::String (percent) + " %", juce::dontSendNotification);
    settingSize = false;
}

// Everything at the fixed layout size (kBaseWidth x kBaseHeight).
void ELXARTwoEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (10);
    area.removeFromBottom (14);

    // top bar (two lines)
    auto top = area.removeFromTop (26);
    top.removeFromLeft (84);   // the six dots
    title.setBounds (top.removeFromLeft (104));
    top.removeFromLeft (110);  // the version
    sizeBox.setBounds (top.removeFromRight (78).reduced (0, 1));
    sizeCaption.setBounds (top.removeFromRight (84));
    top.removeFromRight (10);
    runBox.setBounds (top.removeFromRight (170).reduced (0, 1));
    runCaption.setBounds (top.removeFromRight (100));
    top.removeFromRight (10);
    playingLabel.setBounds (top.removeFromRight (176));
    top.removeFromRight (4);
    patternBox.setBounds (top.removeFromRight (62).reduced (0, 1));
    patternCaption.setBounds (top.removeFromRight (54));
    auto info = area.removeFromTop (20);
    tempoLabel.setBounds (info.removeFromLeft (330));
    statusLabel.setBounds (info);
    area.removeFromTop (6);

    // six voice strips
    auto stripArea = area.removeFromTop (232);
    const int gap = 6;
    const float unit = (float) (stripArea.getWidth() - gap * (strips.size() - 1)) / 31.0f;   // 5 sliders per strip, 6 on the hi-hat
    float x = (float) stripArea.getX();
    for (int i = 0; i < strips.size(); ++i)
    {
        const float w = unit * (i == 4 ? 6.0f : 5.0f);
        strips[i]->setBounds (juce::roundToInt (x), stripArea.getY(), juce::roundToInt (w), stripArea.getHeight());
        x += w + (float) gap;
    }
    area.removeFromTop (6);

    // effect and master at the bottom, the pages in between
    auto fx = area.removeFromBottom (18 + 2 * 24 + 12);
    fxPanel = fx;
    fx = fx.reduced (8, 6);
    area.removeFromBottom (6);
    pagePanel = area;
    tabs.setBounds (area.reduced (2, 2));

    fxCaption.setBounds (fx.removeFromTop (18));
    auto place = [] (juce::Rectangle<int>& from, juce::Label& l, int labelWidth, juce::Component& c, int width)
    {
        l.setBounds (from.removeFromLeft (labelWidth));
        c.setBounds (from.removeFromLeft (width).reduced (0, 1));
        from.removeFromLeft (14);
    };
    auto line = fx.removeFromTop (24);
    place (line, fxTypeLabel, 46, fxType, 130);
    place (line, fxKindLabel, 36, fxKind, 130);
    place (line, fxMixLabel, 62, fxMix, 250);
    masterLabel.setBounds (line.removeFromLeft (84));
    master.setBounds (line.reduced (0, 1));
    line = fx.removeFromTop (24);
    const int knobWidth = (line.getWidth() - 3 * 14) / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto k = line.removeFromLeft (knobWidth);
        fxKnobLabels[(size_t) i].setBounds (k.removeFromLeft (74));
        fxKnobs[(size_t) i].setBounds (k.reduced (0, 1));
        line.removeFromLeft (14);
    }
}
