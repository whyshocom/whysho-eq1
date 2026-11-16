// Undo / redo, presets and A/B, on the real parameter set and PresetManager,
// hosted by a minimal processor (the real one needs the whole GUI; it gets the
// same checks from `WhyshoEQ1_Snapshot --selftest`).
#include "Parameters.h"
#include "PresetManager.h"
#include <iostream>

using namespace wsho;

extern int failures;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (false)

namespace
{
struct Host : juce::AudioProcessor
{
    explicit Host (const juce::File& presetDir) : presets (apvts, presetDir) {}

    const juce::String getName() const override { return "test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Same as WhyshoEQAudioProcessor.
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        auto tree = apvts.copyState();
        presets.writeSessionInfo (tree);
        if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
            {
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
                presets.sessionLoaded();
            }
    }

    void set (const juce::String& id, float v)
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    float get (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); }
    bool near (const juce::String& id, float v) { return std::abs (get (id) - v) < 0.01f; }

    // What the editor's timer does once a gesture is over (parameter changes
    // reach the state tree, and so the undo history, only when flushed).
    void flush()      { apvts.copyState(); }
    void endGesture() { flush(); undo.beginNewTransaction(); }

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts { *this, &undo, "WHYSHO_EQ1", createParameterLayout() };
    PresetManager presets;
};

const juce::String gain1 = pid::band (1, pid::gain), freq2 = pid::band (2, pid::freq), q2 = pid::band (2, pid::q);

void testUndo (const juce::File& dir)
{
    Host h (dir);
    h.flush();
    CHECK (! h.undo.canUndo());                 // a fresh instance has nothing to undo

    const float freqBefore = h.get (freq2), qBefore = h.get (q2);
    h.set (gain1, 3.0f);
    h.endGesture();
    h.set (freq2, 523.3f);                      // one gesture moving two parameters (a node drag)
    h.set (q2, 2.5f);
    h.endGesture();

    CHECK (h.undo.undo());                      // the second step goes, as a whole ...
    CHECK (h.near (freq2, freqBefore) && h.near (q2, qBefore));
    CHECK (h.near (gain1, 3.0f));               // ... the first stays
    h.endGesture();                             // the editor timer ticking must not eat the redo
    CHECK (h.undo.canRedo());
    CHECK (h.undo.redo());
    CHECK (h.near (freq2, 523.3f) && h.near (q2, 2.5f) && h.near (gain1, 3.0f));
    h.endGesture();
    CHECK (h.undo.undo() && h.undo.undo());
    CHECK (h.near (gain1, 0.0f) && ! h.undo.canUndo());

    // Undo tells the host, like any other parameter change.
    struct Counter : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        int changes = 0;
    } hostSide;
    h.addListener (&hostSide);
    CHECK (h.undo.redo() && hostSide.changes == 1);
    h.removeListener (&hostSide);

    // Loading a project clears the history.
    juce::MemoryBlock project;
    h.getStateInformation (project);
    h.set (gain1, -7.0f);
    h.endGesture();
    CHECK (h.undo.canUndo());
    h.setStateInformation (project.getData(), (int) project.getSize());
    h.endGesture();
    CHECK (h.near (gain1, 3.0f) && ! h.undo.canUndo() && ! h.undo.canRedo());
}

void testPresets (const juce::File& dir)
{
    Host h (dir);
    auto& pm = h.presets;
    CHECK (pm.current() == "Init (Flat)" && ! pm.isModified());

    // A preset leaves Bypass and the analyzer source alone, and is one undo step.
    h.set (pid::bypass, 1.0f);
    h.set (pid::analyzer, 0.0f);
    h.set (gain1, 3.0f);
    h.endGesture();
    CHECK (pm.isModified());
    CHECK (pm.applyFactory ("Vocal Presence"));
    CHECK (pm.current() == "Vocal Presence" && ! pm.isModified());
    CHECK (h.near (pid::band (0, pid::on), 1.0f) && h.near (pid::band (8, pid::gain), 3.0f) && h.near (gain1, 0.0f));
    CHECK (h.near (pid::bypass, 1.0f) && h.near (pid::analyzer, 0.0f));
    CHECK (h.undo.undo());
    CHECK (h.near (pid::band (0, pid::on), 0.0f) && h.near (pid::band (8, pid::gain), 0.0f) && h.near (gain1, 3.0f));
    CHECK (pm.current() == "Init (Flat)");
    CHECK (h.undo.redo() && pm.current() == "Vocal Presence" && h.near (pid::band (8, pid::gain), 3.0f));
    pm.initToDefault();
    CHECK (h.near (pid::band (0, pid::on), 0.0f) && h.near (pid::bypass, 1.0f) && h.near (pid::analyzer, 0.0f));

    // Names: always a plain file inside the preset folder; empty and factory names refused.
    CHECK (PresetManager::legalName ("  My Preset ") == "My Preset");
    CHECK (PresetManager::legalName ("../../x") == "x" && PresetManager::legalName (" .. ").isEmpty());
    CHECK (! pm.saveUser ("   ") && ! pm.saveUser ("/") && ! pm.saveUser (".."));
    CHECK (! pm.saveUser ("Warmth") && ! pm.saveUser (" warmth "));
    CHECK (pm.userNames().isEmpty());
    CHECK (pm.saveUser ("../../escape"));
    CHECK (pm.userNames() == juce::StringArray ("escape") && pm.current() == "escape");
    CHECK (dir.getChildFile ("escape.wshoeq").existsAsFile());
    CHECK (dir.getParentDirectory().getParentDirectory().findChildFiles (juce::File::findFiles, true, "*.wshoeq").size() == 1);
    CHECK (! pm.applyUser ("../outside") && ! pm.deleteUser ("../outside") && ! pm.renameUser ("../outside", "x"));
    CHECK (pm.deleteUser ("../../escape") && pm.userNames().isEmpty() && pm.current().isEmpty());

    // Save, change, load: the sound comes back, Bypass / analyzer stay as they are now.
    h.set (gain1, 4.5f);
    h.set (pid::bypass, 0.0f);
    h.set (pid::analyzer, 2.0f);
    CHECK (pm.saveUser ("Mine") && pm.current() == "Mine" && ! pm.isModified());
    CHECK (pm.userExists ("Mine") && pm.userExists (" Mine ") && ! pm.userExists ("Other") && ! pm.isFactory ("Mine"));
    h.set (gain1, -9.0f);
    h.set (pid::bypass, 1.0f);
    h.set (pid::analyzer, 4.0f);
    CHECK (pm.isModified());
    h.endGesture();
    CHECK (pm.applyUser ("Mine"));
    CHECK (h.near (gain1, 4.5f) && h.near (pid::bypass, 1.0f) && h.near (pid::analyzer, 4.0f) && ! pm.isModified());
    CHECK (h.undo.undo() && h.near (gain1, -9.0f));          // loading a user preset is undoable too

    // Rename / delete.
    CHECK (pm.applyUser ("Mine"));
    CHECK (! pm.renameUser ("Mine", "Air Lift") && ! pm.renameUser ("Mine", " ") && ! pm.renameUser ("Nope", "X"));
    CHECK (pm.renameUser ("Mine", "Yours") && pm.userNames() == juce::StringArray ("Yours") && pm.current() == "Yours");
    CHECK (pm.saveUser ("Second") && pm.renameUser ("Second", "Yours"));   // replacing is the caller's decision
    CHECK (pm.userNames() == juce::StringArray ("Yours") && pm.current() == "Yours");
    CHECK (pm.deleteUser ("Yours") && pm.userNames().isEmpty() && pm.current().isEmpty() && ! pm.deleteUser ("Yours"));

    // The preset name and its "modified" mark are saved with the project.
    CHECK (pm.applyFactory ("Warmth"));
    juce::MemoryBlock clean, changed;
    h.getStateInformation (clean);
    h.set (gain1, 1.5f);
    h.getStateInformation (changed);
    Host other (dir);
    other.setStateInformation (changed.getData(), (int) changed.getSize());
    CHECK (other.presets.current() == "Warmth" && other.presets.isModified() && other.near (gain1, 1.5f));
    other.setStateInformation (clean.getData(), (int) clean.getSize());
    CHECK (other.presets.current() == "Warmth" && ! other.presets.isModified());
}

void testAB (const juce::File& dir)
{
    Host h (dir);
    auto& pm = h.presets;
    h.set (gain1, 3.0f);
    h.endGesture();
    CHECK (! pm.slotB());

    pm.toggleSlot();                            // B starts as a copy of A
    CHECK (pm.slotB() && h.near (gain1, 3.0f));
    h.set (gain1, -5.0f);
    h.set (pid::bypass, 1.0f);
    h.endGesture();
    pm.toggleSlot();
    CHECK (! pm.slotB() && h.near (gain1, 3.0f) && h.near (pid::bypass, 1.0f));   // Bypass is not part of a slot
    pm.toggleSlot();
    CHECK (pm.slotB() && h.near (gain1, -5.0f));

    // Undoing a switch goes back to the other slot, flag included, and loses nothing.
    CHECK (h.undo.undo());
    CHECK (! pm.slotB() && h.near (gain1, 3.0f));
    pm.toggleSlot();
    CHECK (pm.slotB() && h.near (gain1, -5.0f));

    pm.copyToOtherSlot();                       // B -> A
    pm.toggleSlot();
    CHECK (! pm.slotB() && h.near (gain1, -5.0f));

    // A project always opens on A (slots are not saved).
    pm.toggleSlot();
    juce::MemoryBlock project;
    h.getStateInformation (project);
    Host other (dir);
    other.setStateInformation (project.getData(), (int) project.getSize());
    CHECK (! other.presets.slotB());
}
} // namespace

void runWorkflowTests()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("whysho-eq1-workflow-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    const auto dir = root.getChildFile ("a").getChildFile ("Presets");
    testUndo (dir);
    testPresets (dir);
    testAB (dir);
    root.deleteRecursively();
}
