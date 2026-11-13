#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// Modal-style activation panel. Online activation runs on a background thread
// so the UI never freezes; result is posted back to the message thread.
class LicenseDialog : public juce::Component
{
public:
    explicit LicenseDialog (WhyshoEQAudioProcessor& p);
    ~LicenseDialog() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshStatus();

private:
    void doActivate();
    void setBusy (bool b);

    WhyshoEQAudioProcessor& proc;
    juce::Label  title, status, hint;
    juce::TextEditor keyField;
    juce::TextButton activateBtn { "Activate" }, trialBtn { "Start 14-day Trial" },
                     deactivateBtn { "Deactivate" }, buyBtn { "Buy / Manage" }, closeBtn { "Close" };
    std::unique_ptr<std::thread> worker;
    std::atomic<bool> busy { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseDialog)
};
} // namespace wsho::gui
