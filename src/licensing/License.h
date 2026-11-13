#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace wsho::lic
{
enum class LicenseState { Unlicensed, Trial, Licensed, Expired, MachineMismatch, Tampered };

struct LicenseInfo
{
    LicenseState state = LicenseState::Unlicensed;
    juce::String email, edition, licenseId, activationId;
    juce::StringArray features;
    juce::int64 issuedAt = 0, expiresAt = 0;
    juce::String reason;

    bool isUsable() const { return state == LicenseState::Licensed || state == LicenseState::Trial; }
    bool hasFeature (const juce::String& f) const { return features.contains (f); }
};

// Offline token verification + local persistence of the signed license.
class LicenseManager
{
public:
    LicenseManager();

    // Verify a signed token (base64url(payload).base64url(sig)) against the
    // embedded Ed25519 public key, product, machine binding and expiry.
    LicenseInfo verifyToken (const juce::String& token) const;

    // Persist / load the active token under the user's app data.
    bool   saveToken (const juce::String& token);
    juce::String loadToken() const;
    void   clearToken();

    // Load whatever is on disk and verify it.
    LicenseInfo refresh();

    // Start / check an N-day local trial (machine-bound, tamper-resistant file).
    LicenseInfo startOrCheckTrial (int days = 14);

    const LicenseInfo& current() const { return info; }
    juce::File licenseFile() const;
    juce::File trialFile() const;

private:
    LicenseInfo info;
};

// base64url helpers (no padding, -_ alphabet) shared with the server contract.
juce::MemoryBlock base64UrlDecode (const juce::String& in);
juce::String      base64UrlEncode (const juce::MemoryBlock& in);

} // namespace wsho::lic
