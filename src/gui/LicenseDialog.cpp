#include "LicenseDialog.h"
#include "WhyshoLookAndFeel.h"

using namespace wsho;
using namespace wsho::gui;
using Kind = lic::Verdict::Kind;
using Activity = lic::Status::Activity;

LicenseDialog::LicenseDialog (WhyshoEQAudioProcessor& p) : license (p.license())
{
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);   // nothing underneath is reachable while this is up

    title.setText ("whysho EQ1", juce::dontSendNotification);
    for (auto* l : { &title, &headline, &body, &code, &note })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (l);
    }

    for (auto* b : { &signInBtn, &buyBtn, &checkBtn, &signOutBtn, &reopenBtn, &cancelBtn, &closeBtn })
        addChildComponent (b);

    signInBtn.onClick  = [this] { license.signIn(); };
    cancelBtn.onClick  = [this] { license.cancelSignIn(); };
    reopenBtn.onClick  = [this] { license.reopenBrowser(); };
    checkBtn.onClick   = [this] { license.checkNow(); };
    signOutBtn.onClick = [this] { license.signOut(); };
    buyBtn.onClick     = [] { juce::URL (lic::AccountClient::productPageUrl()).launchInDefaultBrowser(); };
    closeBtn.onClick   = [this] { if (onClose) onClose(); };

    license.addChangeListener (this);
    lookAndFeelChanged();
    refresh();
}

LicenseDialog::~LicenseDialog()
{
    license.removeChangeListener (this);
}

void LicenseDialog::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

bool LicenseDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && closeBtn.isVisible()) { closeBtn.triggerClick(); return true; }
    return false;
}

void LicenseDialog::lookAndFeelChanged()
{
    const auto& t = theme();
    title.setFont (themeFont (13.0f, true));
    title.setColour (juce::Label::textColourId, t.accent);
    headline.setFont (themeFont (19.0f, true));
    headline.setColour (juce::Label::textColourId, t.text);
    body.setFont (themeFont (13.0f));
    body.setColour (juce::Label::textColourId, t.text);
    code.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 34.0f, juce::Font::bold)));
    code.setColour (juce::Label::textColourId, t.text);
    note.setFont (themeFont (12.5f));
    refresh();
}

void LicenseDialog::refresh()
{
    const auto s = license.status();
    const auto& t = theme();
    const bool waiting = s.activity == Activity::waitingForBrowser;
    const bool busy    = s.activity == Activity::contacting;
    const bool licensed = s.verdict.kind == Kind::licensed;

    juce::String head, text;
    if (waiting)
    {
        head = "Approve this code";
        text = "Your browser is opening whysho.com. Sign in there and approve the code below.";
    }
    else if (licensed)
    {
        head = "EQ1 is unlocked";
        text = s.signedIn ? "Signed in as " + s.email + ". Thanks for buying EQ1."
                          : "Bought by " + s.email + ".";
        if (s.verdict.offlineDaysLeft <= 9)
            text << " Go online within " << juce::String (juce::jmax (1, s.verdict.offlineDaysLeft))
                 << (s.verdict.offlineDaysLeft == 1 ? " day" : " days") << " to keep it unlocked.";
    }
    else if (s.verdict.kind == Kind::trial)
    {
        const int d = s.verdict.trialDaysLeft;
        head = "Free trial: " + juce::String (d) + (d == 1 ? " day left" : " days left");
        text = s.signedIn ? "Signed in as " + s.email + ". Buy EQ1 to keep using it after the trial."
                          : "Everything works during the trial. After it, EQ1 needs a whysho account that owns it.";
    }
    else
    {
        head = "The free trial has ended";
        text = s.signedIn ? "Signed in as " + s.email + ". EQ1 is switched off until this account owns it; audio passes through unchanged."
                          : "EQ1 is switched off on this computer and audio passes through unchanged. Sign in to the whysho account that owns EQ1 to switch it back on.";
    }

    headline.setText (head, juce::dontSendNotification);
    body.setText (text, juce::dontSendNotification);
    code.setText (s.userCode, juce::dontSendNotification);
    code.setVisible (waiting);

    note.setText (busy ? juce::String ("Contacting whysho.com...") : s.note, juce::dontSendNotification);
    note.setColour (juce::Label::textColourId, (s.noteIsError && ! busy) ? t.bad : t.text);

    signInBtn.setVisible  (! waiting && ! s.signedIn);
    checkBtn.setVisible   (! waiting && s.signedIn && ! licensed);
    buyBtn.setVisible     (! waiting && ! licensed);
    signOutBtn.setVisible (! waiting && s.signedIn);
    reopenBtn.setVisible  (waiting);
    cancelBtn.setVisible  (waiting);
    closeBtn.setVisible   (s.unlocked());     // a locked plugin keeps this panel up

    signInBtn.setEnabled (! busy);
    checkBtn.setEnabled (! busy);

    shown = s;
    resized();
    repaint();
}

juce::Rectangle<int> LicenseDialog::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (480, getWidth() - 24), 318);
}

void LicenseDialog::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.bg.withAlpha (0.94f));

    const auto panel = panelBounds().toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (t.grid);
    g.drawRoundedRectangle (panel, 10.0f, 1.0f);

    // State stripe: colour carries the state, the text stays full-contrast.
    const auto k = shown.verdict.kind;
    g.setColour (k == Kind::licensed ? t.good : k == Kind::trial ? t.warn : t.bad);
    g.fillRoundedRectangle (panel.withHeight (5.0f).reduced (10.0f, 0.0f).translated (0.0f, 1.0f), 2.5f);
}

void LicenseDialog::resized()
{
    auto r = panelBounds().reduced (26, 20);
    title.setBounds (r.removeFromTop (18));
    r.removeFromTop (8);
    headline.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    body.setBounds (r.removeFromTop (52));
    code.setBounds (r.removeFromTop (code.isVisible() ? 50 : 0));
    r.removeFromTop (4);

    auto buttons = r.removeFromBottom (36);
    r.removeFromBottom (8);
    note.setBounds (r);

    juce::Array<juce::TextButton*> row;
    for (auto* b : { &signInBtn, &checkBtn, &reopenBtn, &buyBtn, &signOutBtn, &cancelBtn, &closeBtn })
        if (b->isVisible()) row.add (b);
    if (row.isEmpty()) return;

    const int gap = 8;
    const int w = (buttons.getWidth() - gap * (row.size() - 1)) / row.size();
    for (auto* b : row)
    {
        b->setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (gap);
    }
}
