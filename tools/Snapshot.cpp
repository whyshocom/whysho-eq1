// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void renderShot (int w, int h, const juce::File& out, bool openLicense, int themeIndex = 0)
{
    wsho::WhyshoEQAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.state().state.setProperty ("uiTheme", themeIndex, nullptr);

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
    ed->setBounds (0, 0, w, h);
    ed->setVisible (true);

    if (openLicense)
        if (auto* lb = dynamic_cast<juce::Button*> (ed->findChildWithID ("license")))
            lb->triggerClick();

    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    renderShot (1100, 680, dir.getChildFile ("whysho-eq1-main.png"),     false, 0); // Paper (default)
    renderShot (1100, 680, dir.getChildFile ("whysho-eq1-midnight.png"), false, 1); // Midnight
    renderShot (1100, 680, dir.getChildFile ("whysho-eq1-license.png"),  true,  0);
    return 0;
}
