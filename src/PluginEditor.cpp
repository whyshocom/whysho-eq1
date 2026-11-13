#include "PluginEditor.h"

using namespace wsho;
using namespace wsho::gui;

WhyshoEQAudioProcessorEditor::WhyshoEQAudioProcessorEditor (WhyshoEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), analyzer (p), bandEditor (p)
{
    setLookAndFeel (&lnf);

    brand.setText ("WHYSHO  EQ1", juce::dontSendNotification);
    brand.setFont (juce::Font (20.0f, juce::Font::bold));
    brand.setColour (juce::Label::textColourId, theme().accent);
    addAndMakeVisible (brand);

    addAndMakeVisible (analyzer);
    addAndMakeVisible (bandEditor);
    analyzer.onBandSelected = [this] (int b) { bandEditor.setBand (b); };

    // global knobs: in, out, mix, scale
    addKnob ("In");    sa.add (new SA (proc.state(), pid::inGain,    *knobs[0]));
    addKnob ("Out");   sa.add (new SA (proc.state(), pid::outGain,   *knobs[1]));
    addKnob ("Mix");   sa.add (new SA (proc.state(), pid::mix,       *knobs[2]));
    addKnob ("Scale"); sa.add (new SA (proc.state(), pid::gainScale, *knobs[3]));

    // combos: phase, os, analyzer
    addCombo (kPhaseModeNames);      ca.add (new CA (proc.state(), pid::phase,    *combos[0]));
    addCombo (kOversampleNames);     ca.add (new CA (proc.state(), pid::os,       *combos[1]));
    addCombo (kAnalyzerSourceNames); ca.add (new CA (proc.state(), pid::analyzer, *combos[2]));

    // toggles: bypass, autogain, sc listen
    addToggle ("Bypass");   ba.add (new BA (proc.state(), pid::bypass,   *toggles[0]));
    addToggle ("Auto Gain");ba.add (new BA (proc.state(), pid::autoGain, *toggles[1]));
    addToggle ("SC Listen");ba.add (new BA (proc.state(), pid::scListen, *toggles[2]));

    addAndMakeVisible (licenseBtn);
    licenseBtn.setComponentID ("license");
    licenseBtn.onClick = [this] { openLicense(); };

    // Theme selector (UI preference, persisted in plugin state).
    themeBox.addItemList (themeNames(), 1);
    const int savedTheme = (int) proc.state().state.getProperty ("uiTheme", 0);
    setThemeIndex (savedTheme);
    themeBox.setSelectedId (currentThemeIndex() + 1, juce::dontSendNotification);
    themeBox.onChange = [this] { applyThemeChange (themeBox.getSelectedId() - 1); };
    addAndMakeVisible (themeBox);
    applyThemeChange (currentThemeIndex());

    setResizable (true, true);
    setResizeLimits (860, 540, 1800, 1200);
    setSize (1020, 640);
    startTimerHz (4);
}

WhyshoEQAudioProcessorEditor::~WhyshoEQAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Slider& WhyshoEQAudioProcessorEditor::addKnob (const juce::String& label)
{
    auto* s = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 13);
    addAndMakeVisible (s); knobs.add (s);
    auto* l = new juce::Label ({}, label);
    l->setJustificationType (juce::Justification::centred);
    l->setFont (10.0f); l->setColour (juce::Label::textColourId, theme().textDim);
    addAndMakeVisible (l); knobLabels.add (l);
    return *s;
}
juce::ComboBox& WhyshoEQAudioProcessorEditor::addCombo (const juce::StringArray& items)
{
    auto* c = new juce::ComboBox(); c->addItemList (items, 1);
    addAndMakeVisible (c); combos.add (c); return *c;
}
juce::ToggleButton& WhyshoEQAudioProcessorEditor::addToggle (const juce::String& t)
{
    auto* b = new juce::ToggleButton (t); addAndMakeVisible (b); toggles.add (b); return *b;
}

void WhyshoEQAudioProcessorEditor::applyThemeChange (int index)
{
    setThemeIndex (index);
    proc.state().state.setProperty ("uiTheme", index, nullptr);
    lnf.applyTheme();
    sendLookAndFeelChange();   // propagate to children (refreshes label colours)
    repaint();
}

void WhyshoEQAudioProcessorEditor::lookAndFeelChanged()
{
    brand.setColour (juce::Label::textColourId, theme().accent);
    for (auto* l : knobLabels)
        l->setColour (juce::Label::textColourId, theme().textDim);
}

void WhyshoEQAudioProcessorEditor::openLicense()
{
    if (licenseDialog) return;
    licenseDialog = std::make_unique<LicenseDialog> (proc);
    addAndMakeVisible (*licenseDialog);
    licenseDialog->setBounds (getLocalBounds());
    licenseDialog->onClose = [this] { licenseDialog.reset(); repaint(); };
}

void WhyshoEQAudioProcessorEditor::timerCallback()
{
    const auto& i = proc.licenseInfo;
    licenseBtn.setColour (juce::TextButton::textColourOffId,
                          i.state == lic::LicenseState::Licensed ? theme().good
                        : i.state == lic::LicenseState::Trial    ? theme().warn : theme().bad);
    licenseBtn.setButtonText (i.state == lic::LicenseState::Licensed ? "Licensed"
                            : i.state == lic::LicenseState::Trial    ? "Trial" : "Activate");
}

void WhyshoEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    g.setColour (theme().panel);
    g.fillRect (0, 0, getWidth(), 54);                    // top bar
}

void WhyshoEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (54).reduced (8, 6);

    brand.setBounds (top.removeFromLeft (150));
    licenseBtn.setBounds (top.removeFromRight (90).reduced (2, 4));
    themeBox.setBounds (top.removeFromRight (104).reduced (3, 4));

    auto placeCombo = [&] (int idx, const juce::String& cap, int w)
    {
        auto cell = top.removeFromLeft (w).reduced (3, 2);
        combos[idx]->setBounds (cell);
        juce::ignoreUnused (cap);
    };
    auto placeKnob = [&] (int idx, int w)
    {
        auto cell = top.removeFromLeft (w).reduced (2, 0);
        knobLabels[idx]->setBounds (cell.removeFromBottom (12));
        knobs[idx]->setBounds (cell);
    };

    for (int i = 0; i < 4; ++i) placeKnob (i, 52);
    top.removeFromLeft (6);
    placeCombo (0, "Phase", 108);
    placeCombo (1, "OS", 70);
    placeCombo (2, "Analyzer", 96);
    top.removeFromLeft (6);
    for (int i = 0; i < 3; ++i) toggles[i]->setBounds (top.removeFromLeft (84).reduced (2, 6));

    auto bottom = r.removeFromBottom (150).reduced (8, 6);
    bandEditor.setBounds (bottom);
    analyzer.setBounds (r.reduced (8, 6));

    if (licenseDialog) licenseDialog->setBounds (getLocalBounds());
}
