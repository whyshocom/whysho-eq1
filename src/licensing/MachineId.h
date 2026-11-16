#pragma once
#include <juce_core/juce_core.h>

namespace wsho::lic
{
// Anonymous hashed id for this computer (hex SHA-256 of a hardware id + salt).
// It binds the trial records to this machine, and is sent to whysho.com with a
// license check so the signed answer is only valid on this computer.
juce::String getMachineId();
}
