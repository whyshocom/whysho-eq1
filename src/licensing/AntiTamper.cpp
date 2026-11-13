#include "AntiTamper.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <sys/sysctl.h>
 #include <unistd.h>
#elif JUCE_LINUX
 #include <sys/ptrace.h>
#endif

namespace wsho::lic
{
bool isDebuggerAttached()
{
#if JUCE_WINDOWS
    return IsDebuggerPresent() != 0;
#elif JUCE_MAC
    int mib[4] { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc proc {};
    size_t size = sizeof (proc);
    if (sysctl (mib, 4, &proc, &size, nullptr, 0) == 0)
        return (proc.kp_proc.p_flag & P_TRACED) != 0;
    return false;
#elif JUCE_LINUX
    // A second ptrace(TRACEME) fails if already traced.
    if (ptrace (PTRACE_TRACEME, 0, 1, 0) < 0) return true;
    ptrace (PTRACE_DETACH, 0, 1, 0);
    return false;
#else
    return false;
#endif
}

Guard::Guard() { setLicensed (false); }

void Guard::prepare (double sampleRate)
{
    fs = sampleRate > 0 ? sampleRate : 44100.0;
    samplesUntilEvent = (int) (fs * (3.0 + rng.nextFloat() * 3.0));
    muteRemaining = 0;
}

void Guard::setLicensed (bool licensed) noexcept
{
    const uint32_t a = licensed ? 0xA5A5C3C3u : 0x12345678u;
    tokenA.store (a, std::memory_order_release);
    tokenB.store (a ^ kSalt, std::memory_order_release);
}

bool Guard::isLicensed() const noexcept
{
    const uint32_t a = tokenA.load (std::memory_order_acquire);
    const uint32_t b = tokenB.load (std::memory_order_acquire);
    return (a == 0xA5A5C3C3u) && ((a ^ b) == kSalt);
}

void Guard::applyDegradation (juce::AudioBuffer<float>& buffer) noexcept
{
    if (isLicensed()) return;

    const int n = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();

    for (int i = 0; i < n; ++i)
    {
        if (muteRemaining <= 0 && --samplesUntilEvent <= 0)
        {
            muteRemaining = (int) (fs * 0.15);                       // ~150 ms event
            samplesUntilEvent = (int) (fs * (3.0 + rng.nextFloat() * 4.0));
        }
        if (muteRemaining > 0)
        {
            --muteRemaining;
            const float noise = (rng.nextFloat() * 2.0f - 1.0f) * 0.02f;
            for (int c = 0; c < ch; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                d[i] = d[i] * 0.0f + noise;                          // mute + faint noise
            }
        }
    }
}

} // namespace wsho::lic
