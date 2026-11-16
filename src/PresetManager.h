#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include <atomic>
#include <map>

namespace wsho
{
// Factory bank + user presets (XML on disk) + A/B snapshots.
//
// Presets and A/B slots set the sound only: Bypass and the analyzer source stay
// as they are. Everything goes through the parameters (never by swapping the
// state tree), so the host is notified and each load is one undo step.
class PresetManager : private juce::AudioProcessorValueTreeState::Listener
{
public:
    // `userPresetDir` is for tests; by default presets live in the user's app-data folder.
    explicit PresetManager (juce::AudioProcessorValueTreeState& s, const juce::File& userPresetDir = {});
    ~PresetManager() override;

    juce::StringArray factoryNames() const;
    juce::StringArray userNames() const;
    bool isFactory (const juce::String& name) const;
    bool userExists (const juce::String& name) const;

    // What a typed name becomes as a preset name: trimmed, a plain file name
    // (no slashes, no "../"). Empty = not usable.
    static juce::String legalName (const juce::String& name);

    void initToDefault();                       // flat / neutral
    bool applyFactory (const juce::String& name);
    bool applyUser (const juce::String& name);

    // User presets. Names are made legal first; empty and factory names are refused.
    bool saveUser (const juce::String& name);                           // overwrites
    bool renameUser (const juce::String& from, const juce::String& to); // overwrites `to`
    bool deleteUser (const juce::String& name);

    juce::File userDir() const;

    // The preset last loaded or saved (kept in the state tree, so it survives
    // closing the window and saving the project), and whether a parameter has
    // changed since.
    juce::String current() const;
    bool isModified() const { return modified.load(); }

    // A/B compare. Lives here, not in the editor: reopening the window keeps both slots.
    bool slotB() const;
    void toggleSlot();              // store the settings in the active slot, switch to the other
    void copyToOtherSlot();

    // Project save / load (called by the processor).
    void writeSessionInfo (juce::ValueTree& stateCopy) const;
    void sessionLoaded();

private:
    using Values = std::map<juce::String, float>;   // parameter ID -> real (unnormalised) value
    struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; };
    struct Slot { juce::ValueTree state; bool modified = false; };

    static const std::vector<Preset>& factory();
    static bool keptOnPresetLoad (const juce::String& paramID);
    static Values valuesOf (const juce::ValueTree& state);
    juce::File presetFile (const juce::String& name) const;

    void newUndoStep();
    void apply (const Values& v, const juce::String& name, bool nowModified);
    void captureTo (bool b);
    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessorValueTreeState& apvts;
    juce::File presetDir;
    std::atomic<bool> modified { false }, applying { false };
    Slot slots[2];
};
} // namespace wsho
