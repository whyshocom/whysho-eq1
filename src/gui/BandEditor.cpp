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
    addToggle ("On").setTooltip ("Switch this band on or off");   // 3

    // What is usable depends on the band's on / type / dynamic settings.
    combos[0]->onChange = [this] { updateEnablement(); };
    toggles[1]->onClick = [this] { updateEnablement(); };
    toggles[3]->onClick = [this] { updateEnablement(); };

    setBand (0);
    lookAndFeelChanged();
}

void BandEditor::lookAndFeelChanged()
{
    title.setFont (themeFont (14.0f, true));
    updateEnablement();   // also colours the title
    for (auto* k : knobs)
    {
        k->label.setColour (juce::Label::textColourId, theme().textDim);
        k->label.setFont (themeFont (10.5f));
    }
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
    ba.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (s, mk (pid::on),    *toggles[3]));
    updateEnablement();
}

// An off band reads as off (only its power switch stays live), and controls
// that do nothing for the current filter type / with dynamics off are disabled.
void BandEditor::updateEnablement()
{
    const bool on  = toggles[3]->getToggleState();
    const bool dyn = on && toggles[1]->getToggleState();
    const auto type = (FilterType) combos[0]->getSelectedItemIndex();
    const bool isCut   = type == FilterType::HighPass || type == FilterType::LowPass;
    const bool hasGain = ! (isCut || type == FilterType::Notch || type == FilterType::BandPass
                                  || type == FilterType::AllPass);

    const bool knobOn[] = { on, on && hasGain, on, dyn, dyn, dyn, dyn };
    for (int i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->slider.setEnabled (knobOn[i]);
        knobs[i]->label.setEnabled (knobOn[i]);
    }
    combos[0]->setEnabled (on);
    combos[1]->setEnabled (on && isCut);    // slope
    combos[2]->setEnabled (on);
    toggles[0]->setEnabled (on);
    toggles[1]->setEnabled (on);
    toggles[2]->setEnabled (dyn);           // sc key

    title.setColour (juce::Label::textColourId, on ? theme().accent : theme().textDim);
    repaint();   // the DYNAMICS caption
}

void BandEditor::paint (juce::Graphics& g)
{
    g.setColour (theme().panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // separator between static and dynamic sections
    g.setColour (theme().grid);
    g.drawVerticalLine (getWidth() * 55 / 100, 8.0f, getHeight() - 8.0f);
    g.setColour (theme().textDim.withMultipliedAlpha (toggles[1]->isEnabled() && toggles[1]->getToggleState() ? 1.0f : 0.5f));
    g.setFont (themeFont (10.0f, true));
    g.drawText ("DYNAMICS", getWidth() * 55 / 100 + 6, 4, 90, 12, juce::Justification::left);
}

void BandEditor::resized()
{
    auto b = getLocalBounds().reduced (10);
    auto head = b.removeFromTop (18);
    title.setBounds (head.removeFromLeft (78));
    toggles[3]->setBounds (head.removeFromLeft (70));   // on

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
