#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WhyshoLookAndFeel.h"
#include "gui/AnalyzerComponent.h"
#include "gui/BandEditor.h"
#include "gui/LicenseDialog.h"

namespace wsho
{
class WhyshoEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::ChangeListener,
                                     private juce::Timer
{
public:
    explicit WhyshoEQAudioProcessorEditor (WhyshoEQAudioProcessor&);
    ~WhyshoEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateLicenseUi();
    void openLicense();

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider&  addKnob (const juce::String& label);
    juce::ComboBox& addCombo (const juce::StringArray& items);
    juce::ToggleButton& addToggle (const juce::String& t);

    WhyshoEQAudioProcessor& proc;
    gui::WhyshoLookAndFeel lnf;

    juce::TooltipWindow    tooltipWindow { this, 600 };
    gui::AnalyzerComponent analyzer;
    gui::BandEditor        bandEditor;
    std::unique_ptr<gui::LicenseDialog> licenseDialog;

    juce::OwnedArray<juce::Slider> knobs;        // in,out,mix,scale
    juce::OwnedArray<juce::Label>  knobLabels;
    juce::OwnedArray<juce::ComboBox> combos;     // phase, os, analyzer
    juce::OwnedArray<juce::ToggleButton> toggles;// bypass, autogain, sclisten
    juce::TextButton licenseBtn { "License" };
    juce::ComboBox themeBox;
    juce::Label brand;

    // preset bar
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, presetMenuBtn { "..." },
                     undoBtn { "Undo" }, redoBtn { "Redo" },
                     abBtn { "A" }, copyBtn { "Copy" };
    juce::StringArray factoryPresets, userPresets;   // as listed in presetBox
    juce::String shownPreset;

    // The one dialog (name prompt / confirmation / message) that can be open.
    using DialogAction = std::function<void (WhyshoEQAudioProcessorEditor&, const juce::String& text)>;
    std::unique_ptr<juce::AlertWindow> dialog;
    void showDialog (const juce::String& title, const juce::String& message, const juce::String& okText,
                     DialogAction onOk, bool askForName = false, const juce::String& initialName = {});
    void showMessage (const juce::String& title, const juce::String& message);

    void applyTheme();
    static constexpr int kHeaderH = 78;
    void rebuildPresetMenu();
    void refreshPresetDisplay();
    int  presetId (const juce::String& name) const;
    bool userPresetIsCurrent() const;
    void selectPresetDelta (int d);
    void applyPresetSelection();
    void showPresetMenu();
    void askSavePreset();
    void savePreset (const juce::String& typedName);
    void askRenamePreset();
    void renamePreset (const juce::String& from, const juce::String& typedName);
    void askDeletePreset();
    void toggleAB();
    void undoOrRedo (bool redo);

    bool mouseWasDown = false;
    juce::Point<int> rememberedSize;     // the window size last read from / written to the preference
    bool userResized = false;
    void rememberWindowSize();

    juce::OwnedArray<SA> sa; juce::OwnedArray<CA> ca; juce::OwnedArray<BA> ba;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoEQAudioProcessorEditor)
};
} // namespace wsho
