#include "MachineId.h"
#include <juce_cryptography/juce_cryptography.h>

namespace wsho::lic
{
// A stable, hashed id for this computer. It never leaves the machine: it only
// binds the local trial / license records so they can't be copied elsewhere.
// Computed once per process (the OS queries behind it aren't free).
juce::String getMachineId()
{
    static const juce::String id = []
    {
        // Motherboard/CPU-level id; unaffected by OS upgrades, disks or network adapters.
        juce::String seed = juce::SystemStats::getUniqueDeviceID();
        if (seed.isEmpty()) seed = juce::SystemStats::getComputerName();   // containers / odd Linux boxes
        seed << "|whysho-eq1-v2";
        return juce::SHA256 (seed.toRawUTF8(), seed.getNumBytesAsUTF8()).toHexString();
    }();
    return id;
}
}
