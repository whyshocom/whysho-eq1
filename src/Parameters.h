#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// ===========================================================================
//  Whysho EQ1 — parameter model
//  A single source of truth for every automatable parameter in the plugin.
// ===========================================================================
namespace wsho
{
static constexpr int kNumBands = 16;

// --- Enumerations (indices MUST stay stable: they are serialized) ----------
enum class FilterType
{
    Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass, TiltShelf, AllPass
};
static const juce::StringArray kFilterTypeNames
{ "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "Tilt Shelf", "All Pass" };

enum class ChannelMode { Stereo = 0, Left, Right, Mid, Side };
static const juce::StringArray kChannelModeNames { "Stereo", "Left", "Right", "Mid", "Side" };

// Slope in dB/oct; index -> order (number of 2nd-order sections doubling / 1st).
static const juce::StringArray kSlopeNames
{ "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
static constexpr std::array<int, 8> kSlopeOrders { 1, 2, 3, 4, 6, 8, 12, 16 }; // order = n poles

enum class PhaseMode { ZeroLatency = 0, Natural, Linear };
static const juce::StringArray kPhaseModeNames { "Zero Latency", "Natural Phase", "Linear Phase" };

static const juce::StringArray kOversampleNames { "Off", "2x", "4x", "8x", "16x" };

enum class AnalyzerSource { Off = 0, Pre, Post, Both, Sidechain };
static const juce::StringArray kAnalyzerSourceNames { "Off", "Pre", "Post", "Pre+Post", "Sidechain" };

// --- Parameter ID helpers --------------------------------------------------
namespace pid
{
    inline juce::String band (int i, const char* suffix) { return "b" + juce::String (i) + "_" + suffix; }

    // per-band suffixes
    constexpr const char* on    = "on";
    constexpr const char* type  = "type";
    constexpr const char* freq  = "freq";
    constexpr const char* gain  = "gain";
    constexpr const char* q     = "q";
    constexpr const char* slope = "slope";
    constexpr const char* chan  = "chan";
    constexpr const char* solo  = "solo";
    constexpr const char* dyn       = "dyn";      // dynamic EQ engaged
    constexpr const char* dynThresh = "dthr";     // dB
    constexpr const char* dynRange  = "drng";     // dB (+ boost / - cut target)
    constexpr const char* dynAttack = "datt";     // ms
    constexpr const char* dynRelease= "drel";     // ms
    constexpr const char* dynSC     = "dsc";      // key from external sidechain

    // global
    constexpr const char* inGain   = "in_gain";
    constexpr const char* outGain  = "out_gain";
    constexpr const char* bypass   = "bypass";
    constexpr const char* mix      = "mix";
    constexpr const char* phase    = "phase";
    constexpr const char* os       = "os";
    constexpr const char* autoGain = "auto_gain";
    constexpr const char* analyzer = "analyzer";
    constexpr const char* scListen = "sc_listen";
    constexpr const char* gainScale= "gain_scale"; // global EQ gain scaling 0..200%
}

// --- Default band layout (so a fresh instance already looks musical) -------
struct BandDefault { float freq; FilterType type; };
inline BandDefault defaultBand (int i)
{
    // 16 bands spread logarithmically 20 Hz .. 20 kHz; ends are shelves/cuts.
    const float f = 20.0f * std::pow (1000.0f, (float) i / (float) (kNumBands - 1));
    FilterType t = FilterType::Bell;
    if (i == 0)               t = FilterType::HighPass;
    else if (i == 1)          t = FilterType::LowShelf;
    else if (i == kNumBands-2)t = FilterType::HighShelf;
    else if (i == kNumBands-1)t = FilterType::LowPass;
    return { f, t };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace wsho
