#include "Activation.h"
#include "MachineId.h"
#include "ObfString.h"

namespace wsho::lic
{
juce::String ActivationClient::serverBase() const
{
    if (serverOverride.isNotEmpty()) return serverOverride;
    // Obfuscated at rest; override via WHYSHO_LICENSE_SERVER env for self-host.
    if (auto env = juce::SystemStats::getEnvironmentVariable ("WHYSHO_LICENSE_SERVER", {}); env.isNotEmpty())
        return env;
    return WSHO_JS ("https://license.whysho.com");
}

static juce::var postJson (const juce::String& url, const juce::var& body, int& statusCode)
{
    const juce::String json = juce::JSON::toString (body, true);
    juce::URL u (url);
    u = u.withPOSTData (json);

    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                    .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json")
                    .withConnectionTimeoutMs (6000)
                    .withStatusCode (&statusCode);

    std::unique_ptr<juce::InputStream> stream (u.createInputStream (opts));
    if (stream == nullptr) { statusCode = -1; return {}; }
    return juce::JSON::parse (stream->readEntireStreamAsString());
}

ActivationClient::Result ActivationClient::activate (const juce::String& licenseKey)
{
    Result r;
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("licenseKey", licenseKey.trim());
    body->setProperty ("machineId", getMachineId());
    body->setProperty ("productId", juce::String (WSHO_JS ("whysho-eq1")));

    int status = 0;
    auto resp = postJson (serverBase() + WSHO_JS ("/api/activate"), juce::var (body.get()), status);

    if (status == -1) { r.error = "Could not reach the activation server. Check your connection."; return r; }
    if (status != 200)
    {
        r.error = resp.getProperty ("error", "Activation failed (HTTP " + juce::String (status) + ").").toString();
        return r;
    }
    r.token = resp.getProperty ("token", "").toString();
    r.ok = r.token.isNotEmpty();
    if (! r.ok) r.error = "Server did not return a license token.";
    return r;
}

bool ActivationClient::deactivate (const juce::String& licenseKey)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("licenseKey", licenseKey.trim());
    body->setProperty ("machineId", getMachineId());
    int status = 0;
    postJson (serverBase() + WSHO_JS ("/api/deactivate"), juce::var (body.get()), status);
    return status == 200;
}

bool ActivationClient::validateOnline (const juce::String& token)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("token", token);
    int status = 0;
    auto resp = postJson (serverBase() + WSHO_JS ("/api/validate"), juce::var (body.get()), status);
    if (status != 200) return true; // network issue -> don't punish the user (offline grace)
    return (bool) resp.getProperty ("valid", true);
}

} // namespace wsho::lic
