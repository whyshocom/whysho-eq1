#include "License.h"
#include "LicensePublicKey.h"
#include "MachineId.h"
#include "ObfString.h"
#include <juce_cryptography/juce_cryptography.h>

extern "C" {
#include "monocypher-ed25519.h"
}

namespace wsho::lic
{
// --- base64url ------------------------------------------------------------
juce::MemoryBlock base64UrlDecode (const juce::String& in)
{
    juce::String s = in.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s << '=';
    juce::MemoryOutputStream os;
    juce::Base64::convertFromBase64 (os, s);
    return os.getMemoryBlock();
}

juce::String base64UrlEncode (const juce::MemoryBlock& in)
{
    auto s = juce::Base64::toBase64 (in.getData(), in.getSize());
    return s.replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

// --------------------------------------------------------------------------
LicenseManager::LicenseManager() = default;

juce::File LicenseManager::licenseFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Whysho").getChildFile ("EQ1").getChildFile ("license.wslic");
}

juce::File LicenseManager::trialFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Whysho").getChildFile ("EQ1").getChildFile (".wstr");
}

bool LicenseManager::saveToken (const juce::String& token)
{
    auto f = licenseFile();
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (token);
}

juce::String LicenseManager::loadToken() const
{
    auto f = licenseFile();
    return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
}

void LicenseManager::clearToken() { licenseFile().deleteFile(); }

LicenseInfo LicenseManager::verifyToken (const juce::String& token) const
{
    LicenseInfo out;

    if (! kLicensePublicKeyIsReal)
    {
        out.state = LicenseState::Unlicensed;
        out.reason = "No production key embedded (dev build).";
        return out;
    }

    auto dot = token.indexOfChar ('.');
    if (dot <= 0) { out.reason = "Malformed token."; return out; }

    const juce::String payloadB64 = token.substring (0, dot);
    const juce::String sigB64     = token.substring (dot + 1);

    juce::MemoryBlock payload = base64UrlDecode (payloadB64);
    juce::MemoryBlock sig     = base64UrlDecode (sigB64);
    if (sig.getSize() != 64 || payload.getSize() == 0) { out.reason = "Bad token fields."; return out; }

    // Ed25519 (RFC 8032, SHA-512) verification over the exact payload bytes.
    const int ok = crypto_ed25519_check ((const uint8_t*) sig.getData(),
                                         kLicensePublicKey,
                                         (const uint8_t*) payload.getData(),
                                         payload.getSize());
    if (ok != 0) { out.state = LicenseState::Tampered; out.reason = "Signature invalid."; return out; }

    auto json = juce::JSON::parse (payload.toString());
    if (! json.isObject()) { out.reason = "Payload not JSON."; return out; }

    out.email       = json.getProperty ("email", "").toString();
    out.edition     = json.getProperty ("edition", "standard").toString();
    out.licenseId   = json.getProperty ("licenseId", "").toString();
    out.activationId= json.getProperty ("activationId", "").toString();
    out.issuedAt    = (juce::int64) (double) json.getProperty ("issuedAt", 0.0);
    out.expiresAt   = (juce::int64) (double) json.getProperty ("expiresAt", 0.0);

    const juce::String product = json.getProperty ("product", "").toString();
    if (product != WSHO_JS ("whysho-eq1")) { out.reason = "Wrong product."; return out; }

    if (auto* arr = json.getProperty ("features", juce::var()).getArray())
        for (auto& v : *arr) out.features.add (v.toString());

    const juce::String boundMachine = json.getProperty ("machineId", "").toString();
    if (boundMachine != getMachineId()) { out.state = LicenseState::MachineMismatch; out.reason = "License bound to another machine."; return out; }

    const auto now = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    if (out.expiresAt != 0 && now > out.expiresAt) { out.state = LicenseState::Expired; out.reason = "License expired."; return out; }

    out.state = LicenseState::Licensed;
    out.reason = "OK";
    return out;
}

LicenseInfo LicenseManager::refresh()
{
    const auto token = loadToken();
    if (token.isNotEmpty())
    {
        info = verifyToken (token);
        if (info.state == LicenseState::Licensed) return info;
    }
    info = startOrCheckTrial();
    return info;
}

// Tamper-resistant local trial: store machine-bound, signed-by-HMAC start date.
LicenseInfo LicenseManager::startOrCheckTrial (int days)
{
    LicenseInfo out;
    auto f = trialFile();
    const juce::String machine = getMachineId();

    auto hmac = [&] (const juce::String& body)
    {
        return juce::SHA256 ((body + "|" + machine + "|" + WSHO_JS ("wsho-trial-hmac-v1")).toRawUTF8(),
                             (size_t) (body + "|" + machine + "|" + WSHO_JS ("wsho-trial-hmac-v1")).getNumBytesAsUTF8())
                 .toHexString();
    };

    juce::int64 start = 0;
    if (f.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        if (lines.size() >= 2 && lines[1] == hmac (lines[0]))
            start = lines[0].getLargeIntValue();
        else { out.state = LicenseState::Tampered; out.reason = "Trial data tampered."; return out; }
    }

    const auto now = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    if (start == 0)
    {
        start = now;
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::String (start) + "\n" + hmac (juce::String (start)));
    }

    const juce::int64 end = start + (juce::int64) days * 86400;
    if (now > end) { out.state = LicenseState::Expired; out.reason = "Trial expired."; out.expiresAt = end; return out; }

    out.state = LicenseState::Trial;
    out.edition = "trial";
    out.expiresAt = end;
    out.features = juce::StringArray { "dynamic-eq", "linear-phase", "mid-side", "oversampling", "sidechain" };
    out.reason = "Trial active";
    return out;
}

} // namespace wsho::lic
