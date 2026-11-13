#include "BandEditor.h"
#include "WhyshoLookAndFeel.h"

using namespace wsho;
using namespace wsho::gui;

BandEditor::BandEditor (WhyshoEQAudioProcessor& p) : proc (p)
{
    addAndMakeVisible (title);
    title.setFont (juce::Font (15.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme().accent);

    // knobs: 0 freq, 1 gain, 2 q, 3 dThr, 4 dRng, 5 dAtt, 6 dRel
    const char* knobNames[] = { "Freq", "Gain", "Q", "Threshold", "Range", "Attack", "Release" };
    for (auto* nm : knobNames) addKnob (nm);

    addCombo (kFilterTypeNames);    // 0 type
    addCombo (kSlopeNames);         // 1 slope
    addCombo (kChannelModeNames);   // 2 chan

    addToggle ("Solo");             // 0
    addToggle ("Dynamic");          // 1
    addToggle ("SC Key");           // 2

    setBand (0);
    lookAndFeelChanged();
}

void BandEditor::lookAndFeelChanged()
{
    title.setColour (juce::Label::textColourId, theme().accent);
    for (auto* k : knobs)
        k->label.setColour (juce::Label::textColourId, theme().textDim);
    repaint();
}

BandEditor::Knob& BandEditor::addKnob (const juce::String& name)
{
    auto* k = new Knob();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    addAndMakeVisible (k->slider);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (11.0f);
    k->label.setColour (juce::Label::textColourId, theme().textDim);
    addAndMakeVisible (k->label);
    knobs.add (k);
    return *k;
}

juce::ComboBox& BandEditor::addCombo (const juce::StringArray& items)
{
    auto* c = new juce::ComboBox();
    c->addItemList (items, 1);
    addAndMakeVisible (c);
    combos.add (c);
    return *c;
}

juce::ToggleButton& BandEditor::addToggle (const juce::String& name)
{
    auto* t = new juce::ToggleButton (name);
    addAndMakeVisible (t);
    toggles.add (t);
    return *t;
}

void BandEditor::setBand (int index)
{
    band = juce::jlimit (0, kNumBands - 1, index);
    title.setText ("BAND " + juce::String (band + 1), juce::dontSendNotification);

    sa.clear(); ca.clear(); ba.clear();
    auto& s = proc.state();
    auto mk = [&] (const char* suf) { return pid::band (band, suf); };

    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::freq),      knobs[0]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::gain),      knobs[1]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::q),         knobs[2]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::dynThresh), knobs[3]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::dynRange),  knobs[4]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::dynAttack), knobs[5]->slider));
    sa.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, mk (pid::dynRelease),knobs[6]->slider));

    ca.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (s, mk (pid::type),  *combos[0]));
    ca.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (s, mk (pid::slope), *combos[1]));
    ca.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (s, mk (pid::chan),  *combos[2]));

    ba.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (s, mk (pid::solo),  *toggles[0]));
    ba.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (s, mk (pid::dyn),   *toggles[1]));
    ba.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (s, mk (pid::dynSC), *toggles[2]));
}

void BandEditor::paint (juce::Graphics& g)
{
    g.setColour (theme().panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // separator between static and dynamic sections
    g.setColour (theme().grid);
    g.drawVerticalLine (getWidth() * 55 / 100, 8.0f, getHeight() - 8.0f);
    g.setColour (theme().textDim);
    g.setFont (10.0f);
    g.drawText ("DYNAMICS", getWidth() * 55 / 100 + 6, 4, 80, 12, juce::Justification::left);
}

void BandEditor::resized()
{
    auto b = getLocalBounds().reduced (10);
    title.setBounds (b.removeFromTop (18).removeFromLeft (120));

    auto row = b;
    auto combosArea = row.removeFromLeft (row.getWidth() * 55 / 100);

    // left: type/slope/chan combos + solo on top, then freq/gain/q knobs
    auto top = combosArea.removeFromTop (28);
    combos[0]->setBounds (top.removeFromLeft (120).reduced (2)); // type
    combos[2]->setBounds (top.removeFromLeft (90).reduced (2));  // chan
    combos[1]->setBounds (top.removeFromLeft (100).reduced (2)); // slope
    toggles[0]->setBounds (top.removeFromLeft (70).reduced (2)); // solo

    auto knobW = combosArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto col = combosArea.removeFromLeft (knobW);
        knobs[i]->label.setBounds (col.removeFromTop (14));
        knobs[i]->slider.setBounds (col.reduced (4));
    }

    // right: dynamics
    auto top2 = row.removeFromTop (28);
    toggles[1]->setBounds (top2.removeFromLeft (90).reduced (2)); // dynamic
    toggles[2]->setBounds (top2.removeFromLeft (80).reduced (2)); // sc key
    auto dknobW = row.getWidth() / 4;
    for (int i = 3; i < 7; ++i)
    {
        auto col = row.removeFromLeft (dknobW);
        knobs[i]->label.setBounds (col.removeFromTop (14));
        knobs[i]->slider.setBounds (col.reduced (4));
    }
}
