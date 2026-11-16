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
    //   --signin   the "approve this code" screen, from a real sign-in start
    //   --expired  the locked screen after the trial; the trial is restarted afterwards
    const juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--measure")) measureAnalyzer();
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
