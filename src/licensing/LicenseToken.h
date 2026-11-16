#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace wsho::lic
{
using PublicKey = std::array<uint8_t, 32>;

// A license answer signed by whysho.com:  base64url(payload) "." base64url(signature)
// payload is JSON: { v, product, owned, email, machine, nonce, issuedAt, expiresAt }.
// The signature (Ed25519) covers the exact payload bytes, so nobody without the
// site's private key can produce an "owned" answer: not a fake server, and not
// someone editing the cached copy on disk.
struct SignedLicense
{
    bool valid = false;          // signature, product and machine all check out
    bool owned = false;
    juce::String email, nonce;
    juce::int64 issuedAt = 0;    // server time, unix seconds
    juce::int64 expiresAt = 0;   // how long this answer may be trusted offline
};

SignedLicense verifyLicenseToken (const juce::String& token, const juce::String& machine, const PublicKey& key);

juce::MemoryBlock base64UrlDecode (const juce::String&);
juce::String      base64UrlEncode (const void* data, size_t size);
} // namespace wsho::lic
