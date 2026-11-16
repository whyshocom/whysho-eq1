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

// The graph on its own, while audio plays: spectrum, dynamic bands, zoom range.
// The audio goes through processBlock; when EQ1 is switched off on this machine
// (trial over, not signed in) processBlock passes it through untouched, so the
// analyzer taps are then fed directly, to still show the spectrum drawing.
static void renderAnalyzer (const juce::File& out, int themeIndex, int dbRange = 12, int analyzerSource = 3, float gainScale = 1.0f)
{
    using namespace wsho;
    WhyshoEQAudioProcessor proc;
    proc.enableAllBuses();      // the sidechain input too
    proc.prepareToPlay (48000.0, 512);
    gui::setThemeIndex (themeIndex);

    auto set = [&] (const juce::String& id, float v)
    { if (auto* p = proc.state().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    for (int i = 0; i < 6; ++i) set (pid::band (i, pid::on), 1.0f);
    set (pid::band (0, pid::freq), 45.0f);    // high-pass (no gain), dynamic
    set (pid::band (0, pid::dyn), 1.0f);     set (pid::band (0, pid::dynThresh), -50.0f);
    set (pid::band (1, pid::freq), 110.0f);  set (pid::band (1, pid::gain), 3.5f);
    set (pid::band (2, pid::freq), 450.0f);  set (pid::band (2, pid::gain), -4.0f);
    set (pid::band (3, pid::freq), 1600.0f); set (pid::band (3, pid::gain), 2.5f);    // dynamic bell, cutting
    set (pid::band (3, pid::dyn), 1.0f);     set (pid::band (3, pid::dynThresh), -50.0f);
    set (pid::band (4, pid::freq), 5200.0f); set (pid::band (4, pid::gain), 8.0f);    // off the top at +/-6 dB
    set (pid::band (5, pid::freq), 12000.0f);set (pid::band (5, pid::gain), -2.0f);
    set (pid::band (5, pid::dyn), 1.0f);     set (pid::band (5, pid::dynThresh), -50.0f);
    set (pid::band (5, pid::dynRange), 5.0f);                                         // dynamic shelf, boosting
    set (pid::analyzer, (float) analyzerSource);
    set (pid::gainScale, gainScale);
    proc.state().state.setProperty ("dbRange", dbRange, nullptr);

    gui::AnalyzerComponent graph (proc);
    graph.setBounds (0, 0, 1004, 360);
    graph.setSelectedBand (3);

    // About 2.5 s of pink noise plus a -12 dBFS 1 kHz sine; a quieter, duller copy
    // for the sidechain (and for the post tap when it has to be fed directly).
    const bool feedTaps = ! proc.license().unlocked();
    juce::AudioBuffer<float> buf (proc.getTotalNumInputChannels(), 512);
    juce::MidiBuffer midi;
    juce::Random rng (7);
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, dull = 0.0f;
    double phase = 0.0;
    for (int frame = 0; frame < 60; ++frame)
    {
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float white = rng.nextFloat() * 2.0f - 1.0f;      // Paul Kellet's economy pink filter
                b0 = 0.99765f * b0 + white * 0.0990460f;
                b1 = 0.96300f * b1 + white * 0.2965164f;
                b2 = 0.57000f * b2 + white * 1.0526913f;
                const float s = (b0 + b1 + b2 + white * 0.1848f) * 0.05f + 0.2512f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                dull += 0.2f * (s - dull);
                for (int c = 0; c < buf.getNumChannels(); ++c) buf.setSample (c, i, c < 2 ? s : dull * 0.5f);
                if (feedTaps)
                {
                    if (analyzerSource == 1 || analyzerSource == 3) proc.preAnalyzer().push (s);
                    if (analyzerSource == 2 || analyzerSource == 3) proc.postAnalyzer().push (dull);
                    if (analyzerSource == 4)                        proc.scAnalyzer().push (dull * 0.5f);
                }
            }
            proc.processBlock (buf, midi);
        }
        graph.createComponentSnapshot (graph.getLocalBounds());   // offscreen, each paint is an analyzer frame
    }

    auto img = graph.createComponentSnapshot (graph.getLocalBounds(), false, 2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << (feedTaps ? " (EQ1 is switched off here: taps fed directly)" : "") << "\n";
}

// --hero: the whole window in use, for the website: a factory preset loaded and
// music-like audio running, at the editor's default size (1x, like the other
// plugins' product shots).
static void renderHero (const juce::File& out, const juce::String& preset, int selectBand)
{
    using namespace wsho;
    WhyshoEQAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    gui::loadThemePreference();
    gui::setThemeIndex (0);
    proc.presetManager().applyFactory (preset);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);    // parameters reach the UI
    if (auto* graph = dynamic_cast<gui::AnalyzerComponent*> (ed->findChildWithID ("analyzer")))
    {
        graph->setSelectedBand (selectBand);
        if (graph->onBandSelected) graph->onBandSelected (selectBand);
    }

    // Pink noise with a slow swell and a few tones standing in for a mix.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (11);
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    double t = 0.0;
    for (int frame = 0; frame < 70; ++frame)
    {
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i, t += 1.0 / 48000.0)
            {
                const float white = rng.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + white * 0.0990460f;
                b1 = 0.96300f * b1 + white * 0.2965164f;
                b2 = 0.57000f * b2 + white * 1.0526913f;
                const double tau = juce::MathConstants<double>::twoPi * t;
                const float tones = 0.20f * (float) std::sin (tau * 82.0) + 0.10f * (float) std::sin (tau * 247.0)
                                  + 0.07f * (float) std::sin (tau * 1320.0) + 0.05f * (float) std::sin (tau * 6200.0);
                const float s = (b0 + b1 + b2 + white * 0.1848f) * 0.07f + tones;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            proc.processBlock (buf, midi);
        }
        ed->createComponentSnapshot (ed->getLocalBounds());   // each offscreen paint is an analyzer frame
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

// What the analyzer reads for a -12 dBFS 1 kHz sine, at each FFT size.
static void measureAnalyzer()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        wsho::dsp::SpectrumAnalyzer an;
        an.prepare (sr);
        an.render();
        double phase = 0.0;
        for (int hop = 0; hop < 200; ++hop)
        {
            for (int i = 0; i < an.hopSize(); ++i)
            {
                an.push (0.2512f * (float) std::sin (phase));
                phase += juce::MathConstants<double>::twoPi * 1000.0 / sr;
            }
            an.render();
        }
        const auto& lv = an.levels();
        const auto peak = std::max_element (lv.begin(), lv.end());
        const int i = (int) (peak - lv.begin());
        std::cout << sr << " Hz: FFT " << an.fftSize() << ", hop " << an.hopSize() << ", bin " << sr / an.fftSize()
                  << " Hz, " << sr / an.hopSize() << " frames/s; -12 dBFS 1 kHz sine reads " << *peak
                  << " dB at " << wsho::dsp::SpectrumAnalyzer::pointFreq (i) << " Hz\n";
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

    // The graph with audio playing, in each theme: pre+post, then the sidechain; then
    // the other zoom ranges, and Scale at 50% (the nodes must stay on the curve).
    for (int t = 0; t < 3; ++t)
    {
        renderAnalyzer (dir.getChildFile (juce::String ("graph-") + names[t] + ".png"), t);
        renderAnalyzer (dir.getChildFile (juce::String ("graph-") + names[t] + "-sidechain.png"), t, 12, 4);
    }
    renderAnalyzer (dir.getChildFile ("graph-paper-6db.png"),  0, 6);
    renderAnalyzer (dir.getChildFile ("graph-paper-30db.png"), 0, 30);
    renderAnalyzer (dir.getChildFile ("graph-paper-scale50.png"), 0, 12, 1, 0.5f);
    renderAnalyzer (dir.getChildFile ("graph-midnight-post.png"), 1, 12, 2);

    // Optional extra states (they touch this machine's real license state / the network):
    //   --dialogs  the "Save Preset" dialog in each theme. It is a real desktop window, so this
    //              needs an X server with a window manager (a bare xvfb has none and JUCE exits)
    //   --selftest no pictures: checks undo / redo, presets, A/B and the window size (exit code 0 = ok);
    //              with --dialogs also the Save dialog chain
    //   --signin   the "approve this code" screen, from a real sign-in start
    //   --expired  the locked screen after the trial; the trial is restarted afterwards
    const juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--hero"))
    {
        //   --hero     also the product shot for the website (eq1-hero.png)
        renderHero (dir.getChildFile ("eq1-hero.png"), "Vocal Presence", 8);   // band 9, the presence bell
        return 0;
    }
    if (args.contains ("--measure")) measureAnalyzer();
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
