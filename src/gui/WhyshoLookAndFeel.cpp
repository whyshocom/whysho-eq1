#include "WhyshoLookAndFeel.h"

using namespace wsho::gui;

WhyshoLookAndFeel::WhyshoLookAndFeel() { applyTheme(); }

void WhyshoLookAndFeel::applyTheme()
{
    const auto& t = theme();

    // Rebuild every JUCE default from the theme first. Without this, any colour
    // we don't name below falls back to JUCE's dark scheme (white text), which
    // is unreadable on the light Paper theme.
    setColourScheme ({ t.bg, t.panel2, t.panel2, t.grid, t.text, t.accent,
                       onColour (t.accent), t.accent, t.text });

    setColour (juce::ResizableWindow::backgroundColourId, t.bg);
    setColour (juce::Label::textColourId, t.text);

    setColour (juce::TextButton::buttonColourId, t.panel2);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, onColour (t.accent));

    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, t.accent.withAlpha (0.3f));

    setColour (juce::ComboBox::backgroundColourId, t.panel2);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.grid);
    setColour (juce::ComboBox::arrowColourId, t.text);

    setColour (juce::PopupMenu::backgroundColourId, t.panel2);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);

    setColour (juce::TextEditor::backgroundColourId, t.panel2);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.grid);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::CaretComponent::caretColourId, t.text);

    setColour (juce::AlertWindow::backgroundColourId, t.panel);
    setColour (juce::AlertWindow::textColourId, t.text);
    setColour (juce::AlertWindow::outlineColourId, t.grid);

    setColour (juce::TooltipWindow::backgroundColourId, t.text);
    setColour (juce::TooltipWindow::textColourId, t.bg);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId, t.textDim);
}

juce::Font WhyshoLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

// A disabled control is drawn faded: the same rule for every control and theme.
// (Text that lives in a child Label - knob values, combo text - is faded by JUCE.)
namespace
{
struct DisabledFade
{
    DisabledFade (juce::Graphics& gr, const juce::Component& c) : g (gr), active (! c.isEnabled())
    {
        if (active) g.beginTransparencyLayer (0.4f);
    }
    ~DisabledFade() { if (active) g.endTransparencyLayer(); }
    juce::Graphics& g;
    const bool active;
};
}

void WhyshoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> (x, y, w, h).reduced (6.0f);
    const auto r = juce::jmax (8.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const DisabledFade fade (g, s);

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
    g.setColour (s.isEnabled() ? theme().accent : theme().textDim);
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

void WhyshoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const DisabledFade fade (g, box);
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
    const DisabledFade fade (g, bt);
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
    const auto& t = theme();
    const bool on = b.getToggleState();
    auto bounds = b.getLocalBounds().toFloat();
    const DisabledFade fade (g, b);

    // --- pill switch ---
    const float h = juce::jmin (16.0f, bounds.getHeight());
    auto track = bounds.removeFromLeft (30.0f).withSizeKeepingCentre (30.0f, h);
    g.setColour (on ? t.accent : (t.light ? t.grid : t.panel2));
    g.fillRoundedRectangle (track, h * 0.5f);
    if (! on) { g.setColour (t.grid); g.drawRoundedRectangle (track, h * 0.5f, 1.0f); }

    const float knobR = h * 0.5f - 2.0f;
    const float kx = on ? track.getRight() - knobR - 2.0f : track.getX() + knobR + 2.0f;
    g.setColour (on ? juce::Colours::white : t.textDim);
    g.fillEllipse (kx - knobR, track.getCentreY() - knobR, knobR * 2, knobR * 2);

    // --- label ---
    g.setColour (on || hl ? t.text : t.textDim);
    g.setFont (themeFont (11.0f));
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (6), juce::Justification::centredLeft);
}
