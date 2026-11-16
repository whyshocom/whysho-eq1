// Test runner. Licensing rules here (trial clock, offline grace, tamper handling);
// DSP stress tests in DspTests.cpp; undo, presets and A/B in WorkflowTests.cpp.
//   WhyshoEQ1_Tests          unit tests, no network
//   WhyshoEQ1_Tests --live   also talks to the real whysho.com plugin API
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "licensing/AccountClient.h"
#include "licensing/LicenseStore.h"
#include <iostream>
#include <map>

using namespace wsho::lic;
using Kind = Verdict::Kind;

int failures = 0;
void runDspTests();
void runWorkflowTests();
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (false)

// In-memory anchors so tests never touch the real machine state.
struct FakeAnchors
{
    std::map<int, juce::String> slots;
    std::vector<Anchor> make (int n)
    {
        std::vector<Anchor> a;
        for (int i = 0; i < n; ++i)
            a.push_back ({ [this, i] { return slots[i]; }, [this, i] (const juce::String& s) { slots[i] = s; } });
        return a;
    }
};

static void testVerdicts()
{
    const juce::int64 t0 = 1'800'000'000;
    Account none;

    // Not started yet (plugin only scanned): full trial, unlocked.
    CHECK (evaluate (none, {}, t0).kind == Kind::trial);
    CHECK (evaluate (none, {}, t0).trialDaysLeft == 14);

    Trial tr { t0, t0 };
    CHECK (evaluate (none, tr, t0 + 1).trialDaysLeft == 14);
    CHECK (evaluate (none, tr, t0 + 13 * kDay + 5).trialDaysLeft == 1);
    CHECK (evaluate (none, tr, t0 + 14 * kDay - 1).unlocked());
    CHECK (evaluate (none, tr, t0 + 14 * kDay).kind == Kind::expired);       // day 14 is the end
    CHECK (! evaluate (none, tr, t0 + 400 * kDay).unlocked());

    // Turning the clock back after expiry doesn't help: lastSeen wins.
    Trial seen { t0, t0 + 20 * kDay };
    CHECK (evaluate (none, seen, t0 + 2 * kDay).kind == Kind::expired);

    // Owner: unlocked regardless of the trial, for 30 days without the server.
    Account owner { "tok", "a@b.c", true, t0 + 20 * kDay };
    CHECK (evaluate (owner, seen, t0 + 21 * kDay).kind == Kind::licensed);
    CHECK (evaluate (owner, seen, t0 + 49 * kDay).kind == Kind::licensed);
    CHECK (evaluate (owner, seen, t0 + 51 * kDay).kind == Kind::expired);    // grace over, trial long gone

    // Signed in but the account doesn't own it: back to the trial rules.
    Account notOwner { "tok", "a@b.c", false, t0 };
    CHECK (evaluate (notOwner, tr, t0 + kDay).kind == Kind::trial);
    CHECK (evaluate (notOwner, tr, t0 + 15 * kDay).kind == Kind::expired);
}

static void testStore()
{
    auto dir = juce::File::createTempFile ("wsho-test");
    dir.createDirectory();
    const auto file = dir.getChildFile ("account.json");
    const juce::int64 t0 = 1'800'000'000;

    {   // account round trip
        FakeAnchors fa;
        LicenseStore store (file, fa.make (2), "machine-A");
        CHECK (! store.loadAccount().signedIn());
        store.saveAccount ({ "tok", "a@b.c", true, t0 });
        const auto a = store.loadAccount();
        CHECK (a.token == "tok" && a.email == "a@b.c" && a.owned && a.checkedAt == t0);

        // Copied to another computer: still signed in, but must re-check online.
        LicenseStore other (file, fa.make (2), "machine-B");
        CHECK (other.loadAccount().signedIn() && ! other.loadAccount().owned);

        // Hand-edited "owned": false -> true with a stale mac is ignored.
        store.saveAccount ({ "tok", "a@b.c", false, t0 });
        file.replaceWithText (file.loadFileAsString().replace ("\"owned\": false", "\"owned\": true"));
        CHECK (! store.loadAccount().owned);

        store.clearAccount();
        CHECK (! store.loadAccount().signedIn());
    }
    {   // trial anchors
        FakeAnchors fa;
        LicenseStore store (file, fa.make (3), "machine-A");
        CHECK (! store.loadTrial().started());

        store.saveTrial ({ t0, t0 + 100 });
        CHECK (store.loadTrial().start == t0 && store.loadTrial().lastSeen == t0 + 100);

        // Deleting some anchors doesn't reset the trial ...
        fa.slots[0].clear(); fa.slots[1].clear();
        CHECK (store.loadTrial().start == t0);
        // ... and neither does corrupting one while another is intact.
        fa.slots[0] = "garbage";
        CHECK (store.loadTrial().start == t0);

        // Editing the start date breaks the record; with nothing valid left the trial is used up.
        fa.slots[2] = fa.slots[2].replace (juce::String (t0), juce::String (t0 + 999 * kDay));
        CHECK (store.loadTrial().started());
        CHECK (evaluate ({}, store.loadTrial(), t0).kind == Kind::expired);

        // A record from another machine is not accepted either.
        FakeAnchors fb;
        LicenseStore (file, fb.make (1), "machine-B").saveTrial ({ t0 + 5 * kDay, t0 + 5 * kDay });
        LicenseStore mine (file, fb.make (1), "machine-A");
        CHECK (evaluate ({}, mine.loadTrial(), t0).kind == Kind::expired);
    }
    dir.deleteRecursively();
}

static void testLive()
{
    AccountClient client;
    std::cout << "live: " << AccountClient::productPageUrl() << "\n";

    CHECK (client.checkLicense ("not-a-real-token").state == AccountClient::Ownership::invalidToken);

    const auto start = client.startLogin ("whysho EQ1 self-test");
    CHECK (start.ok);
    std::cout << "live: login started, code " << start.userCode << ", interval " << start.intervalSeconds
              << "s, expires " << start.expiresInSeconds << "s\n";
    CHECK (client.pollLogin (start.deviceCode).state == AccountClient::Poll::pending);
    CHECK (client.pollLogin ("bogus-device-code").state == AccountClient::Poll::expired);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    testVerdicts();
    testStore();
    runDspTests();
    runWorkflowTests();
    if (argc > 1 && juce::String (argv[1]) == "--live") testLive();
    std::cout << (failures == 0 ? "all tests passed\n" : "tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
