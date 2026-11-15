#include "AccountClient.h"
#include "ObfString.h"

namespace wsho::lic
{
namespace
{
juce::String siteBase()
{
   #if JUCE_DEBUG
    // Development only: point a debug build at a local `npm run dev` site.
    // Release builds ignore this, since the server's answers aren't signed.
    if (auto env = juce::SystemStats::getEnvironmentVariable ("WHYSHO_SITE", {}); env.isNotEmpty())
        return env.trimCharactersAtEnd ("/");
   #endif
    return WSHO_JS ("https://whysho.com");
}

juce::String productSlug() { return WSHO_JS ("eq1"); }

struct Response
{
    int status = -1;          // -1: no HTTP response at all
    juce::var body;

    bool unreachable() const       { return status < 0 || status >= 500; }
    juce::String error() const     { return body.getProperty ("error", {}).toString(); }
    juce::String text (const char* key) const { return body.getProperty (key, {}).toString(); }
};

Response request (const juce::String& path, const char* method,
                  const juce::String& jsonBody, const juce::String& bearer)
{
    juce::String headers = "Accept: application/json";
    if (jsonBody.isNotEmpty()) headers << "\r\nContent-Type: application/json";
    if (bearer.isNotEmpty())   headers << "\r\nAuthorization: Bearer " << bearer;

    juce::URL url (siteBase() + path);
    if (jsonBody.isNotEmpty()) url = url.withPOSTData (jsonBody);

    Response r;
    int status = 0;
    const auto options = juce::URL::InputStreamOptions (jsonBody.isNotEmpty() ? juce::URL::ParameterHandling::inPostData
                                                                               : juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (headers)
                             .withHttpRequestCmd (method)
                             .withConnectionTimeoutMs (5000)
                             .withNumRedirectsToFollow (0)
                             .withStatusCode (&status);

    if (auto stream = url.createInputStream (options))
    {
        r.status = status > 0 ? status : -1;
        // The API's answers are tiny; refuse to buffer anything unexpected.
        r.body = juce::JSON::parse (stream->readString().substring (0, 16 * 1024));
    }
    return r;
}

juce::String json (std::initializer_list<std::pair<const char*, juce::String>> fields)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    for (auto& f : fields) o->setProperty (f.first, f.second);
    return juce::JSON::toString (juce::var (o.get()), true);
}
} // namespace

AccountClient::LoginStart AccountClient::startLogin (const juce::String& deviceLabel) const
{
    LoginStart out;
    const auto r = request ("/api/plugin/login/start", "POST", json ({ { "device", deviceLabel } }), {});
    if (r.status != 200) return out;

    out.deviceCode = r.text ("device_code");
    out.userCode   = r.text ("user_code");
    out.expiresInSeconds = juce::jlimit (30, 3600, (int) r.body.getProperty ("expires_in", 600));
    out.intervalSeconds  = juce::jlimit (2, 60,    (int) r.body.getProperty ("interval", 5));
    out.ok = out.deviceCode.isNotEmpty() && out.userCode.isNotEmpty();
    return out;
}

AccountClient::PollResult AccountClient::pollLogin (const juce::String& deviceCode) const
{
    PollResult out;
    const auto r = request ("/api/plugin/login/poll", "POST", json ({ { "device_code", deviceCode } }), {});

    if (r.status == 200 && r.text ("token").isNotEmpty())
    {
        out.state = Poll::approved;
        out.token = r.text ("token");
        out.email = r.text ("email");
    }
    else if (r.status == 400 && r.error() == "authorization_pending") out.state = Poll::pending;
    else if (r.status == 400 && r.error() == "expired_token")         out.state = Poll::expired;
    return out;
}

AccountClient::LicenseResult AccountClient::checkLicense (const juce::String& token) const
{
    LicenseResult out;
    const auto r = request ("/api/plugin/license?product=" + productSlug(), "GET", {}, token);

    if (r.status == 401) { out.state = Ownership::invalidToken; return out; }

    // Anything that isn't a clear answer (offline, 5xx, a proxy's HTML page, ...)
    // counts as "unreachable", so a network hiccup never locks out an owner.
    const auto owned = r.body.getProperty ("owned", juce::var());
    if (r.status == 200 && owned.isBool() && r.text ("product") == productSlug())
    {
        out.state = (bool) owned ? Ownership::owned : Ownership::notOwned;
        out.email = r.text ("email");
    }
    return out;
}

void AccountClient::logout (const juce::String& token) const
{
    request ("/api/plugin/logout", "POST", "{}", token);
}

juce::String AccountClient::activationPageUrl (const juce::String& userCode)
{
    return siteBase() + "/activate?code=" + juce::URL::addEscapeChars (userCode, true);
}

juce::String AccountClient::productPageUrl()
{
    return siteBase() + "/plugins/" + productSlug();
}

} // namespace wsho::lic
