#include "LicenseService.h"

namespace wsho::lic
{
namespace
{
constexpr uint32_t kOpen = 0xA5A5C3C3u, kShut = 0x12345678u, kSalt = 0x9E3779B9u;
constexpr juce::int64 kRecheckAfter = 12 * 3600;   // refresh a cached answer this often

juce::int64 nowSeconds() { return juce::Time::currentTimeMillis() / 1000; }
}

LicenseService::LicenseService() : juce::Thread ("whysho license")
{
    account = store.loadAccount();
    trial   = store.loadTrial();
    reevaluate();

    // No network here: a host that only scans the plugin must not wait on us.
    // The first online check happens once the plugin is really used (markUsed).
    startTimer (30 * 1000);
}

LicenseService::~LicenseService()
{
    stopTimer();
    signalThreadShouldExit();
    notify();
    stopThread (12000);
}

// ---- audio-thread view -----------------------------------------------------
void LicenseService::setUnlocked (bool open) noexcept
{
    const uint32_t a = open ? kOpen : kShut;
    flagA.store (a, std::memory_order_relaxed);
    flagB.store (a ^ kSalt, std::memory_order_relaxed);
}

bool LicenseService::unlocked() const noexcept
{
    const uint32_t a = flagA.load (std::memory_order_relaxed);
    const uint32_t b = flagB.load (std::memory_order_relaxed);
    return a == kOpen && (a ^ b) == kSalt;
}

// ---- state -----------------------------------------------------------------
Status LicenseService::status() const
{
    const juce::ScopedLock sl (lock);
    return current;
}

void LicenseService::reevaluate()
{
    bool changed = false;
    {
        const juce::ScopedLock sl (lock);
        const auto now = nowSeconds();

        // Keep the forward-only clock moving; persist it about hourly.
        if (trial.start > 1)     // a real, trusted record (1 = "untrusted, used up": never written back)
        {
            trial.lastSeen = juce::jmax (trial.lastSeen, now);
            if (trial.lastSeen - lastTrialWrite >= 3600)
            {
                lastTrialWrite = trial.lastSeen;
                store.saveTrial (trial);
            }
        }

        const auto before = current;
        current.verdict      = evaluate (account, trial, now);
        current.trialStarted = trial.started();
        current.signedIn     = account.signedIn();
        current.email        = account.email;
        setUnlocked (current.unlocked());

        changed = before.verdict.kind != current.verdict.kind
               || before.verdict.trialDaysLeft != current.verdict.trialDaysLeft
               || before.verdict.offlineDaysLeft != current.verdict.offlineDaysLeft
               || before.signedIn != current.signedIn
               || before.email != current.email
               || before.trialStarted != current.trialStarted;
    }
    if (changed) sendChangeMessage();
}

void LicenseService::markUsed()
{
    bool firstCheck = false, startedTrial = false;
    {
        const juce::ScopedLock sl (lock);
        firstCheck = account.signedIn() && lastCheckAttempt == 0;
        if (firstCheck) lastCheckAttempt = nowSeconds();
        if (! trial.started())
        {
            trial.start = trial.lastSeen = lastTrialWrite = nowSeconds();
            store.saveTrial (trial);
            startedTrial = true;
        }
    }
    if (startedTrial) reevaluate();
    if (firstCheck)   request (wantCheck);     // confirm the license once per DAW launch
}

void LicenseService::setActivity (Status::Activity a, const juce::String& userCode)
{
    {
        const juce::ScopedLock sl (lock);
        current.activity = a;
        current.userCode = userCode;
    }
    sendChangeMessage();
}

void LicenseService::setNote (const juce::String& text, bool isError)
{
    {
        const juce::ScopedLock sl (lock);
        current.note = text;
        current.noteIsError = isError;
    }
    sendChangeMessage();
}

// ---- requests (message thread) ---------------------------------------------
void LicenseService::request (std::atomic<bool>& flag)
{
    flag = true;
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
    notify();
}

void LicenseService::signIn()        { cancelLogin = false; request (wantSignIn); }
void LicenseService::cancelSignIn()  { cancelLogin = true; notify(); }
void LicenseService::checkNow()      { request (wantUserCheck); }

void LicenseService::reopenBrowser() const
{
    const auto code = status().userCode;
    if (code.isNotEmpty())
        juce::URL (AccountClient::activationPageUrl (code)).launchInDefaultBrowser();
}

void LicenseService::signOut()
{
    {
        const juce::ScopedLock sl (lock);
        if (! account.signedIn()) return;
        signOutToken = account.token;
        account = {};
        store.clearAccount();
        current.note = {};
    }
    reevaluate();
    request (wantSignOut);   // tell the server, best effort
}

void LicenseService::timerCallback()
{
    reevaluate();

    bool due = false;
    {
        const juce::ScopedLock sl (lock);
        // Only once the plugin is in real use (lastCheckAttempt set by markUsed).
        due = account.signedIn() && lastCheckAttempt > 0 && nowSeconds() - lastCheckAttempt > kRecheckAfter;
    }
    if (due) request (wantCheck);
}

// ---- worker thread ---------------------------------------------------------
void LicenseService::run()
{
    while (! threadShouldExit())
    {
        if      (wantSignOut.exchange (false))   doSignOut();
        else if (wantSignIn.exchange (false))    doSignIn();
        else if (wantUserCheck.exchange (false)) doCheck (true);
        else if (wantCheck.exchange (false))     doCheck (false);
        else                                     wait (-1);
    }
}

void LicenseService::doSignOut()
{
    juce::String token;
    {
        const juce::ScopedLock sl (lock);
        token = std::exchange (signOutToken, {});
    }
    if (token.isNotEmpty()) client.logout (token);
}

void LicenseService::doSignIn()
{
    setNote ({}, false);
    setActivity (Status::Activity::contacting);

    const auto start = client.startLogin ("whysho EQ1 on " + juce::SystemStats::getComputerName());
    if (! start.ok)
    {
        setNote ("Couldn't reach whysho.com. Check your internet connection and try again.", true);
        setActivity (Status::Activity::idle);
        return;
    }

    setActivity (Status::Activity::waitingForBrowser, start.userCode);
    juce::MessageManager::callAsync ([url = AccountClient::activationPageUrl (start.userCode)]
                                     { juce::URL (url).launchInDefaultBrowser(); });

    const auto deadline = juce::Time::currentTimeMillis() + (juce::int64) start.expiresInSeconds * 1000;
    juce::String failure = "That code expired before it was approved. Try again.";

    for (;;)
    {
        wait (start.intervalSeconds * 1000);
        if (threadShouldExit()) return;
        if (cancelLogin.exchange (false)) { failure = {}; break; }
        if (juce::Time::currentTimeMillis() > deadline) break;

        const auto poll = client.pollLogin (start.deviceCode);
        if (threadShouldExit()) return;
        if (poll.state == AccountClient::Poll::expired) break;
        if (poll.state != AccountClient::Poll::approved) continue;   // pending, or a network blip

        {
            const juce::ScopedLock sl (lock);
            account = {};
            account.token = poll.token;
            account.email = poll.email;
            store.saveAccount (account);
        }
        setActivity (Status::Activity::contacting);
        doCheck (true);
        return;
    }

    if (failure.isNotEmpty()) setNote (failure, true);
    setActivity (Status::Activity::idle);
}

void LicenseService::doCheck (bool userAsked)
{
    juce::String token;
    {
        const juce::ScopedLock sl (lock);
        token = account.token;
        lastCheckAttempt = nowSeconds();
    }
    if (token.isEmpty()) { setActivity (Status::Activity::idle); return; }
    if (userAsked) setActivity (Status::Activity::contacting);

    const auto answer = client.checkLicense (token);
    if (threadShouldExit()) return;

    juce::String note;
    bool noteIsError = false, haveNote = userAsked, stale = false;
    {
        const juce::ScopedLock sl (lock);
        stale = account.token != token;                     // signed out meanwhile
        if (! stale)
        {
        // Stamp answers on the same forward-only clock evaluate() uses; otherwise a
        // clock that was once wrong could leave a real owner looking expired.
        const auto now = juce::jmax (nowSeconds(), trial.lastSeen);

        switch (answer.state)
        {
            case AccountClient::Ownership::owned:
                account.owned = true;
                account.checkedAt = now;
                account.email = answer.email;
                store.saveAccount (account);
                haveNote = true;                            // clears any earlier "couldn't reach" note
                break;

            case AccountClient::Ownership::notOwned:        // never bought, or refunded
                account.owned = false;
                account.checkedAt = now;
                account.email = answer.email;
                store.saveAccount (account);
                note = account.email + " doesn't own EQ1 yet.";
                haveNote = true;
                break;

            case AccountClient::Ownership::invalidToken:
                // The server ended this session (password reset, a year passed, signed
                // out on the site). Don't cut an owner off mid-project: keep what we
                // knew for a few more days and ask them to sign in again.
                account.token.clear();
                if (account.owned)
                    account.checkedAt = juce::jmin (account.checkedAt,
                                                    now - (kOfflineGraceDays - kSignedOutGraceDays) * kDay);
                store.saveAccount (account);
                note = "You've been signed out. Sign in again to keep EQ1 unlocked.";
                noteIsError = haveNote = true;
                break;

            case AccountClient::Ownership::unreachable:
                note = "Couldn't reach whysho.com. Check your internet connection and try again.";
                noteIsError = true;
                break;
        }
        }
    }

    if (haveNote && ! stale) setNote (note, noteIsError);
    setActivity (Status::Activity::idle);
    reevaluate();
}

} // namespace wsho::lic
