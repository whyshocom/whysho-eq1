#pragma once
#include <juce_core/juce_core.h>

namespace wsho::lic
{
// Thin client for the whysho.com plugin API (see whysho.com README, "Plugin
// activation API"). Licensing is account-based: the plugin signs in once, keeps
// the session token, and asks whether that account owns the product.
//
// Every call blocks on the network: never call from the audio or message thread.
class AccountClient
{
public:
    // POST /api/plugin/login/start
    struct LoginStart
    {
        bool ok = false;
        juce::String deviceCode, userCode;
        int expiresInSeconds = 600, intervalSeconds = 5;
    };
    LoginStart startLogin (const juce::String& deviceLabel) const;

    // POST /api/plugin/login/poll
    enum class Poll { pending, approved, expired, unreachable };
    struct PollResult { Poll state = Poll::unreachable; juce::String token, email; };
    PollResult pollLogin (const juce::String& deviceCode) const;

    // GET /api/plugin/license?product=eq1&machine=...&nonce=...
    // `answered` carries the site's signed answer; whether it says "owned" is only
    // known after its signature is verified (LicenseStore::acceptLicense).
    enum class Answer { answered, unsignedAnswer, invalidToken, unreachable };
    struct LicenseResult { Answer state = Answer::unreachable; juce::String license; };
    LicenseResult checkLicense (const juce::String& token, const juce::String& machine, const juce::String& nonce) const;

    // POST /api/plugin/logout (best effort)
    void logout (const juce::String& token) const;

    // Pages opened in the user's browser.
    static juce::String activationPageUrl (const juce::String& userCode);
    static juce::String productPageUrl();
};
} // namespace wsho::lic
