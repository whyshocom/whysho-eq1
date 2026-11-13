#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Bottom control strip bound to the currently-selected band. Attachments are
// rebuilt whenever the selection changes.
class BandEditor : public juce::Component
{
public:
    explicit BandEditor (WhyshoEQAudioProcessor& p);
    void setBand (int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    struct Knob { juce::Slider slider; juce::Label label; };
    Knob& addKnob (const juce::String& name);
    juce::ComboBox& addCombo (const juce::StringArray& items);
    juce::ToggleButton& addToggle (const juce::String& name);

    WhyshoEQAudioProcessor& proc;
    int band = 0;

    juce::OwnedArray<Knob> knobs;          // freq, gain, q, dThr, dRng, dAtt, dRel
    juce::OwnedArray<juce::ComboBox> combos; // type, slope, chan
    juce::OwnedArray<juce::ToggleButton> toggles; // solo, dyn, dynSC
    juce::Label title;

    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
};
} // namespace wsho::gui
