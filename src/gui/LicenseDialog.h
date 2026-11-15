#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Account panel shown over the editor: trial state, "Sign in with browser",
// buy / check again / sign out. It only observes lic::LicenseService (which
// owns all network work), so it can be closed or destroyed at any moment.
class LicenseDialog : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit LicenseDialog (WhyshoEQAudioProcessor& p);
    ~LicenseDialog() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    juce::Rectangle<int> panelBounds() const;

    lic::LicenseService& license;
    lic::Status shown;

    juce::Label title, headline, body, code, note;
    juce::TextButton signInBtn { "Sign in with browser" }, buyBtn { "Buy EQ1" },
                     checkBtn { "Check again" }, signOutBtn { "Sign out" },
                     reopenBtn { "Open browser again" }, cancelBtn { "Cancel" },
                     closeBtn { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseDialog)
};
} // namespace wsho::gui
