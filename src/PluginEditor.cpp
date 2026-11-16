#include "PluginEditor.h"

using namespace wsho;
using namespace wsho::gui;

namespace
{
constexpr int kMinW = 940, kMinH = 580, kMaxW = 1800, kMaxH = 1200;
constexpr int kDefaultW = 1020, kDefaultH = 640;

// The window size is, like the theme, one preference for this computer (not
// part of a project or preset): a small file next to the theme's.
juce::File windowSizeFile() { return themePreferenceFile().getSiblingFile ("window.txt"); }

// The size to open with: the last one the user set, kept inside the resize
// limits and on the screen (it may have been saved on a bigger display).
juce::Point<int> loadWindowSize()
{
    int w = kDefaultW, h = kDefaultH;
    const auto saved = juce::StringArray::fromTokens (windowSizeFile().loadFileAsString(), " \r\n", "");
    if (saved.size() == 2 && saved[0].getIntValue() > 0 && saved[1].getIntValue() > 0)
    {
        w = saved[0].getIntValue();
        h = saved[1].getIntValue();
    }
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        w = juce::jmin (w, d->userArea.getWidth());
        h = juce::jmin (h, d->userArea.getHeight());
    }
    return { juce::jlimit (kMinW, kMaxW, w), juce::jlimit (kMinH, kMaxH, h) };
}

void saveWindowSize (juce::Point<int> size)
{
    auto f = windowSizeFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::String (size.x) + " " + juce::String (size.y));
}

juce::String shortcutText (const juce::String& keys)
{
   #if JUCE_MAC
    return " (Cmd+" + keys + ")";
   #else
    return " (Ctrl+" + keys + ")";
   #endif
}
} // namespace

WhyshoEQAudioProcessorEditor::WhyshoEQAudioProcessorEditor (WhyshoEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), analyzer (p), bandEditor (p)
{
    setLookAndFeel (&lnf);

    brand.setText ("whysho EQ1", juce::dontSendNotification);
    brand.setFont (juce::Font (20.0f, juce::Font::bold));
    brand.setColour (juce::Label::textColourId, theme().accent);
    addAndMakeVisible (brand);

    addAndMakeVisible (analyzer);
    analyzer.setComponentID ("analyzer");
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

    // Theme selector: one preference for every EQ1 on this computer.
    loadThemePreference();
    themeBox.addItemList (themeNames(), 1);
    themeBox.onChange = [this] { chooseTheme (themeBox.getSelectedId() - 1); };
    themeBox.setTooltip ("Colour theme");
    addAndMakeVisible (themeBox);
    themeBroadcaster().addChangeListener (this);
    applyTheme();

    // Preset bar
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &presetMenuBtn, &undoBtn, &redoBtn, &abBtn, &copyBtn })
        addAndMakeVisible (b);
    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("No preset");
    rebuildPresetMenu();
    presetBox.onChange = [this] { applyPresetSelection(); };
    prevBtn.onClick = [this] { selectPresetDelta (-1); };
    nextBtn.onClick = [this] { selectPresetDelta (+1); };
    saveBtn.onClick = [this] { askSavePreset(); };
    saveBtn.setComponentID ("presetSave");
    saveBtn.setTooltip ("Save the settings as a user preset");
    presetMenuBtn.onClick = [this] { showPresetMenu(); };
    presetMenuBtn.setTooltip ("Save as, rename or delete a user preset");
    undoBtn.onClick = [this] { undoOrRedo (false); };
    undoBtn.setComponentID ("undo");
    redoBtn.setComponentID ("redo");
    abBtn.setComponentID ("ab");
    undoBtn.setTooltip ("Undo" + shortcutText ("Z"));
    redoBtn.onClick = [this] { undoOrRedo (true); };
    redoBtn.setTooltip ("Redo" + shortcutText ("Shift+Z"));
    abBtn.onClick   = [this] { toggleAB(); };
    abBtn.setTooltip ("Switch between the A and B settings");
    copyBtn.onClick = [this] { proc.presetManager().copyToOtherSlot(); };
    copyBtn.setTooltip ("Copy the settings to the other A/B slot");

    setResizable (true, true);
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);
    rememberedSize = loadWindowSize();
    setSize (rememberedSize.x, rememberedSize.y);
    proc.license().markUsed();
    proc.license().addChangeListener (this);
    updateLicenseUi();

    timerCallback();
    startTimerHz (10);
}

WhyshoEQAudioProcessorEditor::~WhyshoEQAudioProcessorEditor()
{
    stopTimer();
    if (userResized) rememberWindowSize();
    proc.license().removeChangeListener (this);
    themeBroadcaster().removeChangeListener (this);
    if (dialog) dialog->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Slider& WhyshoEQAudioProcessorEditor::addKnob (const juce::String& label)
{
    auto* s = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
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

void WhyshoEQAudioProcessorEditor::applyTheme()
{
    themeBox.setSelectedId (currentThemeIndex() + 1, juce::dontSendNotification);
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

// ---- undo / redo, and everything else that follows the processor ------------
void WhyshoEQAudioProcessorEditor::timerCallback()
{
    auto& um = proc.undoManager();

    // One drag / one click = one undo step: close the step once the mouse is up.
    // Parameter changes reach the state tree (where undo records them) on a
    // timer of their own, so push pending ones through first (copyState() does
    // that), or the end of a drag would land in the next step.
    const bool mouseDown = juce::Component::isMouseButtonDownAnywhere();
    if (! mouseDown && (mouseWasDown || um.getNumActionsInCurrentTransaction() > 0))
    {
        proc.state().copyState();
        um.beginNewTransaction();
    }
    mouseWasDown = mouseDown;

    undoBtn.setEnabled (um.canUndo());
    redoBtn.setEnabled (um.canRedo());

    // Undo can bring back the other A/B slot and the previous preset name.
    abBtn.setButtonText (proc.presetManager().slotB() ? "B" : "A");
    refreshPresetDisplay();

    // Remember the size the user dragged the window to, once the drag is over.
    if (userResized && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        rememberWindowSize();
}

void WhyshoEQAudioProcessorEditor::rememberWindowSize()
{
    userResized = false;
    const juce::Point<int> size (getWidth(), getHeight());
    if (size != std::exchange (rememberedSize, size))
        saveWindowSize (size);
}

void WhyshoEQAudioProcessorEditor::undoOrRedo (bool redo)
{
    auto& um = proc.undoManager();
    proc.state().copyState();   // a change not yet in the tree belongs to the step being undone
    if (redo) um.redo(); else um.undo();
    timerCallback();
}

bool WhyshoEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // commandModifier is Cmd on macOS and Ctrl elsewhere.
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('z', cmd, 0)) { undoOrRedo (false); return true; }
    if (key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', cmd, 0)) { undoOrRedo (true); return true; }
    return false;
}

// ---- presets ----------------------------------------------------------------
void WhyshoEQAudioProcessorEditor::rebuildPresetMenu()
{
    auto& pm = proc.presetManager();
    presetBox.clear (juce::dontSendNotification);
    factoryPresets = pm.factoryNames();
    userPresets = pm.userNames();

    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < factoryPresets.size(); ++i) presetBox.addItem (factoryPresets[i], i + 1);   // ids 1..F
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < userPresets.size(); ++i) presetBox.addItem (userPresets[i], 100 + i);   // ids 100+
    }
    shownPreset = "\n";   // never a real name: forces the refresh
    refreshPresetDisplay();
}

int WhyshoEQAudioProcessorEditor::presetId (const juce::String& name) const
{
    if (const int i = userPresets.indexOf (name); i >= 0)    return 100 + i;
    if (const int i = factoryPresets.indexOf (name); i >= 0) return i + 1;
    return 0;
}

bool WhyshoEQAudioProcessorEditor::userPresetIsCurrent() const
{
    return userPresets.contains (proc.presetManager().current());
}

// Shows the current preset; with a "*" once the settings have changed since it was loaded.
void WhyshoEQAudioProcessorEditor::refreshPresetDisplay()
{
    auto& pm = proc.presetManager();
    const auto name = pm.current();
    const auto shown = name.isEmpty() ? juce::String() : name + (pm.isModified() ? " *" : "");
    if (shown == shownPreset) return;
    shownPreset = shown;

    const int id = pm.isModified() ? 0 : presetId (name);
    if (id != 0) presetBox.setSelectedId (id, juce::dontSendNotification);
    else         presetBox.setText (shown, juce::dontSendNotification);
}

void WhyshoEQAudioProcessorEditor::applyPresetSelection()
{
    auto& pm = proc.presetManager();
    const int id = presetBox.getSelectedId();
    if (id >= 1 && id < 100) pm.applyFactory (factoryPresets[id - 1]);
    else if (id >= 100)      pm.applyUser (userPresets[id - 100]);
    bandEditor.setBand (0);
    timerCallback();
}

void WhyshoEQAudioProcessorEditor::selectPresetDelta (int d)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (auto id = presetBox.getItemId (i)) ids.add (id);
    if (ids.isEmpty()) return;
    int idx = ids.indexOf (presetId (proc.presetManager().current()));
    idx = juce::jlimit (0, ids.size() - 1, (idx < 0 ? 0 : idx) + d);
    presetBox.setSelectedId (ids[idx]); // triggers onChange
}

// Every dialog goes through here. The host can close the editor while one is
// open, so the callback only ever reaches the editor through a SafePointer.
void WhyshoEQAudioProcessorEditor::showDialog (const juce::String& title, const juce::String& message,
                                               const juce::String& okText, DialogAction onOk,
                                               bool askForName, const juce::String& initialName)
{
    if (dialog) dialog->setLookAndFeel (nullptr);
    dialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    if (askForName) dialog->addTextEditor ("name", initialName);
    if (onOk)
    {
        dialog->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }
    else
        dialog->addButton (okText, 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<WhyshoEQAudioProcessorEditor> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, opened = dialog.get(), askForName, action = std::move (onOk)] (int result)
    {
        // Gone, or already replaced by a newer dialog: nothing to do.
        if (safe == nullptr || safe->dialog.get() != opened) return;
        const auto text = askForName ? safe->dialog->getTextEditorContents ("name") : juce::String();
        safe->dialog->setLookAndFeel (nullptr);
        safe->dialog.reset();
        if (result == 1 && action) action (*safe.getComponent(), text);
    }), false);
}

void WhyshoEQAudioProcessorEditor::showMessage (const juce::String& title, const juce::String& message)
{
    showDialog (title, message, "OK", {});
}

void WhyshoEQAudioProcessorEditor::showPresetMenu()
{
    const bool user = userPresetIsCurrent();   // factory presets cannot be renamed or deleted
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addItem (1, "Save as...");
    m.addItem (2, "Rename...", user);
    m.addItem (3, "Delete...", user);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenuBtn),
                     [safe = juce::Component::SafePointer<WhyshoEQAudioProcessorEditor> (this)] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->askSavePreset();
        if (choice == 2) safe->askRenamePreset();
        if (choice == 3) safe->askDeletePreset();
    });
}

void WhyshoEQAudioProcessorEditor::askSavePreset()
{
    showDialog ("Save Preset", "Name:", "Save",
                [] (WhyshoEQAudioProcessorEditor& ed, const juce::String& typed) { ed.savePreset (typed); },
                true, userPresetIsCurrent() ? proc.presetManager().current() : juce::String ("My Preset"));
}

void WhyshoEQAudioProcessorEditor::savePreset (const juce::String& typedName)
{
    auto& pm = proc.presetManager();
    const auto name = PresetManager::legalName (typedName);   // a plain file name (no "../", no slashes)

    if (name.isEmpty())      { showMessage ("Save Preset", "The preset needs a name."); return; }
    if (pm.isFactory (name)) { showMessage ("Save Preset", "\"" + name + "\" is a factory preset. Please use another name."); return; }

    auto save = [name] (WhyshoEQAudioProcessorEditor& ed, const juce::String&)
    {
        if (! ed.proc.presetManager().saveUser (name))
            ed.showMessage ("Save Preset", "The preset could not be saved.");
        ed.rebuildPresetMenu();
    };

    if (pm.userExists (name))
        showDialog ("Replace Preset?", "A preset named \"" + name + "\" already exists.", "Replace", save);
    else
        save (*this, {});
}

void WhyshoEQAudioProcessorEditor::askRenamePreset()
{
    if (! userPresetIsCurrent()) return;
    const auto from = proc.presetManager().current();
    showDialog ("Rename Preset", "New name for \"" + from + "\":", "Rename",
                [from] (WhyshoEQAudioProcessorEditor& ed, const juce::String& typed) { ed.renamePreset (from, typed); },
                true, from);
}

void WhyshoEQAudioProcessorEditor::renamePreset (const juce::String& from, const juce::String& typedName)
{
    auto& pm = proc.presetManager();
    const auto to = PresetManager::legalName (typedName);

    if (to == from)        return;
    if (to.isEmpty())      { showMessage ("Rename Preset", "The preset needs a name."); return; }
    if (pm.isFactory (to)) { showMessage ("Rename Preset", "\"" + to + "\" is a factory preset. Please use another name."); return; }

    auto rename = [from, to] (WhyshoEQAudioProcessorEditor& ed, const juce::String&)
    {
        if (! ed.proc.presetManager().renameUser (from, to))
            ed.showMessage ("Rename Preset", "The preset could not be renamed.");
        ed.rebuildPresetMenu();
    };

    if (pm.userExists (to))
        showDialog ("Replace Preset?", "A preset named \"" + to + "\" already exists.", "Replace", rename);
    else
        rename (*this, {});
}

void WhyshoEQAudioProcessorEditor::askDeletePreset()
{
    if (! userPresetIsCurrent()) return;
    const auto name = proc.presetManager().current();
    showDialog ("Delete Preset?", "Delete \"" + name + "\"? This cannot be undone.", "Delete",
                [name] (WhyshoEQAudioProcessorEditor& ed, const juce::String&)
    {
        if (! ed.proc.presetManager().deleteUser (name))
            ed.showMessage ("Delete Preset", "The preset could not be deleted.");
        ed.rebuildPresetMenu();
    });
}

void WhyshoEQAudioProcessorEditor::toggleAB()
{
    proc.presetManager().toggleSlot();
    bandEditor.setBand (0);
    timerCallback();
}

void WhyshoEQAudioProcessorEditor::openLicense()
{
    if (licenseDialog) return;
    licenseDialog = std::make_unique<LicenseDialog> (proc);
    addAndMakeVisible (*licenseDialog);
    licenseDialog->setBounds (getLocalBounds());
    licenseDialog->toFront (true);
    const juce::Component::SafePointer<WhyshoEQAudioProcessorEditor> safe (this);
    licenseDialog->onClose = [safe]
    {
        // Deferred: the dialog must not be destroyed from inside its own callback.
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) { safe->licenseDialog.reset(); safe->repaint(); }
        });
    };
}

void WhyshoEQAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &themeBroadcaster()) applyTheme();
    else                               updateLicenseUi();
}

void WhyshoEQAudioProcessorEditor::updateLicenseUi()
{
    using Kind = lic::Verdict::Kind;
    const auto s = proc.license().status();

    juce::String text = "Sign in";
    if (s.verdict.kind == Kind::licensed)   text = "Licensed";
    else if (s.verdict.kind == Kind::trial) text = "Trial: " + juce::String (s.verdict.trialDaysLeft) + "d";
    licenseBtn.setButtonText (text);
    licenseBtn.setTooltip ("Your whysho account and license");

    // Trial over, no license: the plugin is off, and the account panel stays up.
    if (! s.unlocked()) openLicense();
}

void WhyshoEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    g.setColour (theme().panel);
    g.fillRect (0, 0, getWidth(), kHeaderH);              // top bar
}

void WhyshoEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kHeaderH).reduced (8, 6);

    brand.setBounds (top.removeFromLeft (150));

    auto placeCombo = [&] (int idx, const juce::String& cap, int w)
    {
        auto cell = top.removeFromLeft (w).reduced (3, 2);
        combos[idx]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 30));
        juce::ignoreUnused (cap);
    };
    auto placeKnob = [&] (int idx, int w)
    {
        auto cell = top.removeFromLeft (w).reduced (2, 0);
        knobLabels[idx]->setBounds (cell.removeFromBottom (12));
        knobs[idx]->setBounds (cell);
    };

    for (int i = 0; i < 4; ++i) placeKnob (i, 58);
    top.removeFromLeft (6);
    placeCombo (0, "Phase", 108);
    placeCombo (1, "OS", 70);
    placeCombo (2, "Analyzer", 96);
    top.removeFromLeft (6);
    for (int i = 0; i < 3; ++i) toggles[i]->setBounds (top.removeFromLeft (88).reduced (2, 6));

    // --- preset bar (second row); 792 px of controls, 924 available at the minimum width ---
    auto pr = r.removeFromTop (34).reduced (8, 4);
    prevBtn.setBounds  (pr.removeFromLeft (28).reduced (1));
    presetBox.setBounds (pr.removeFromLeft (220).reduced (2, 1));
    nextBtn.setBounds  (pr.removeFromLeft (28).reduced (1));
    pr.removeFromLeft (6);
    saveBtn.setBounds  (pr.removeFromLeft (56).reduced (2, 1));
    presetMenuBtn.setBounds (pr.removeFromLeft (32).reduced (2, 1));
    pr.removeFromLeft (8);
    undoBtn.setBounds  (pr.removeFromLeft (52).reduced (2, 1));
    redoBtn.setBounds  (pr.removeFromLeft (52).reduced (2, 1));
    pr.removeFromLeft (8);
    abBtn.setBounds    (pr.removeFromLeft (40).reduced (2, 1));
    copyBtn.setBounds  (pr.removeFromLeft (56).reduced (2, 1));
    licenseBtn.setBounds (pr.removeFromRight (96).reduced (2, 1));
    themeBox.setBounds (pr.removeFromRight (110).reduced (2, 1));

    auto bottom = r.removeFromBottom (150).reduced (8, 6);
    bandEditor.setBounds (bottom);
    analyzer.setBounds (r.reduced (8, 6));

    if (licenseDialog) licenseDialog->setBounds (getLocalBounds());

    // A resize with a mouse button held is the user dragging the window (its own
    // corner or the host's frame); sizes set by the host or by code are not remembered.
    if (juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        userResized = true;
}
