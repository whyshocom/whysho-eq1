#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace wsho::lic
{
// Platform anti-debug probe (deterrent only; never auto-blocks legit DAW use).
bool isDebuggerAttached();

// ---------------------------------------------------------------------------
//  Audio-path license gate.
//  The "licensed" flag is stored redundantly and obfuscated (two atomics that
//  must agree), so a cracker can't flip one boolean. When unlicensed, audio is
//  periodically degraded (brief mutes + low noise) at pseudo-random intervals
//  — enough to audition, not to ship. This is a deterrent, not DRM magic.
// ---------------------------------------------------------------------------
class Guard
{
public:
    Guard();
    void prepare (double sampleRate);
    void setLicensed (bool licensed) noexcept;
    bool isLicensed() const noexcept;

    // Call once per audio block (post-processing). No-op when licensed.
    void applyDegradation (juce::AudioBuffer<float>& buffer) noexcept;

private:
    static constexpr uint32_t kSalt = 0x9E3779B9u;
    std::atomic<uint32_t> tokenA { 0 }, tokenB { 0 };
    double fs = 44100.0;
    int    samplesUntilEvent = 0;
    int    muteRemaining = 0;
    juce::Random rng;
};

} // namespace wsho::lic
