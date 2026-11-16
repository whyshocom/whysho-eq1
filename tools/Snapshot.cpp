// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
#include "PluginProcessor.h"
#include "PluginEditor.h"

// What the band strip shows (it starts on band 1): the default set-up has a high-pass
// there (no gain, dynamics off), so some controls are disabled.
enum class Strip { standard, bandOff, allEnabled };

static void renderShot (int w, int h, const juce::File& out, bool openLicense, int themeIndex = 0, bool clickSignIn = false,
                        Strip strip = Strip::standard)
{
    wsho::WhyshoEQAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    wsho::gui::loadThemePreference();
    wsho::gui::setThemeIndex (themeIndex);

    // enable a few bands + a dynamic band so the curve/nodes are interesting
    auto set = [&] (const juce::String& id, float v)
    { if (auto* p = proc.state().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    using namespace wsho;
    for (int i = 0; i < 6; ++i) { if (auto* p = proc.state().getParameter (pid::band (i, pid::on))) p->setValueNotifyingHost (1.0f); }
    set (pid::band (0, pid::freq), 45.0f);   set (pid::band (0, pid::gain), 0.0f);
    set (pid::band (1, pid::freq), 110.0f);  set (pid::band (1, pid::gain), 3.5f);
    set (pid::band (2, pid::freq), 450.0f);  set (pid::band (2, pid::gain), -4.0f);
    set (pid::band (3, pid::freq), 1600.0f); set (pid::band (3, pid::gain), 2.5f);
    if (auto* p = proc.state().getParameter (pid::band (3, pid::dyn))) p->setValueNotifyingHost (1.0f);
    set (pid::band (4, pid::freq), 5200.0f); set (pid::band (4, pid::gain), 4.0f);
    set (pid::band (5, pid::freq), 12000.0f);set (pid::band (5, pid::gain), -2.0f);
    if (strip == Strip::bandOff)    set (pid::band (0, pid::on), 0.0f);
    if (strip == Strip::allEnabled) { set (pid::band (0, pid::type), 0.0f); set (pid::band (0, pid::dyn), 1.0f); }

    // No addToDesktop(): software-render the component tree offscreen (headless).
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (w > 0) ed->setBounds (0, 0, w, h);     // w == 0: the editor's own default size
    ed->setVisible (true);

    if (openLicense)
        if (auto* lb = dynamic_cast<juce::Button*> (ed->findChildWithID ("license")))
            lb->triggerClick();

    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);

    if (clickSignIn)   // really starts a browser sign-in against whysho.com, then cancels it
    {
        proc.license().signIn();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";

    if (clickSignIn)
    {
        proc.license().cancelSignIn();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    }
}

// The preset dialogs are separate windows: render the "Save Preset" one on its own (--dialogs).
static void renderDialogShot (const juce::File& out, int themeIndex)
{
    wsho::WhyshoEQAudioProcessor proc;
    wsho::gui::loadThemePreference();
    wsho::gui::setThemeIndex (themeIndex);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (auto* b = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetSave"))) b->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* aw = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
        {
            auto img = aw->createComponentSnapshot (aw->getLocalBounds(), false, 2.0f);
            out.deleteFile();
            juce::FileOutputStream os (out);
            if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
            std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
        }
    ed.reset();   // with the dialog still open, as a host may do
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
}

static juce::AlertWindow* openDialog()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* aw = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
            if (aw->isCurrentlyModal()) return aw;
    return nullptr;
}

// --selftest --dialogs: the Save dialog chain, by pressing the real buttons. Saves and
// deletes one user preset with an unlikely name. (Needs a window manager, like --dialogs.)
static int saveDialogTest()
{
    using namespace wsho;
    int failures = 0;
    #define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (false)
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    WhyshoEQAudioProcessor proc;
    auto& pm = proc.presetManager();
    const juce::String name ("Snapshot selftest 7f3a");
    pm.deleteUser (name);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* save = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetSave"));
    CHECK (save != nullptr);
    if (save == nullptr) return 1;

    // Answers the open dialog; returns the title of the dialog that follows it ("" = none).
    auto answer = [&] (const juce::String& typed, int result)
    {
        if (auto* aw = openDialog())
        {
            if (auto* te = aw->getTextEditor ("name")) te->setText (typed);
            aw->exitModalState (result);
        }
        pump (200);
        auto* next = openDialog();
        return next != nullptr ? next->getName() : juce::String();
    };
    auto typeName = [&] (const juce::String& typed) { save->triggerClick(); pump (200); return answer (typed, 1); };

    CHECK (typeName ("   ") == "Save Preset" && openDialog()->getTextEditor ("name") == nullptr);   // refused, with a message
    CHECK (answer ({}, 0).isEmpty());
    CHECK (typeName (" warmth ") == "Save Preset" && openDialog()->getTextEditor ("name") == nullptr); // factory name refused
    CHECK (answer ({}, 0).isEmpty() && pm.userNames().indexOf ("warmth", true) < 0);
    CHECK (typeName ("../" + name + "  ").isEmpty());                                                // new name: saved at once
    CHECK (pm.userExists (name) && pm.current() == name && ! pm.isModified());
    CHECK (pm.userDir().getChildFile (name + ".wshoeq").existsAsFile());

    const auto gain = pid::band (1, pid::gain);
    proc.state().getParameter (gain)->setValueNotifyingHost (0.75f);
    pump (200);
    CHECK (pm.isModified());
    CHECK (typeName (name) == "Replace Preset?");                                                    // existing name: asks first
    CHECK (answer ({}, 0).isEmpty() && pm.isModified());                                             // Cancel keeps the old file
    CHECK (typeName (name) == "Replace Preset?");
    CHECK (answer ({}, 1).isEmpty() && ! pm.isModified());                                           // Replace saves

    save->triggerClick(); pump (200);
    CHECK (openDialog() != nullptr);
    ed.reset();                       // the host closes the editor with a dialog open
    pump (200);
    CHECK (openDialog() == nullptr);
    CHECK (pm.deleteUser (name));

    #undef CHECK
    std::cout << (failures == 0 ? "save dialog test passed\n" : "save dialog test FAILED\n");
    return failures == 0 ? 0 : 1;
}

// --selftest: undo / redo, presets, A/B and the window size, through the real
// processor and editor (tests/WorkflowTests.cpp covers the rules with a stand-in host).
static int selfTest()
{
    using namespace wsho;
    int failures = 0;
    #define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (false)
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    WhyshoEQAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    auto& um = proc.undoManager();
    auto& pm = proc.presetManager();
    auto set = [&] (const juce::String& id, float v)
    { auto* p = proc.state().getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto get = [&] (const juce::String& id) { return proc.state().getRawParameterValue (id)->load(); };
    auto near = [&] (const juce::String& id, float v) { return std::abs (get (id) - v) < 0.01f; };
    const auto gain1 = pid::band (1, pid::gain), freq2 = pid::band (2, pid::freq);

    {   // The editor's timer turns each change into its own undo step; the buttons follow.
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        auto* undo = dynamic_cast<juce::Button*> (ed->findChildWithID ("undo"));
        auto* redo = dynamic_cast<juce::Button*> (ed->findChildWithID ("redo"));
        auto* ab   = dynamic_cast<juce::Button*> (ed->findChildWithID ("ab"));
        CHECK (undo != nullptr && redo != nullptr && ab != nullptr);
        if (undo == nullptr || redo == nullptr || ab == nullptr) return 1;
        pump (300);
        CHECK (! undo->isEnabled() && ! redo->isEnabled());

        const float freqBefore = get (freq2);
        set (gain1, 3.0f);   pump (700);
        CHECK (undo->isEnabled() && ! redo->isEnabled());
        set (freq2, 523.3f); pump (700);
        undo->triggerClick(); pump (300);
        CHECK (near (freq2, freqBefore) && near (gain1, 3.0f));        // second change reverted, first kept
        CHECK (undo->isEnabled() && redo->isEnabled());
        redo->triggerClick(); pump (700);
        CHECK (near (freq2, 523.3f) && near (gain1, 3.0f) && ! redo->isEnabled());
        CHECK (ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
        CHECK (near (freq2, freqBefore));
        CHECK (ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
        CHECK (near (freq2, 523.3f));
        CHECK (ed->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::commandModifier, 0)));
        CHECK (ed->keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)));
        CHECK (near (freq2, 523.3f));
        CHECK (! ed->keyPressed (juce::KeyPress ('z')) && ! ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));

        // A preset is one step, and leaves Bypass and the analyzer source alone.
        set (pid::bypass, 1.0f); set (pid::analyzer, 0.0f); pump (700);
        pm.applyFactory ("Vocal Presence"); pump (300);
        CHECK (near (pid::band (8, pid::gain), 3.0f) && near (gain1, 0.0f) && near (pid::bypass, 1.0f) && near (pid::analyzer, 0.0f));
        CHECK (pm.current() == "Vocal Presence" && ! pm.isModified());
        undo->triggerClick(); pump (300);
        CHECK (near (pid::band (8, pid::gain), 0.0f) && near (gain1, 3.0f) && near (freq2, 523.3f) && near (pid::bypass, 1.0f));
        redo->triggerClick(); pump (300);
        CHECK (near (pid::band (8, pid::gain), 3.0f) && pm.current() == "Vocal Presence");

        // A/B lives in the processor: closing and reopening the window keeps both slots.
        set (gain1, 2.0f); pump (700);
        ab->triggerClick(); pump (300);
        CHECK (pm.slotB() && ab->getButtonText() == "B" && near (gain1, 2.0f));
        set (gain1, -6.0f); pump (700);
        ed.reset();
        ed.reset (proc.createEditor());
        pump (300);
        ab = dynamic_cast<juce::Button*> (ed->findChildWithID ("ab"));
        CHECK (ab != nullptr && ab->getButtonText() == "B" && pm.slotB() && near (gain1, -6.0f));
        if (ab != nullptr) { ab->triggerClick(); pump (300); }
        CHECK (! pm.slotB() && near (gain1, 2.0f));
        CHECK (pm.current() == "Vocal Presence" && pm.isModified());
    }

    {   // The project remembers the preset name; loading one clears the undo history.
        juce::MemoryBlock project;
        proc.getStateInformation (project);
        CHECK (um.canUndo());
        WhyshoEQAudioProcessor other;
        set (gain1, 9.0f);
        other.setStateInformation (project.getData(), (int) project.getSize());
        CHECK (other.presetManager().current() == "Vocal Presence" && other.presetManager().isModified());
        proc.setStateInformation (project.getData(), (int) project.getSize());
        proc.state().copyState();
        CHECK (near (gain1, 2.0f) && ! um.canUndo() && ! um.canRedo() && pm.current() == "Vocal Presence");
    }

    {   // Window size: a per-computer preference, clamped to the limits and the screen.
        const auto file = gui::themePreferenceFile().getSiblingFile ("window.txt");
        const bool had = file.existsAsFile();
        const auto before = file.loadFileAsString();
        juce::Rectangle<int> screen (0, 0, 100000, 100000);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) screen = d->userArea;
        std::cout << "screen " << screen.getWidth() << "x" << screen.getHeight() << "\n";
        auto opensAt = [&] (const juce::String& saved)
        {
            file.getParentDirectory().createDirectory();
            if (saved.isEmpty()) file.deleteFile(); else file.replaceWithText (saved);
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            return juce::Point<int> (ed->getWidth(), ed->getHeight());
        };
        CHECK (opensAt ("") == juce::Point<int> (1020, 640));
        CHECK (opensAt ("1100 700") == juce::Point<int> (1100, 700));
        CHECK (opensAt ("100 100") == juce::Point<int> (940, 580));
        CHECK (opensAt ("rubbish") == juce::Point<int> (1020, 640));
        CHECK (opensAt ("-5 99999999999") == juce::Point<int> (1020, 640));
        const auto big = opensAt ("5000 4000");   // saved on a bigger display
        std::cout << "5000x4000 opens at " << big.x << "x" << big.y << "\n";
        CHECK (big.x <= juce::jmin (1800, juce::jmax (940, screen.getWidth())) && big.y <= juce::jmin (1200, juce::jmax (580, screen.getHeight())));
        CHECK (big.x >= 940 && big.y >= 580);
        if (had) file.replaceWithText (before); else file.deleteFile();
    }

    #undef CHECK
    std::cout << (failures == 0 ? "selftest passed\n" : "selftest FAILED\n");
    return failures == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (juce::StringArray (argv + 1, argc - 1).contains ("--selftest"))
        return selfTest() + (juce::StringArray (argv + 1, argc - 1).contains ("--dialogs") ? saveDialogTest() : 0);
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    const char* names[] = { "paper", "midnight", "graphite" };
    for (int t = 0; t < 3; ++t)
    {
        renderShot (0, 0,     dir.getChildFile (juce::String ("eq1-") + names[t] + ".png"),         false, t);
        renderShot (0, 0,     dir.getChildFile (juce::String ("eq1-") + names[t] + "-account.png"), true,  t);
        renderShot (0, 0,     dir.getChildFile (juce::String ("eq1-") + names[t] + "-bandoff.png"), false, t, false, Strip::bandOff);
        renderShot (940, 580, dir.getChildFile (juce::String ("eq1-") + names[t] + "-min.png"),     false, t, false,
                    t == 0 ? Strip::allEnabled : Strip::standard);                                  // smallest allowed size
    }

    // Optional extra states (they touch this machine's real license state / the network):
    //   --dialogs  the "Save Preset" dialog in each theme. It is a real desktop window, so this
    //              needs an X server with a window manager (a bare xvfb has none and JUCE exits)
    //   --selftest no pictures: checks undo / redo, presets, A/B and the window size (exit code 0 = ok);
    //              with --dialogs also the Save dialog chain
    //   --signin   the "approve this code" screen, from a real sign-in start
    //   --expired  the locked screen after the trial; the trial is restarted afterwards
    const juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--dialogs"))
        for (int t = 0; t < 3; ++t)
            renderDialogShot (dir.getChildFile (juce::String ("eq1-") + names[t] + "-dialog.png"), t);
    if (args.contains ("--signin"))
        renderShot (0, 0, dir.getChildFile ("eq1-paper-signin.png"), true, 0, true);
    if (args.contains ("--expired"))
    {
        const auto now = juce::Time::currentTimeMillis() / 1000;
        wsho::lic::LicenseStore().saveTrial ({ now - 20 * wsho::lic::kDay, now });
        renderShot (0, 0, dir.getChildFile ("eq1-paper-expired.png"), false, 0);
        {   // and the audio really is untouched while locked
            wsho::WhyshoEQAudioProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            for (auto* id : { "on", "gain" })
                if (auto* p = proc.state().getParameter (wsho::pid::band (0, id))) p->setValueNotifyingHost (1.0f);
            juce::AudioBuffer<float> buf (2, 512), ref;
            juce::Random rng (1);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, rng.nextFloat() - 0.5f);
            ref.makeCopyOf (buf);
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            bool same = true;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) same = same && juce::exactlyEqual (buf.getSample (c, i), ref.getSample (c, i));
            std::cout << "locked plugin passes audio through unchanged: " << (same ? "yes" : "NO") << "\n";
        }
        wsho::lic::LicenseStore().saveTrial ({ now, now });
    }
    return 0;
}
