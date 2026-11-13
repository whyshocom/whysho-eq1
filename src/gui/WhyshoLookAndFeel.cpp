#include "WhyshoLookAndFeel.h"

using namespace wsho::gui;

WhyshoLookAndFeel::WhyshoLookAndFeel() { applyTheme(); }

void WhyshoLookAndFeel::applyTheme()
{
    const auto& t = theme();
    setColour (juce::ResizableWindow::backgroundColourId, t.bg);
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, t.panel2);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.grid);
    setColour (juce::PopupMenu::backgroundColourId, t.panel2);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.25f));
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::TextEditor::backgroundColourId, t.panel2);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.grid);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
}

juce::Font WhyshoLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

void WhyshoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> (x, y, w, h).reduced (6.0f);
    const auto r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);

    g.setColour (theme().panel2);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, a1, true);
    g.setColour (theme().grid);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, angle, true);
    g.setColour (s.isEnabled() ? theme().accent : theme().textDim);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip (c.x + (r - 6) * std::cos (angle - juce::MathConstants<float>::halfPi),
                            c.y + (r - 6) * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (theme().text);
    g.drawLine ({ c, tip }, 2.0f);
    g.setColour (theme().accent);
    g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
}

void WhyshoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float, float,
                                          const juce::Slider::SliderStyle, juce::Slider&)
{
    auto track = juce::Rectangle<float> ((float) x, y + h * 0.5f - 2.0f, (float) w, 4.0f);
    g.setColour (theme().grid);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (theme().accent);
    g.fillRoundedRectangle (track.withWidth (pos - x), 2.0f);
    g.setColour (theme().text);
    g.fillEllipse (pos - 6, y + h * 0.5f - 6, 12, 12);
}

void WhyshoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (theme().panel2);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (theme().grid);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    juce::Path p;
    const float cx = w - 14.0f, cy = h * 0.5f;
    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (theme().textDim);
    g.fillPath (p);
}

void WhyshoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool hl, bool down)
{
    auto b = bt.getLocalBounds().toFloat().reduced (0.5f);
    auto base = bt.getToggleState() ? theme().accent.withAlpha (0.9f) : theme().panel2;
    if (down) base = base.darker (0.2f); else if (hl) base = base.brighter (0.1f);
    g.setColour (base);
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (theme().grid);
    g.drawRoundedRectangle (b, 5.0f, 1.0f);
}

void WhyshoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    auto bounds = b.getLocalBounds().toFloat();
    auto box = bounds.removeFromLeft (20.0f).withSizeKeepingCentre (16.0f, 16.0f);
    g.setColour (b.getToggleState() ? theme().accent : theme().panel2);
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (theme().grid);
    g.drawRoundedRectangle (box, 4.0f, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (theme().bg);
        juce::Path tick;
        auto t = box.reduced (3.0f);
        tick.startNewSubPath (t.getX(), t.getCentreY());
        tick.lineTo (t.getCentreX() - 1, t.getBottom());
        tick.lineTo (t.getRight(), t.getY());
        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }
    g.setColour (hl ? theme().text : theme().textDim);
    g.setFont (11.5f);
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (3), juce::Justification::centredLeft);
}
