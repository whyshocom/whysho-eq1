#include "LicenseDialog.h"
#include "WhyshoLookAndFeel.h"
#include "../licensing/ObfString.h"
#include <thread>

using namespace wsho;
using namespace wsho::gui;

LicenseDialog::LicenseDialog (WhyshoEQAudioProcessor& p) : proc (p)
{
    title.setText ("whysho EQ1 - Activation", juce::dontSendNotification);
    title.setFont (juce::Font (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme().text);
    addAndMakeVisible (title);

    keyField.setTextToShowWhenEmpty ("WSHO-XXXXX-XXXXX-XXXXX-XXXXX", theme().textDim);
    keyField.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 15.0f, 0));
    keyField.setJustification (juce::Justification::centred);
    addAndMakeVisible (keyField);

    hint.setText ("Enter the license key from your purchase email, or start a free trial.",
                  juce::dontSendNotification);
    hint.setFont (12.0f);
    hint.setColour (juce::Label::textColourId, theme().textDim);
    hint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hint);

    status.setJustificationType (juce::Justification::centred);
    status.setFont (13.0f);
    addAndMakeVisible (status);

    for (auto* b : { &activateBtn, &trialBtn, &deactivateBtn, &buyBtn, &closeBtn })
        addAndMakeVisible (b);

    activateBtn.onClick = [this] { doActivate(); };
    trialBtn.onClick = [this]
    {
        proc.licenseManager().startOrCheckTrial();
        proc.refreshLicense();
        refreshStatus();
    };
    deactivateBtn.onClick = [this]
    {
        const auto key = keyField.getText().trim();
        if (key.isNotEmpty()) proc.activationClient().deactivate (key);
        proc.licenseManager().clearToken();
        proc.refreshLicense();
        refreshStatus();
    };
    buyBtn.onClick = [] { juce::URL (WSHO_JS ("https://whysho.com/eq1")).launchInDefaultBrowser(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };

    refreshStatus();
}

LicenseDialog::~LicenseDialog()
{
    if (worker && worker->joinable()) worker->join();
}

void LicenseDialog::setBusy (bool b)
{
    busy = b;
    activateBtn.setEnabled (! b);
    activateBtn.setButtonText (b ? "Activating..." : "Activate");
}

void LicenseDialog::doActivate()
{
    if (busy) return;
    const auto key = keyField.getText().trim();
    if (key.length() < 8) { status.setText ("Please enter a valid license key.", juce::dontSendNotification);
                            status.setColour (juce::Label::textColourId, theme().warn); return; }
    setBusy (true);
    status.setText ("Contacting activation server...", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, theme().textDim);

    if (worker && worker->joinable()) worker->join();
    worker = std::make_unique<std::thread> ([this, key]
    {
        auto res = proc.activationClient().activate (key);
        juce::MessageManager::callAsync ([this, res]
        {
            setBusy (false);
            if (res.ok)
            {
                auto info = proc.licenseManager().verifyToken (res.token);
                if (info.state == lic::LicenseState::Licensed)
                {
                    proc.licenseManager().saveToken (res.token);
                    proc.refreshLicense();
                    status.setText ("Activated. Thank you!", juce::dontSendNotification);
                    status.setColour (juce::Label::textColourId, theme().good);
                }
                else
                {
                    status.setText ("Token rejected: " + info.reason, juce::dontSendNotification);
                    status.setColour (juce::Label::textColourId, theme().bad);
                }
            }
            else
            {
                status.setText (res.error, juce::dontSendNotification);
                status.setColour (juce::Label::textColourId, theme().bad);
            }
            refreshStatus();
        });
    });
}

void LicenseDialog::refreshStatus()
{
    const auto& i = proc.licenseInfo;
    juce::String s; juce::Colour c = theme().textDim;
    switch (i.state)
    {
        case lic::LicenseState::Licensed:  s = "Licensed (" + i.edition + ") - " + i.email; c = theme().good; break;
        case lic::LicenseState::Trial:     { const juce::int64 secs = i.expiresAt - juce::Time::getCurrentTime().toMilliseconds() / 1000;
                                             const juce::int64 days = secs > 0 ? secs / 86400 : 0;
                                             s = "Trial active - " + juce::String (days) + " day(s) left"; c = theme().warn; } break;
        case lic::LicenseState::Expired:   s = "Trial/license expired - please activate"; c = theme().bad; break;
        case lic::LicenseState::MachineMismatch: s = "License is bound to another machine"; c = theme().bad; break;
        case lic::LicenseState::Tampered:  s = "License data invalid"; c = theme().bad; break;
        default:                           s = "Unlicensed - audio is periodically interrupted"; c = theme().bad; break;
    }
    status.setText (s, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, c);
}

void LicenseDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg.withAlpha (0.92f));
    auto panel = getLocalBounds().withSizeKeepingCentre (440, 300).toFloat();
    g.setColour (theme().panel);
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (theme().accent.withAlpha (0.5f));
    g.drawRoundedRectangle (panel, 10.0f, 1.5f);
}

void LicenseDialog::resized()
{
    auto panel = getLocalBounds().withSizeKeepingCentre (440, 300).reduced (24);
    title.setBounds (panel.removeFromTop (28));
    panel.removeFromTop (6);
    status.setBounds (panel.removeFromTop (22));
    panel.removeFromTop (10);
    keyField.setBounds (panel.removeFromTop (34));
    panel.removeFromTop (4);
    hint.setBounds (panel.removeFromTop (32));
    panel.removeFromTop (8);

    auto row1 = panel.removeFromTop (34);
    activateBtn.setBounds (row1.removeFromLeft (row1.getWidth()/2).reduced (3));
    trialBtn.setBounds (row1.reduced (3));
    panel.removeFromTop (6);
    auto row2 = panel.removeFromTop (34);
    deactivateBtn.setBounds (row2.removeFromLeft (row2.getWidth()/3).reduced (3));
    buyBtn.setBounds (row2.removeFromLeft (row2.getWidth()/2).reduced (3));
    closeBtn.setBounds (row2.reduced (3));
}
