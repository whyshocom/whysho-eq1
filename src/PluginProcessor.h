#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/EqEngine.h"
#include "dsp/SpectrumAnalyzer.h"
#include "licensing/License.h"
#include "licensing/Activation.h"
#include "licensing/AntiTamper.h"
#include "PresetManager.h"

namespace wsho
{
class WhyshoEQAudioProcessor : public juce::AudioProcessor
{
public:
    WhyshoEQAudioProcessor();
    ~WhyshoEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "whysho EQ1"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    // --- accessors for the editor ---
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    dsp::EqEngine& engine() { return eqEngine; }
    dsp::SpectrumAnalyzer& preAnalyzer()  { return analyzerPre; }
    dsp::SpectrumAnalyzer& postAnalyzer() { return analyzerPost; }
    dsp::SpectrumAnalyzer& scAnalyzer()   { return analyzerSC; }

    lic::LicenseManager& licenseManager() { return licMgr; }
    lic::ActivationClient& activationClient() { return activation; }
    PresetManager& presetManager() { return presets; }
    void refreshLicense();
    lic::LicenseInfo licenseInfo;

    bool sidechainActive() const { return scActive; }

private:
    dsp::EngineParams buildSnapshot();
    void cacheParamPointers();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    dsp::EqEngine eqEngine;
    dsp::SpectrumAnalyzer analyzerPre, analyzerPost, analyzerSC;

    lic::LicenseManager   licMgr;
    lic::ActivationClient activation;
    lic::Guard            guard;

    // cached raw parameter atomics
    struct BandPtrs {
        std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *chan, *solo,
                           *dyn, *dthr, *drng, *datt, *drel, *dsc;
    };
    std::array<BandPtrs, kNumBands> bandPtrs {};
    std::atomic<float> *pInGain{}, *pOutGain{}, *pBypass{}, *pMix{}, *pPhase{},
                       *pOs{}, *pAutoGain{}, *pAnalyzer{}, *pScListen{}, *pGainScale{};

    juce::AudioBuffer<float> sidechainCopy;
    bool scActive = false;
    int  blockCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoEQAudioProcessor)
};
} // namespace wsho
