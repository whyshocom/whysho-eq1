#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace wsho::gui
{
class WhyshoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WhyshoLookAndFeel();

    // Re-read colours from the currently-active theme.
    void applyTheme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float min, float max,
                           const juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

} // namespace wsho::gui
