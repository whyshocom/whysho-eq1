#include "PresetManager.h"

namespace wsho
{
using V = std::vector<std::pair<juce::String, float>>;

// Helper: build a band's parameter entries.
static void band (V& v, int i, int type, float freq, float gain = 0.0f, float q = 0.707f,
                  int slope = 1, bool dyn = false, float dThr = -24.0f, float dRng = -6.0f)
{
    v.push_back ({ pid::band (i, pid::on),    1.0f });
    v.push_back ({ pid::band (i, pid::type),  (float) type });
    v.push_back ({ pid::band (i, pid::freq),  freq });
    v.push_back ({ pid::band (i, pid::gain),  gain });
    v.push_back ({ pid::band (i, pid::q),     q });
    v.push_back ({ pid::band (i, pid::slope), (float) slope });
    if (dyn)
    {
        v.push_back ({ pid::band (i, pid::dyn),       1.0f });
        v.push_back ({ pid::band (i, pid::dynThresh), dThr });
        v.push_back ({ pid::band (i, pid::dynRange),  dRng });
    }
}

const std::vector<PresetManager::Preset>& PresetManager::factory()
{
    static const std::vector<Preset> list = [] {
        using T = int; // filter type indices
        const T Bell = 0, LowShelf = 1, HighShelf = 2, HighPass = 3, LowPass = 4;
        std::vector<Preset> p;

        p.push_back ({ "Init (Flat)", {} });

        { V v; band (v, 0, HighPass, 85.0f, 0, 0.707f, 1);
                band (v, 3, Bell, 300.0f, -2.0f, 1.0f);
                band (v, 8, Bell, 3000.0f, 3.0f, 0.9f);
                band (v, 14, HighShelf, 12000.0f, 3.0f);
          p.push_back ({ "Vocal Presence", v }); }

        { V v; band (v, 0, HighPass, 30.0f, 0, 0.707f, 2);
                band (v, 1, Bell, 60.0f, 2.0f, 0.8f);
                band (v, 4, Bell, 220.0f, -3.0f, 1.2f);
          p.push_back ({ "Bass Tighten", v }); }

        { V v; band (v, 10, Bell, 6500.0f, 0.0f, 3.0f, 1, /*dyn*/true, -28.0f, -8.0f);
          p.push_back ({ "De-Ess (Dynamic)", v }); }

        { V v; band (v, 0, LowShelf, 120.0f, 1.0f);
                band (v, 14, HighShelf, 8000.0f, 1.0f);
          p.push_back ({ "Mix Glue", v }); }

        { V v; band (v, 14, HighShelf, 11000.0f, 4.0f, 0.7f);
          p.push_back ({ "Air Lift", v }); }

        { V v; band (v, 0, HighPass, 400.0f, 0, 0.707f, 3);
                band (v, 15, LowPass, 3000.0f, 0, 0.707f, 3);
          p.push_back ({ "Telephone", v }); }

        { V v; band (v, 1, LowShelf, 160.0f, 2.0f);
                band (v, 14, HighShelf, 9000.0f, -1.5f);
          p.push_back ({ "Warmth", v }); }

        return p;
    }();
    return list;
}

static const juce::Identifier kPresetName ("presetName"), kPresetModified ("presetModified"), kSlotB ("abSlot");

// Not part of a preset or an A/B slot.
bool PresetManager::keptOnPresetLoad (const juce::String& id) { return id == pid::bypass || id == pid::analyzer; }

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, const juce::File& userPresetDir)
    : apvts (s), presetDir (userPresetDir)
{
    if (presetDir == juce::File())
        presetDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Whysho").getChildFile ("EQ1").getChildFile ("Presets");

    apvts.state.setProperty (kPresetName, factory().front().name, nullptr);

    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! keptOnPresetLoad (rp->paramID))
                apvts.addParameterListener (rp->paramID, this);
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->paramID, this);
}

// Any thread (the host may automate from the audio thread).
void PresetManager::parameterChanged (const juce::String&, float)
{
    if (! applying.load()) modified = true;
}

juce::File PresetManager::userDir() const
{
    presetDir.createDirectory();
    return presetDir;
}

juce::String PresetManager::legalName (const juce::String& name)
{
    // No leading dots either: no hidden files, no "..".
    return juce::File::createLegalFileName (name.trim()).trimCharactersAtStart (". ").trim();
}

// The file for a preset name: always directly inside the preset folder.
juce::File PresetManager::presetFile (const juce::String& name) const
{
    const auto legal = legalName (name);
    return legal.isEmpty() ? juce::File() : userDir().getChildFile (legal + ".wshoeq");
}

juce::StringArray PresetManager::factoryNames() const
{
    juce::StringArray n;
    for (auto& p : factory()) n.add (p.name);
    return n;
}

juce::StringArray PresetManager::userNames() const
{
    juce::StringArray n;
    for (auto& f : userDir().findChildFiles (juce::File::findFiles, false, "*.wshoeq"))
        n.add (f.getFileNameWithoutExtension());
    n.sort (true);
    return n;
}

bool PresetManager::isFactory (const juce::String& name) const
{
    return factoryNames().contains (legalName (name), true);
}

bool PresetManager::userExists (const juce::String& name) const
{
    return presetFile (name).existsAsFile();
}

juce::String PresetManager::current() const { return apvts.state[kPresetName].toString(); }

// Parameter changes reach the state tree (and with it the undo history) on a
// timer; copyState() pushes the pending ones through now, so they land in the
// undo step they belong to.
void PresetManager::newUndoStep()
{
    apvts.copyState();
    if (apvts.undoManager != nullptr) apvts.undoManager->beginNewTransaction();
}

PresetManager::Values PresetManager::valuesOf (const juce::ValueTree& state)
{
    Values v;
    for (const auto& child : state)
        if (child.hasProperty ("id") && child.hasProperty ("value"))
            v[child["id"].toString()] = (float) child["value"];
    return v;
}

// Sets every preset parameter: to its value in `v`, or to its default when `v`
// does not mention it. The caller makes it one undo step.
void PresetManager::apply (const Values& v, const juce::String& name, bool nowModified)
{
    applying = true;
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (keptOnPresetLoad (rp->paramID)) continue;
            const auto it = v.find (rp->paramID);
            const float target = it != v.end() && std::isfinite (it->second) ? rp->convertTo0to1 (it->second)
                                                                             : rp->getDefaultValue();
            if (! juce::approximatelyEqual (rp->getValue(), target))
                rp->setValueNotifyingHost (target);
        }
    apvts.state.setProperty (kPresetName, name, apvts.undoManager);
    modified = nowModified;
    applying = false;
}

void PresetManager::initToDefault()
{
    newUndoStep();
    apply ({}, factory().front().name, false);
    newUndoStep();
}

bool PresetManager::applyFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (p.name == name)
        {
            newUndoStep();
            apply (Values (p.values.begin(), p.values.end()), name, false);
            newUndoStep();
            return true;
        }
    return false;
}

bool PresetManager::applyUser (const juce::String& name)
{
    const auto f = presetFile (name);
    if (! f.existsAsFile()) return false;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            newUndoStep();
            apply (valuesOf (juce::ValueTree::fromXml (*xml)), f.getFileNameWithoutExtension(), false);
            newUndoStep();
            return true;
        }
    return false;
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto f = presetFile (name);
    if (f == juce::File() || isFactory (name)) return false;

    auto state = apvts.copyState();
    state.removeAllProperties (nullptr);          // the preset name etc. belong to the project
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (f)) return false;

    apvts.state.setProperty (kPresetName, f.getFileNameWithoutExtension(), nullptr);
    modified = false;
    return true;
}

bool PresetManager::renameUser (const juce::String& from, const juce::String& to)
{
    const auto src = presetFile (from), dst = presetFile (to);
    if (! src.existsAsFile() || dst == juce::File() || isFactory (to)) return false;
    if (src == dst) return true;
    const bool wasCurrent = current() == src.getFileNameWithoutExtension();
    if (! src.moveFileTo (dst)) return false;     // replaces an existing `to`
    if (wasCurrent) apvts.state.setProperty (kPresetName, dst.getFileNameWithoutExtension(), nullptr);
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    const auto f = presetFile (name);
    if (! f.existsAsFile() || ! f.deleteFile()) return false;
    if (current() == f.getFileNameWithoutExtension())
        apvts.state.setProperty (kPresetName, juce::String(), nullptr);   // the settings stay, the name goes
    return true;
}

// ---- A/B -------------------------------------------------------------------
bool PresetManager::slotB() const { return (bool) apvts.state[kSlotB]; }

void PresetManager::captureTo (bool b) { slots[b ? 1 : 0] = { apvts.copyState(), modified.load() }; }

void PresetManager::copyToOtherSlot() { captureTo (! slotB()); }

void PresetManager::toggleSlot()
{
    const bool to = ! slotB();
    captureTo (! to);
    if (! slots[to ? 1 : 0].state.isValid()) captureTo (to);   // first visit: starts as a copy
    const auto& s = slots[to ? 1 : 0];

    // The slot flag is part of the same undo step as the values: undoing an A/B
    // switch goes back to the other slot, and nothing stored is lost.
    newUndoStep();
    apply (valuesOf (s.state), s.state[kPresetName].toString(), s.modified);
    apvts.state.setProperty (kSlotB, to, apvts.undoManager);
    newUndoStep();
}

// ---- project save / load ---------------------------------------------------
void PresetManager::writeSessionInfo (juce::ValueTree& stateCopy) const
{
    stateCopy.setProperty (kPresetModified, modified.load(), nullptr);
    stateCopy.removeProperty (kSlotB, nullptr);   // A/B slots are not saved: a project opens on A
}

void PresetManager::sessionLoaded()
{
    // A project saved before the name was stored has none: show none, never a wrong one.
    modified = (bool) apvts.state[kPresetModified];
    apvts.state.removeProperty (kPresetModified, nullptr);
    apvts.state.removeProperty (kSlotB, nullptr);
    slots[0] = {};
    slots[1] = {};
}

} // namespace wsho
