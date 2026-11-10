#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// The look of the ELXAR-Two window (Milestone 3). An original design: a dark panel with one colour per voice.
// It is not a copy of the LXR hardware panel (the hardware design is not licensed).

namespace elxarlook
{
// ---- the palette ----
const juce::Colour kBackground (0xff121418);   // window
const juce::Colour kPanel      (0xff1b1e24);   // voice strips, effect section
const juce::Colour kRaised     (0xff262a32);   // buttons, list boxes
const juce::Colour kWell       (0xff0b0c0f);   // fader slots
const juce::Colour kLine       (0xff363c46);   // outlines
const juce::Colour kText       (0xffe4e7eb);
const juce::Colour kDim        (0xff8f96a0);
const juce::Colour kNeutral    (0xffdfe5ee);   // accent for everything that belongs to no single voice
const juce::Colour kStepOff    (0xff2b3039);
const juce::Colour kStepBeat   (0xff3c434f);   // steps 1, 5, 9, 13
const juce::Colour kWarning    (0xffff6b6b);
const juce::Colour kOnText     (0xff101216);   // text on a lit button

// One colour per voice strip: Drum 1, Drum 2, Drum 3, Snare, Hi-hat, Cymbal.
inline juce::Colour voiceColour (int strip)
{
    static const juce::Colour colours[6] = {
        juce::Colour (0xffff6b57),   // Drum 1: coral
        juce::Colour (0xffff9f2e),   // Drum 2: orange
        juce::Colour (0xfff4d03f),   // Drum 3: yellow
        juce::Colour (0xff62d97b),   // Snare: green
        juce::Colour (0xff3ec9e0),   // Hi-hat: cyan
        juce::Colour (0xffb592ff) }; // Cymbal: violet
    return colours[juce::jlimit (0, 5, strip)];
}

// A slider with this property set draws its coloured part from the middle (used for Pan).
inline const juce::Identifier& fromCentreId() { static const juce::Identifier id ("elxarFromCentre"); return id; }

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, kBackground);
        setColour (juce::Label::textColourId, kText);

        setColour (juce::TextButton::buttonColourId, kRaised);
        setColour (juce::TextButton::buttonOnColourId, kNeutral);
        setColour (juce::TextButton::textColourOffId, kText);
        setColour (juce::TextButton::textColourOnId, kOnText);
        setColour (juce::ComboBox::outlineColourId, kLine);   // also the outline of buttons

        setColour (juce::ComboBox::backgroundColourId, kRaised);
        setColour (juce::ComboBox::textColourId, kText);
        setColour (juce::ComboBox::arrowColourId, kDim);
        setColour (juce::ComboBox::focusedOutlineColourId, kDim);

        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff20242b));
        setColour (juce::PopupMenu::textColourId, kText);
        setColour (juce::PopupMenu::headerTextColourId, kDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d4654));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        setColour (juce::Slider::backgroundColourId, kWell);
        setColour (juce::Slider::trackColourId, kNeutral);
        setColour (juce::Slider::thumbColourId, kNeutral);
        setColour (juce::Slider::textBoxTextColourId, kDim);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xff4a5565));

        setColour (juce::TextEditor::backgroundColourId, kWell);
        setColour (juce::TextEditor::textColourId, kText);
        setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff4a5565));
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::outlineColourId, kLine);
        setColour (juce::TextEditor::focusedOutlineColourId, kDim);
        setColour (juce::CaretComponent::caretColourId, kText);

        setColour (juce::ToggleButton::textColourId, kText);
        setColour (juce::ToggleButton::tickColourId, kNeutral);
        setColour (juce::ToggleButton::tickDisabledColourId, kLine);

        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff2a2f38));
        setColour (juce::TooltipWindow::textColourId, kText);
        setColour (juce::TooltipWindow::outlineColourId, kLine);

        setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TabbedButtonBar::tabTextColourId, kDim);
        setColour (juce::TabbedButtonBar::frontTextColourId, kText);
    }

    // ---- faders (upright) and sliders (lying down) ----
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }
        const bool upright = style == juce::Slider::LinearVertical;
        const float opacity = slider.isEnabled() ? 1.0f : 0.4f;
        const juce::Colour accent = slider.findColour (juce::Slider::thumbColourId);
        const bool fromCentre = (bool) slider.getProperties().getWithDefault (fromCentreId(), false);
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float slot = 4.0f;

        juce::Rectangle<float> track, filled, cap;
        if (upright)
        {
            const float cx = area.getCentreX();
            track = { cx - slot * 0.5f, area.getY(), slot, area.getHeight() };
            const float from = fromCentre ? area.getCentreY() : area.getBottom();
            filled = { cx - slot * 0.5f, juce::jmin (from, sliderPos), slot, std::abs (from - sliderPos) };
            const float capWidth = juce::jmin (22.0f, area.getWidth() - 2.0f);
            cap = { cx - capWidth * 0.5f, sliderPos - 6.0f, capWidth, 12.0f };
        }
        else
        {
            const float cy = area.getCentreY();
            track = { area.getX(), cy - slot * 0.5f, area.getWidth(), slot };
            const float from = fromCentre ? area.getCentreX() : area.getX();
            filled = { juce::jmin (from, sliderPos), cy - slot * 0.5f, std::abs (sliderPos - from), slot };
            const float capHeight = juce::jmin (16.0f, area.getHeight());
            cap = { sliderPos - 5.0f, cy - capHeight * 0.5f, 10.0f, capHeight };
        }

        g.setColour (slider.findColour (juce::Slider::backgroundColourId).withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (track, slot * 0.5f);
        g.setColour (kLine.withMultipliedAlpha (opacity * 0.7f));
        g.drawRoundedRectangle (track.expanded (0.5f), slot * 0.5f, 1.0f);
        g.setColour (accent.withMultipliedAlpha (opacity * 0.75f));
        g.fillRoundedRectangle (filled, slot * 0.5f);

        // the cap: a grey block with a line in the accent colour
        g.setColour (juce::Colour (0xff4b525f).withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (juce::Colour (0xff717a89).withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (cap.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (accent.withMultipliedAlpha (opacity));
        if (upright) g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
        else         g.fillRect (cap.getCentreX() - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
    }

    // ---- buttons ----
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour fill = backgroundColour;
        if (isDown) fill = fill.darker (0.25f);
        else if (isHighlighted) fill = fill.brighter (0.12f);
        const float opacity = button.isEnabled() ? 1.0f : 0.4f;
        g.setColour (fill.withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (bounds, 4.0f);
        if (! button.getToggleState())
        {
            g.setColour (button.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (opacity));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.62f)));
    }

    // ---- list boxes ----
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
        const float opacity = box.isEnabled() ? 1.0f : 0.4f;
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
        arrow.lineTo (cx, cy + 2.5f);
        arrow.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (opacity));
        g.strokePath (arrow, juce::PathStrokeType (1.6f));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) box.getHeight() * 0.62f)));
    }

    // ---- the two page tabs ("Pattern", "Sound") ----
    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool) override
    {
        const auto area = button.getActiveArea().toFloat();
        const bool front = button.isFrontTab();
        g.setColour (front ? kText : (isMouseOver ? kText.withAlpha (0.8f) : kDim));
        g.setFont (juce::FontOptions (14.0f, front ? juce::Font::bold : juce::Font::plain));
        g.drawText (button.getButtonText(), area, juce::Justification::centred);
        if (front)
        {
            g.setColour (kNeutral);
            g.fillRoundedRectangle (area.getX() + 8.0f, area.getBottom() - 3.0f, area.getWidth() - 16.0f, 2.5f, 1.2f);
        }
    }

    int getTabButtonBestWidth (juce::TabBarButton&, int) override { return 96; }

    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h) override
    {
        g.setColour (kLine);
        g.fillRect (0, h - 1, w, 1);
    }
};
}
