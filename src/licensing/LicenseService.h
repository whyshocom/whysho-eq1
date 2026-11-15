#pragma once
#include <juce_events/juce_events.h>
#include <atomic>
#include "AccountClient.h"
#include "LicenseStore.h"

namespace wsho::lic
{
// What the UI shows. A plain copy, safe to keep around.
struct Status
{
    Verdict verdict;
    bool trialStarted = false;
    bool signedIn = false;
    juce::String email;

    enum class Activity { idle, contacting, waitingForBrowser };
    Activity activity = Activity::idle;
    juce::String userCode;       // while waitingForBrowser: the code to approve on whysho.com

    juce::String note;           // one line about the last thing that happened
    bool noteIsError = false;

    bool unlocked() const { return verdict.unlocked(); }
};

// The one license authority for the whole process. Every plugin instance shares
// it (juce::SharedResourcePointer), so signing in once unlocks every open EQ1
// and a session with 60 instances does the file / network work once.
//
//  - Audio thread:   unlocked() only (lock-free).
//  - Message thread: status(), the request methods, change callbacks.
//  - Own thread:     all network I/O.
class LicenseService : public juce::ChangeBroadcaster,
                       private juce::Thread,
                       private juce::Timer
{
public:
    LicenseService();
    ~LicenseService() override;

    bool unlocked() const noexcept;
    Status status() const;

    // Starts the 14-day trial clock if it isn't running. Called when the plugin
    // is actually put to use (audio prepared / editor opened), not when a host
    // merely scans it.
    void markUsed();

    void signIn();               // "Sign in with browser" (device-code flow)
    void cancelSignIn();
    void reopenBrowser() const;
    void signOut();
    void checkNow();             // ask whysho.com again (e.g. right after buying)

private:
    void run() override;
    void timerCallback() override;

    void doSignIn();
    void doCheck (bool userAsked);
    void doSignOut();

    void reevaluate();                                     // recompute verdict + audio flag, notify UI
    void setActivity (Status::Activity, const juce::String& userCode = {});
    void setNote (const juce::String&, bool isError);
    void request (std::atomic<bool>& flag);
    void setUnlocked (bool) noexcept;

    AccountClient client;
    LicenseStore  store;

    mutable juce::CriticalSection lock;                    // guards everything below
    Account account;
    Trial   trial;
    Status  current;
    juce::int64 lastTrialWrite = 0, lastCheckAttempt = 0;

    std::atomic<bool> wantSignIn { false }, wantCheck { false }, wantUserCheck { false },
                      wantSignOut { false }, cancelLogin { false };
    juce::String signOutToken;

    // The audio thread's view, stored twice so a single patched byte isn't enough.
    std::atomic<uint32_t> flagA { 0 }, flagB { 0 };
};

} // namespace wsho::lic
