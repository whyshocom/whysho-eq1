#pragma once
#include <juce_core/juce_core.h>

namespace wsho::lic
{
// Stable, privacy-preserving machine fingerprint (hex SHA-256). The raw
// hardware ids never leave the machine — only this hash is sent to the server
// and embedded (bound) into the signed license.
juce::String getMachineId();
}
