#pragma once
#include <juce_core/juce_core.h>

namespace wsho::lic
{
// Hashed id for this computer (hex SHA-256). Used only locally, to bind the
// trial and license records to this machine; it is never sent anywhere.
juce::String getMachineId();
}
