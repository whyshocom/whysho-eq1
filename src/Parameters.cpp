#include "Parameters.h"

namespace wsho
{
using APVTS = juce::AudioProcessorValueTreeState;
using Range = juce::NormalisableRange<float>;

static Range freqRange()
{
    Range r (20.0f, 20000.0f, 0.1f);
    r.setSkewForCentre (1000.0f);
    return r;
}
static Range qRange()
{
    Range r (0.1f, 18.0f, 0.001f);
    r.setSkewForCentre (1.0f);
    return r;
}

APVTS::ParameterLayout createParameterLayout()
{
    APVTS::ParameterLayout layout;
    using FP = juce::AudioParameterFloat;
    using BP = juce::AudioParameterBool;
    using CP = juce::AudioParameterChoice;

    auto gainAttr = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto msAttr   = juce::AudioParameterFloatAttributes().withLabel ("ms");

    // ---- Global ----
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::inGain, 1 },  "Input Gain",  Range (-24.0f, 24.0f, 0.01f), 0.0f, gainAttr));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::outGain, 1 }, "Output Gain", Range (-24.0f, 24.0f, 0.01f), 0.0f, gainAttr));
    layout.add (std::make_unique<BP> (juce::ParameterID { pid::bypass, 1 },  "Bypass", false));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::mix, 1 },     "Mix", Range (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::phase, 1 },   "Phase Mode", kPhaseModeNames, 0));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::os, 1 },      "Oversampling", kOversampleNames, 0));
    layout.add (std::make_unique<BP> (juce::ParameterID { pid::autoGain, 1 },"Auto Gain", false));
    layout.add (std::make_unique<CP> (juce::ParameterID { pid::analyzer, 1 },"Analyzer", kAnalyzerSourceNames, 3));
    layout.add (std::make_unique<BP> (juce::ParameterID { pid::scListen, 1 },"Sidechain Listen", false));
    layout.add (std::make_unique<FP> (juce::ParameterID { pid::gainScale, 1 },"EQ Gain Scale", Range (0.0f, 2.0f, 0.01f), 1.0f));

    // ---- Per band ----
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto d = defaultBand (i);
        const juce::String n = "Band " + juce::String (i + 1) + " ";

        layout.add (std::make_unique<BP> (juce::ParameterID { pid::band (i, pid::on), 1 },   n + "On", i < 1));
        layout.add (std::make_unique<CP> (juce::ParameterID { pid::band (i, pid::type), 1 }, n + "Type", kFilterTypeNames, (int) d.type));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::freq), 1 }, n + "Freq", freqRange(), d.freq,
                     juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::gain), 1 }, n + "Gain", Range (-30.0f, 30.0f, 0.01f), 0.0f, gainAttr));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::q), 1 },    n + "Q", qRange(), 0.707f));
        layout.add (std::make_unique<CP> (juce::ParameterID { pid::band (i, pid::slope), 1 },n + "Slope", kSlopeNames, 1));
        layout.add (std::make_unique<CP> (juce::ParameterID { pid::band (i, pid::chan), 1 }, n + "Channel", kChannelModeNames, 0));
        layout.add (std::make_unique<BP> (juce::ParameterID { pid::band (i, pid::solo), 1 }, n + "Solo", false));

        layout.add (std::make_unique<BP> (juce::ParameterID { pid::band (i, pid::dyn), 1 },      n + "Dynamic", false));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::dynThresh), 1 },n + "Dyn Threshold", Range (-60.0f, 0.0f, 0.1f), -24.0f, gainAttr));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::dynRange), 1 }, n + "Dyn Range", Range (-24.0f, 24.0f, 0.1f), -6.0f, gainAttr));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::dynAttack), 1 },n + "Dyn Attack", Range (0.1f, 200.0f, 0.1f, 0.4f), 10.0f, msAttr));
        layout.add (std::make_unique<FP> (juce::ParameterID { pid::band (i, pid::dynRelease), 1 },n + "Dyn Release", Range (1.0f, 2000.0f, 1.0f, 0.4f), 120.0f, msAttr));
        layout.add (std::make_unique<BP> (juce::ParameterID { pid::band (i, pid::dynSC), 1 },    n + "Dyn Sidechain", false));
    }

    return layout;
}

} // namespace wsho
