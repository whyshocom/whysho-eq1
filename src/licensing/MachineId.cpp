#include "MachineId.h"
#include <juce_cryptography/juce_cryptography.h>

namespace wsho::lic
{
juce::String getMachineId()
{
    juce::String seed;

    // JUCE's stable per-device id (OS-level, survives reinstalls where possible).
    auto ids = juce::SystemStats::getDeviceIdentifiers();
    for (auto& s : ids) seed << s << "|";

    seed << juce::SystemStats::getUniqueDeviceID() << "|";
    seed << juce::SystemStats::getOperatingSystemName() << "|";
    // A product-specific salt so the same machine yields different ids per app
    // (prevents cross-product license correlation / reuse).
    seed << "whysho-eq1-salt-v1";

    return juce::SHA256 (seed.toRawUTF8(), (size_t) seed.getNumBytesAsUTF8()).toHexString();
}
}
