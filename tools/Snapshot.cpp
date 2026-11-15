// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void renderShot (int w, int h, const juce::File& out, bool openLicense, int themeIndex = 0, bool clickSignIn = false)
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    const char* names[] = { "paper", "midnight", "graphite" };
    for (int t = 0; t < 3; ++t)
    {
        renderShot (0, 0,     dir.getChildFile (juce::String ("eq1-") + names[t] + ".png"),         false, t);
        renderShot (0, 0,     dir.getChildFile (juce::String ("eq1-") + names[t] + "-account.png"), true,  t);
    }
    renderShot (940, 580, dir.getChildFile ("eq1-paper-min.png"), false, 0);   // smallest allowed size

    // Optional extra states (they touch this machine's real license state / the network):
    //   --signin   the "approve this code" screen, from a real sign-in start
    //   --expired  the locked screen after the trial; the trial is restarted afterwards
    const juce::StringArray args (argv + 1, argc - 1);
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
