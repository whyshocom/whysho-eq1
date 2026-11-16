#include "LicenseToken.h"
#include "ObfString.h"

extern "C" {
#include "monocypher-ed25519.h"
}

namespace wsho::lic
{
juce::MemoryBlock base64UrlDecode (const juce::String& in)
{
    juce::String s = in.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s << '=';
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, s)) return {};
    return out.getMemoryBlock();
}

juce::String base64UrlEncode (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size)
             .replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

SignedLicense verifyLicenseToken (const juce::String& token, const juce::String& machine, const PublicKey& key)
{
    SignedLicense out;

    const int dot = token.indexOfChar ('.');
    if (dot <= 0 || token.length() > 4096) return out;

    const auto payload   = base64UrlDecode (token.substring (0, dot));
    const auto signature = base64UrlDecode (token.substring (dot + 1));
    if (payload.isEmpty() || signature.getSize() != 64) return out;

    // Ed25519 (RFC 8032) over the exact payload bytes. Checked before anything is parsed.
    if (crypto_ed25519_check ((const uint8_t*) signature.getData(), key.data(),
                              (const uint8_t*) payload.getData(), payload.getSize()) != 0)
        return out;

    const auto json = juce::JSON::parse (payload.toString());
    const auto owned = json.getProperty ("owned", juce::var());

    // A genuine answer, but is it for this product on this computer?
    if ((int) json.getProperty ("v", 0) != 1
        || json.getProperty ("product", {}).toString() != WSHO_JS ("eq1")
        || machine.isEmpty()
        || json.getProperty ("machine", {}).toString() != machine
        || ! owned.isBool())
        return out;

    out.owned     = (bool) owned;
    out.email     = json.getProperty ("email", {}).toString();
    out.nonce     = json.getProperty ("nonce", {}).toString();
    out.issuedAt  = (juce::int64) json.getProperty ("issuedAt", 0);
    out.expiresAt = (juce::int64) json.getProperty ("expiresAt", 0);
    out.valid     = out.issuedAt > 0 && out.expiresAt > out.issuedAt;
    return out;
}
} // namespace wsho::lic
