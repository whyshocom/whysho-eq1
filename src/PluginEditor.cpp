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
    addToggle ("Bypass").setTooltip ("Bypass the whole plugin");
    ba.add (new BA (proc.state(), pid::bypass,   *toggles[0]));
    addToggle ("Auto").setTooltip ("Auto output-gain compensation");
    ba.add (new BA (proc.state(), pid::autoGain, *toggles[1]));
    addToggle ("Listen").setTooltip ("Monitor the sidechain input");
    ba.add (new BA (proc.state(), pid::scListen, *toggles[2]));

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

    // Preset bar
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &abBtn, &copyBtn }) addAndMakeVisible (b);
    addAndMakeVisible (presetBox);
    rebuildPresetMenu();
    presetBox.onChange = [this] { applyPresetSelection(); };
    prevBtn.onClick = [this] { selectPresetDelta (-1); };
    nextBtn.onClick = [this] { selectPresetDelta (+1); };
    saveBtn.onClick = [this] { doSavePreset(); };
    abBtn.onClick   = [this] { toggleAB(); };
    copyBtn.onClick = [this] { proc.presetManager().captureTo (! slotB); };
    proc.presetManager().captureTo (false); // seed slot A with current state

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
    brand.setFont (themeFont (19.0f, true));
    for (auto* l : knobLabels)
    {
        l->setColour (juce::Label::textColourId, theme().textDim);
        l->setFont (themeFont (10.0f));
    }
}

void WhyshoEQAudioProcessorEditor::rebuildPresetMenu()
{
    auto& pm = proc.presetManager();
    presetBox.clear (juce::dontSendNotification);
    auto fac = pm.factoryNames();
    auto usr = pm.userNames();

    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < fac.size(); ++i) presetBox.addItem (fac[i], i + 1);          // ids 1..F
    if (! usr.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < usr.size(); ++i) presetBox.addItem (usr[i], 100 + i);    // ids 100+
    }
    // reflect current
    const auto cur = pm.current();
    for (int i = 0; i < fac.size(); ++i) if (fac[i] == cur) presetBox.setSelectedId (i + 1, juce::dontSendNotification);
    for (int i = 0; i < usr.size(); ++i) if (usr[i] == cur) presetBox.setSelectedId (100 + i, juce::dontSendNotification);
}

void WhyshoEQAudioProcessorEditor::applyPresetSelection()
{
    auto& pm = proc.presetManager();
    const int id = presetBox.getSelectedId();
    if (id >= 1 && id < 100) { auto f = pm.factoryNames(); if (id - 1 < f.size()) pm.applyFactory (f[id - 1]); }
    else if (id >= 100)      { auto u = pm.userNames();    if (id - 100 < u.size()) pm.applyUser (u[id - 100]); }
    bandEditor.setBand (0);
}

void WhyshoEQAudioProcessorEditor::selectPresetDelta (int d)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (auto id = presetBox.getItemId (i)) ids.add (id);
    if (ids.isEmpty()) return;
    int idx = ids.indexOf (presetBox.getSelectedId());
    idx = juce::jlimit (0, ids.size() - 1, (idx < 0 ? 0 : idx) + d);
    presetBox.setSelectedId (ids[idx]); // triggers onChange
}

void WhyshoEQAudioProcessorEditor::doSavePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Name:", juce::MessageBoxIconType::NoIcon);
    saveDialog->addTextEditor ("name", "My Preset");
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1)
        {
            auto name = saveDialog->getTextEditorContents ("name").trim();
            if (name.isNotEmpty()) { proc.presetManager().saveUser (name); rebuildPresetMenu(); }
        }
        saveDialog->exitModalState (r);
        saveDialog.reset();
    }), false);
}

void WhyshoEQAudioProcessorEditor::toggleAB()
{
    auto& pm = proc.presetManager();
    pm.captureTo (slotB);                 // store current into the active slot
    slotB = ! slotB;
    if (! pm.hasSlot (slotB)) pm.captureTo (slotB); // seed empty slot
    pm.recallFrom (slotB);
    abBtn.setButtonText (slotB ? "B" : "A");
    bandEditor.setBand (0);
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

    // --- preset bar (second row) ---
    auto pr = r.removeFromTop (34).reduced (8, 4);
    prevBtn.setBounds  (pr.removeFromLeft (28).reduced (1));
    presetBox.setBounds (pr.removeFromLeft (240).reduced (2, 1));
    nextBtn.setBounds  (pr.removeFromLeft (28).reduced (1));
    pr.removeFromLeft (10);
    saveBtn.setBounds  (pr.removeFromLeft (64).reduced (2, 1));
    pr.removeFromLeft (10);
    abBtn.setBounds    (pr.removeFromLeft (44).reduced (2, 1));
    copyBtn.setBounds  (pr.removeFromLeft (60).reduced (2, 1));

    auto bottom = r.removeFromBottom (150).reduced (8, 6);
    bandEditor.setBounds (bottom);
    analyzer.setBounds (r.reduced (8, 6));

    if (licenseDialog) licenseDialog->setBounds (getLocalBounds());
}
