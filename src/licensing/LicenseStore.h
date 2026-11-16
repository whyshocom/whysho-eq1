#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include "LicenseToken.h"
#include <vector>

namespace wsho::lic
{
constexpr int kTrialDays        = 14;   // free use from the first time the plugin is really used
constexpr int kSignedOutGraceDays = 7;  // an owner keeps working this long after the server ended the session
constexpr juce::int64 kDay = 86400;

// What we remember about the whysho account this computer is signed in to.
struct Account
{
    juce::String token;          // plugin session token (bearer); empty = signed out
    juce::String email;
    juce::String license;        // the last signed answer from /api/plugin/license, kept verbatim
    juce::int64  signedOutAt = 0;// when the server ended the session while we still held a license

    // Read out of `license` after its signature was verified (never stored on their own):
    bool         owned = false;
    juce::int64  checkedAt = 0;  // when the site issued the answer (its clock)
    juce::int64  validUntil = 0; // how long the site lets it stand without a new check (30 days)

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

// Persistence for the account and the trial clock.
//  - The license is the site's signed answer: it can't be forged or edited, and
//    it names this machine, so it can't be copied from another computer either.
//  - The trial clock is local and therefore only a deterrent: its records carry
//    a keyed hash bound to this machine, but that key ships in the binary.
// See docs/LICENSING.md.
class LicenseStore
{
public:
    LicenseStore();                                                                  // real locations
    LicenseStore (juce::File accountFile, std::vector<Anchor>, juce::String machine, PublicKey); // tests

    Account loadAccount() const;
    void    saveAccount (const Account&) const;
    void    clearAccount() const;

    // Takes a signed answer from the site. Returns false (account untouched) unless
    // the signature is good, it is for this machine, and - for a fresh online
    // answer - it echoes the nonce we just sent (so an old answer can't be replayed).
    bool acceptLicense (Account&, const juce::String& licenseToken, const juce::String& expectedNonce = {}) const;

    const juce::String& machineId() const { return machine; }

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
    PublicKey publicKey;
};

} // namespace wsho::lic
