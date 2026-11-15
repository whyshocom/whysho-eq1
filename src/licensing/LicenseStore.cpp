#include "LicenseStore.h"
#include "MachineId.h"
#include "ObfString.h"
#include <juce_cryptography/juce_cryptography.h>
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace wsho::lic
{
// --------------------------------------------------------------------------
Verdict evaluate (const Account& account, const Trial& trial, juce::int64 now)
{
    // Time never runs backwards: the latest moment we have ever seen wins.
    now = juce::jmax (now, trial.lastSeen);

    Verdict v;
    const juce::int64 trialEnd = (trial.started() ? trial.start : now) + kTrialDays * kDay;
    v.trialDaysLeft = (int) juce::jlimit<juce::int64> (0, kTrialDays, (trialEnd - now + kDay - 1) / kDay);

    const juce::int64 graceEnd = account.checkedAt + kOfflineGraceDays * kDay;
    if (account.owned && account.checkedAt > 0 && now < graceEnd)
    {
        v.kind = Verdict::Kind::licensed;
        v.offlineDaysLeft = (int) ((graceEnd - now) / kDay);
    }
    else
    {
        v.kind = now < trialEnd ? Verdict::Kind::trial : Verdict::Kind::expired;
    }
    return v;
}

// --------------------------------------------------------------------------
namespace
{
juce::File localDataDir()
{
   #if JUCE_WINDOWS
    // Local, not Roaming: these records are bound to this machine.
    auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #elif JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Application Support");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile ("whysho").getChildFile ("EQ1");
}

Anchor fileAnchor (juce::File f)
{
    return { [f] { return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String(); },
             [f] (const juce::String& s) { f.getParentDirectory().createDirectory(); f.replaceWithText (s); } };
}

std::vector<Anchor> defaultAnchors()
{
    std::vector<Anchor> a;
    a.push_back (fileAnchor (localDataDir().getChildFile ("state.dat")));

   #if JUCE_WINDOWS
    const juce::String key = "HKEY_CURRENT_USER\\Software\\whysho\\EQ1\\state";
    a.push_back ({ [key] { return juce::WindowsRegistry::getValue (key, {}); },
                   [key] (const juce::String& s) { juce::WindowsRegistry::setValue (key, s); } });
    a.push_back (fileAnchor (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                                 .getChildFile ("whysho").getChildFile ("eq1.dat")));
   #elif JUCE_MAC
    a.push_back (fileAnchor (juce::File ("~/Library/Preferences/com.whysho.eq1.state")));
    a.push_back (fileAnchor (juce::File ("/Users/Shared/.whysho/eq1.dat")));
   #else
    a.push_back (fileAnchor (juce::File ("~/.config/whysho/eq1.state")));
   #endif
    return a;
}
} // namespace

LicenseStore::LicenseStore()
    : accountFile (localDataDir().getChildFile ("account.json")),
      anchors (defaultAnchors()),
      machine (getMachineId())
{
}

LicenseStore::LicenseStore (juce::File f, std::vector<Anchor> a, juce::String m)
    : accountFile (std::move (f)), anchors (std::move (a)), machine (std::move (m))
{
}

juce::String LicenseStore::mac (const juce::String& body) const
{
    const juce::String keyed = WSHO_JS ("wsho-eq1-local-v2") + "|" + machine + "|" + body;
    return juce::SHA256 (keyed.toRawUTF8(), keyed.getNumBytesAsUTF8()).toHexString();
}

// ---- account ---------------------------------------------------------------
static juce::String accountBody (const Account& a)
{
    return a.token + "|" + a.email + "|" + (a.owned ? "1" : "0") + "|" + juce::String (a.checkedAt);
}

Account LicenseStore::loadAccount() const
{
    Account a;
    if (! accountFile.existsAsFile()) return a;

    const auto json = juce::JSON::parse (accountFile.loadFileAsString());
    a.token     = json.getProperty ("token", {}).toString();
    a.email     = json.getProperty ("email", {}).toString();
    a.owned     = (bool) json.getProperty ("owned", false);
    a.checkedAt = (juce::int64) json.getProperty ("checkedAt", 0);

    // A record that doesn't verify (edited, copied from another computer, or the
    // machine id changed) keeps the sign-in but loses the cached answer: the next
    // online check restores it for a real owner.
    if (json.getProperty ("mac", {}).toString() != mac (accountBody (a)))
    {
        a.owned = false;
        a.checkedAt = 0;
    }
    return a;
}

void LicenseStore::saveAccount (const Account& a) const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("token", a.token);
    o->setProperty ("email", a.email);
    o->setProperty ("owned", a.owned);
    o->setProperty ("checkedAt", a.checkedAt);
    o->setProperty ("mac", mac (accountBody (a)));

    accountFile.getParentDirectory().createDirectory();
    accountFile.replaceWithText (juce::JSON::toString (juce::var (o.get())));
   #if ! JUCE_WINDOWS
    chmod (accountFile.getFullPathName().toRawUTF8(), 0600);   // holds a session token
   #endif
}

void LicenseStore::clearAccount() const { accountFile.deleteFile(); }

// ---- trial -----------------------------------------------------------------
Trial LicenseStore::loadTrial() const
{
    Trial t;
    bool anyPresent = false, anyValid = false;

    for (auto& anchor : anchors)
    {
        const auto raw = anchor.read();
        if (raw.isEmpty()) continue;
        anyPresent = true;

        const auto parts = juce::StringArray::fromTokens (raw, "|", {});
        if (parts.size() != 3) continue;
        const juce::int64 start = parts[0].getLargeIntValue(), seen = parts[1].getLargeIntValue();
        if (start <= 0 || seen < start || parts[2] != mac (parts[0] + "|" + parts[1])) continue;

        t.start    = anyValid ? juce::jmin (t.start, start) : start;
        t.lastSeen = juce::jmax (t.lastSeen, seen);
        anyValid = true;
    }

    // Records exist but none can be trusted: the trial counts as used up
    // (signing in still works). Deliberately not written back.
    if (anyPresent && ! anyValid)
        t.start = 1;

    return t;
}

void LicenseStore::saveTrial (const Trial& t) const
{
    const juce::String body = juce::String (t.start) + "|" + juce::String (t.lastSeen);
    const juce::String record = body + "|" + mac (body);
    for (auto& anchor : anchors) anchor.write (record);
}

} // namespace wsho::lic
