#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace wsho
{
// Factory bank + user presets (XML on disk) + A/B snapshots.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s);

    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;

    void initToDefault();                       // flat / neutral
    void applyFactory (const juce::String& name);
    void applyUser (const juce::String& name);

    bool saveUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    juce::File userDir() const;
    juce::String current() const { return currentName; }
    void setCurrent (const juce::String& n) { currentName = n; }

    // A/B compare
    void captureTo (bool slotB);
    void recallFrom (bool slotB);
    bool hasSlot (bool slotB) const { return (slotB ? slotBState : slotAState).isValid(); }

private:
    struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };
    static const std::vector<Preset>& factory();
    void applyValues (const std::vector<std::pair<juce::String, float>>& v);
    void setParam (const juce::String& id, float value);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Init (Flat)" };
    juce::ValueTree slotAState, slotBState;
};
} // namespace wsho
