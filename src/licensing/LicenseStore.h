#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace wsho::lic
{
constexpr int kTrialDays        = 14;   // free use from the first time the plugin is really used
constexpr int kOfflineGraceDays = 30;   // how long a confirmed purchase works without reaching whysho.com
constexpr int kSignedOutGraceDays = 7;  // ... and how long after the server ended the session (password reset etc.)
constexpr juce::int64 kDay = 86400;

// What we remember about the whysho account this computer is signed in to.
struct Account
{
    juce::String token;          // plugin session token (bearer); empty = signed out
    juce::String email;
    bool         owned = false;  // last answer from /api/plugin/license
    juce::int64  checkedAt = 0;  // unix seconds of that answer

    bool signedIn() const { return token.isNotEmpty(); }
};

// The local trial clock. `lastSeen` only ever moves forward, so turning the
// system clock back doesn't buy more time.
struct Trial
{
    juce::int64 start = 0;       // 0 = not started yet
    juce::int64 lastSeen = 0;
    bool started() const { return start > 0; }
};

struct Verdict
{
    enum class Kind { trial, expired, licensed };
    Kind kind = Kind::trial;
    int  trialDaysLeft = kTrialDays;
    int  offlineDaysLeft = 0;    // licensed only: days until an online check is required
    bool unlocked() const { return kind != Kind::expired; }
};

// Pure decision: what is this computer allowed to do right now?
Verdict evaluate (const Account&, const Trial&, juce::int64 now);

// One place a trial record is kept (a file, a registry value, ...).
struct Anchor
{
    std::function<juce::String()> read;
    std::function<void (const juce::String&)> write;
};

// Persistence for the account and the trial clock. Everything here is local
// and therefore only a deterrent: records carry a keyed hash bound to this
// machine so they can't be copied between computers or edited by hand, but
// the key ships in the binary. See docs/LICENSING.md.
class LicenseStore
{
public:
    LicenseStore();                                                                  // real locations
    LicenseStore (juce::File accountFile, std::vector<Anchor>, juce::String machine); // tests

    Account loadAccount() const;
    void    saveAccount (const Account&) const;
    void    clearAccount() const;

    // Reads the trial clock (earliest start / latest lastSeen of all anchors).
    // If anchors exist but none is valid, the trial counts as used up.
    Trial loadTrial() const;
    // Writes the record to every anchor.
    void  saveTrial (const Trial&) const;

private:
    juce::String mac (const juce::String& body) const;

    juce::File accountFile;
    std::vector<Anchor> anchors;
    juce::String machine;
};

} // namespace wsho::lic
