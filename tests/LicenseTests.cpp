// Test runner. Licensing rules here (trial clock, offline grace, tamper handling);
// DSP stress tests in DspTests.cpp.
//   WhyshoEQ1_Tests          unit tests, no network
//   WhyshoEQ1_Tests --live   also talks to the real whysho.com plugin API
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "licensing/AccountClient.h"
#include "licensing/LicenseStore.h"
#include <iostream>
#include <map>

extern "C" {
#include "monocypher-ed25519.h"
}

using namespace wsho::lic;
using Kind = Verdict::Kind;

int failures = 0;
void runDspTests();
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

// A stand-in for whysho.com: signs license answers with a throwaway test key.
struct TestSite
{
    uint8_t secret[64] {};
    PublicKey pub {};

    explicit TestSite (uint8_t fill)
    {
        uint8_t seed[32];
        std::fill (std::begin (seed), std::end (seed), fill);
        crypto_ed25519_key_pair (secret, pub.data(), seed);
    }

    juce::String sign (const juce::String& payload) const
    {
        uint8_t sig[64];
        crypto_ed25519_sign (sig, secret, (const uint8_t*) payload.toRawUTF8(), payload.getNumBytesAsUTF8());
        return base64UrlEncode (payload.toRawUTF8(), payload.getNumBytesAsUTF8()) + "." + base64UrlEncode (sig, 64);
    }

    juce::String license (bool owned, const juce::String& machine, juce::int64 issuedAt,
                          const juce::String& nonce = "nonce-0123456789abcdef", const juce::String& product = "eq1") const
    {
        return sign ("{\"v\":1,\"product\":\"" + product + "\",\"owned\":" + (owned ? "true" : "false")
                     + ",\"email\":\"a@b.c\",\"machine\":\"" + machine + "\",\"nonce\":\"" + nonce
                     + "\",\"issuedAt\":" + juce::String (issuedAt) + ",\"expiresAt\":" + juce::String (issuedAt + 30 * kDay) + "}");
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

    // Owner: unlocked regardless of the trial, until the site's answer expires.
    Account owner;
    owner.token = "tok"; owner.owned = true; owner.checkedAt = t0 + 20 * kDay; owner.validUntil = t0 + 50 * kDay;
    CHECK (evaluate (owner, seen, t0 + 21 * kDay).kind == Kind::licensed);
    CHECK (evaluate (owner, seen, t0 + 49 * kDay).kind == Kind::licensed);
    CHECK (evaluate (owner, seen, t0 + 51 * kDay).kind == Kind::expired);    // answer expired, trial long gone

    // The server ended the session: the owner keeps 7 days, not the full 30.
    Account signedOut = owner;
    signedOut.token.clear(); signedOut.signedOutAt = t0 + 22 * kDay;
    CHECK (evaluate (signedOut, seen, t0 + 28 * kDay).kind == Kind::licensed);
    CHECK (evaluate (signedOut, seen, t0 + 30 * kDay).kind == Kind::expired);

    // Signed in but the account doesn't own it: back to the trial rules.
    Account notOwner;
    notOwner.token = "tok"; notOwner.checkedAt = t0; notOwner.validUntil = t0 + 30 * kDay;
    CHECK (evaluate (notOwner, tr, t0 + kDay).kind == Kind::trial);
    CHECK (evaluate (notOwner, tr, t0 + 15 * kDay).kind == Kind::expired);
}

static void testSignedLicense()
{
    const TestSite site (7), impostor (9);
    const juce::int64 t0 = 1'800'000'000;
    const juce::String machine = "machine-A-0123456789";

    // A genuine answer for this machine.
    const auto good = site.license (true, machine, t0);
    auto lic = verifyLicenseToken (good, machine, site.pub);
    CHECK (lic.valid && lic.owned && lic.email == "a@b.c" && lic.issuedAt == t0 && lic.expiresAt == t0 + 30 * kDay);
    CHECK (verifyLicenseToken (site.license (false, machine, t0), machine, site.pub).valid);
    CHECK (! verifyLicenseToken (site.license (false, machine, t0), machine, site.pub).owned);

    // A fake server (any other key) can't produce one.
    CHECK (! verifyLicenseToken (impostor.license (true, machine, t0), machine, site.pub).valid);

    // Flipping "owned" in a genuine "not owned" answer breaks the signature.
    const auto notOwned = site.license (false, machine, t0);
    const auto payload = base64UrlDecode (notOwned.upToFirstOccurrenceOf (".", false, false)).toString().replace ("false", "true");
    const auto forged = base64UrlEncode (payload.toRawUTF8(), payload.getNumBytesAsUTF8()) + notOwned.fromFirstOccurrenceOf (".", true, false);
    CHECK (! verifyLicenseToken (forged, machine, site.pub).valid);

    // Someone else's genuine answer (another computer, another product) is refused too.
    CHECK (! verifyLicenseToken (good, "machine-B-0123456789", site.pub).valid);
    CHECK (! verifyLicenseToken (site.license (true, machine, t0, "n", "sy1"), machine, site.pub).valid);

    // The fixed vector from the website's own tests (lib/license-token.test.ts in the
    // whysho.com repo): proves the site's Node signer and this verifier agree.
    {
        const PublicKey sitePub = { 0x59,0x6d,0x86,0xb3,0x28,0xfb,0x6d,0xf9,0xfd,0xf0,0x67,0xec,0x96,0xe9,0x19,0xba,
                                    0x24,0xda,0x10,0xac,0x43,0xcf,0x3e,0x34,0xbd,0x45,0xec,0x38,0x82,0xd5,0xa6,0x4e };
        const auto v = verifyLicenseToken (
            "eyJ2IjoxLCJwcm9kdWN0IjoiZXExIiwib3duZWQiOnRydWUsImVtYWlsIjoiYUBiLmMiLCJtYWNoaW5lIjoiYldGamFHbHVaUzFwWkMxb1lYTm9MVEF3TURFIiwi"
            "bm9uY2UiOiJibTl1WTJVdE1EQXdNUzFoWW1Oa1pXWSIsImlzc3VlZEF0IjoxODAwMDAwMDAwLCJleHBpcmVzQXQiOjE4MDI1OTIwMDB9"
            ".0ltq82hhI7lFHfabe8YIh2yDV5J-zKvhX6rLpjpRukQt3-VBbVL5eP_gbCeBKBQHcXSfmPNEVZoos0n0wYclBQ",
            "bWFjaGluZS1pZC1oYXNoLTAwMDE", sitePub);
        CHECK (v.valid && v.owned && v.nonce == "bm9uY2UtMDAwMS1hYmNkZWY" && v.issuedAt == 1800000000 && v.expiresAt == 1802592000);
    }

    // Garbage in, nothing out.
    for (auto* junk : { "", ".", "abc", "abc.def", "e30.AAAA" })
        CHECK (! verifyLicenseToken (junk, machine, site.pub).valid);
}

static void testStore()
{
    auto dir = juce::File::createTempFile ("wsho-test");
    dir.createDirectory();
    const auto file = dir.getChildFile ("account.json");
    const juce::int64 t0 = 1'800'000'000;
    const TestSite site (7);

    {   // account round trip
        FakeAnchors fa;
        LicenseStore store (file, fa.make (2), "machine-A", site.pub);
        CHECK (! store.loadAccount().signedIn());

        Account a;
        a.token = "tok";
        CHECK (store.acceptLicense (a, site.license (true, "machine-A", t0, "fresh-nonce"), "fresh-nonce"));
        CHECK (! store.acceptLicense (a, site.license (true, "machine-A", t0, "old-nonce"), "fresh-nonce"));   // a replayed answer
        store.saveAccount (a);

        const auto b = store.loadAccount();
        CHECK (b.token == "tok" && b.email == "a@b.c" && b.owned && b.checkedAt == t0 && b.validUntil == t0 + 30 * kDay);

        // Copied to another computer: still signed in, but must re-check online.
        LicenseStore other (file, fa.make (2), "machine-B", site.pub);
        CHECK (other.loadAccount().signedIn() && ! other.loadAccount().owned);

        // Hand-editing the file can't grant ownership: there is no "owned" to edit,
        // only the signed answer.
        Account c;
        c.token = "tok";
        store.acceptLicense (c, site.license (false, "machine-A", t0));
        store.saveAccount (c);
        file.replaceWithText (file.loadFileAsString().replace ("\"token\"", "\"owned\": true, \"token\""));
        CHECK (! store.loadAccount().owned);

        store.clearAccount();
        CHECK (! store.loadAccount().signedIn());
    }
    {   // trial anchors
        FakeAnchors fa;
        LicenseStore store (file, fa.make (3), "machine-A", site.pub);
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
        LicenseStore (file, fb.make (1), "machine-B", site.pub).saveTrial ({ t0 + 5 * kDay, t0 + 5 * kDay });
        LicenseStore mine (file, fb.make (1), "machine-A", site.pub);
        CHECK (evaluate ({}, mine.loadTrial(), t0).kind == Kind::expired);
    }
    dir.deleteRecursively();
}

static void testLive()
{
    AccountClient client;
    std::cout << "live: " << AccountClient::productPageUrl() << "\n";

    CHECK (client.checkLicense ("not-a-real-token", "0123456789abcdef0123456789abcdef", "0123456789abcdef").state == AccountClient::Answer::invalidToken);

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
    testSignedLicense();
    testStore();
    runDspTests();
    if (argc > 1 && juce::String (argv[1]) == "--live") testLive();
    std::cout << (failures == 0 ? "all tests passed\n" : "tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
