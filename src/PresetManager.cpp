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

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

juce::File PresetManager::userDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Whysho").getChildFile ("EQ1").getChildFile ("Presets");
    d.createDirectory();
    return d;
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

void PresetManager::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void PresetManager::applyValues (const V& v) { for (auto& kv : v) setParam (kv.first, kv.second); }

void PresetManager::initToDefault()
{
    for (auto* p : apvts.processor.getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::applyFactory (const juce::String& name)
{
    for (auto& p : factory())
        if (p.name == name)
        {
            initToDefault();
            applyValues (p.values);
            currentName = name;
            return;
        }
}

void PresetManager::applyUser (const juce::String& name)
{
    auto f = userDir().getChildFile (name + ".wshoeq");
    if (! f.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentName = name;
        }
}

bool PresetManager::saveUser (const juce::String& name)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        currentName = name;
        return xml->writeTo (userDir().getChildFile (name + ".wshoeq"));
    }
    return false;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    return userDir().getChildFile (name + ".wshoeq").deleteFile();
}

void PresetManager::captureTo (bool slotB) { (slotB ? slotBState : slotAState) = apvts.copyState(); }

void PresetManager::recallFrom (bool slotB)
{
    auto& s = slotB ? slotBState : slotAState;
    if (s.isValid()) apvts.replaceState (s.createCopy());
}

} // namespace wsho
