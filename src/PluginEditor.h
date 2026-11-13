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
                                     private juce::Timer
{
public:
    explicit WhyshoEQAudioProcessorEditor (WhyshoEQAudioProcessor&);
    ~WhyshoEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
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
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" },
                     abBtn { "A" }, copyBtn { "Copy" };
    bool slotB = false;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    void applyThemeChange (int index);
    void rebuildPresetMenu();
    void selectPresetDelta (int d);
    void applyPresetSelection();
    void doSavePreset();
    void toggleAB();

    juce::OwnedArray<SA> sa; juce::OwnedArray<CA> ca; juce::OwnedArray<BA> ba;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoEQAudioProcessorEditor)
};
} // namespace wsho
