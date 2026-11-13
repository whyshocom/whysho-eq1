#pragma once
#include <juce_core/juce_core.h>
#include "License.h"

namespace wsho::lic
{
// Talks to the Whysho license server to turn a human license key into a
// signed, machine-bound token. Network failures degrade gracefully to the
// offline/trial path (the plugin never hard-blocks on a dropped connection).
class ActivationClient
{
public:
    struct Result { bool ok = false; juce::String token, error; };

    // POST /api/activate { licenseKey, machineId, productId } -> { token }
    Result activate (const juce::String& licenseKey);

    // POST /api/deactivate { licenseKey, machineId }
    bool deactivate (const juce::String& licenseKey);

    // POST /api/validate { token } -> { valid, reason } (periodic re-check)
    bool validateOnline (const juce::String& token);

    void setServerOverride (const juce::String& url) { serverOverride = url; }

private:
    juce::String serverBase() const;
    juce::String serverOverride;
};
}
