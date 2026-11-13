#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace wsho
{
WhyshoEQAudioProcessor::WhyshoEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "WHYSHO_EQ1", createParameterLayout())
{
    cacheParamPointers();
    refreshLicense();
}

void WhyshoEQAudioProcessor::cacheParamPointers()
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& b = bandPtrs[(size_t) i];
        b.on    = get (pid::band (i, pid::on));
        b.type  = get (pid::band (i, pid::type));
        b.freq  = get (pid::band (i, pid::freq));
        b.gain  = get (pid::band (i, pid::gain));
        b.q     = get (pid::band (i, pid::q));
        b.slope = get (pid::band (i, pid::slope));
        b.chan  = get (pid::band (i, pid::chan));
        b.solo  = get (pid::band (i, pid::solo));
        b.dyn   = get (pid::band (i, pid::dyn));
        b.dthr  = get (pid::band (i, pid::dynThresh));
        b.drng  = get (pid::band (i, pid::dynRange));
        b.datt  = get (pid::band (i, pid::dynAttack));
        b.drel  = get (pid::band (i, pid::dynRelease));
        b.dsc   = get (pid::band (i, pid::dynSC));
    }
    pInGain   = get (pid::inGain);
    pOutGain  = get (pid::outGain);
    pBypass   = get (pid::bypass);
    pMix      = get (pid::mix);
    pPhase    = get (pid::phase);
    pOs       = get (pid::os);
    pAutoGain = get (pid::autoGain);
    pAnalyzer = get (pid::analyzer);
    pScListen = get (pid::scListen);
    pGainScale= get (pid::gainScale);
}

void WhyshoEQAudioProcessor::refreshLicense()
{
    licenseInfo = licMgr.refresh();
    guard.setLicensed (licenseInfo.isUsable());
}

void WhyshoEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    eqEngine.prepare (sampleRate, samplesPerBlock, 2);
    analyzerPre.prepare (sampleRate);
    analyzerPost.prepare (sampleRate);
    analyzerSC.prepare (sampleRate);
    guard.prepare (sampleRate);
    sidechainCopy.setSize (2, samplesPerBlock);
    setLatencySamples (eqEngine.getLatencySamples());
}

bool WhyshoEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getChannelSet (false, 0);
    const auto& mainIn  = layouts.getChannelSet (true, 0);
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

dsp::EngineParams WhyshoEQAudioProcessor::buildSnapshot()
{
    dsp::EngineParams s;
    bool anySolo = false;
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& p = bandPtrs[(size_t) i];
        auto& bp = s.bands[(size_t) i];
        bp.on     = p.on->load()   > 0.5f;
        bp.type   = (FilterType) (int) p.type->load();
        bp.freq   = p.freq->load();
        bp.gainDb = p.gain->load();
        bp.q      = p.q->load();
        bp.order  = kSlopeOrders[(size_t) juce::jlimit (0, 7, (int) p.slope->load())];
        bp.chan   = (ChannelMode) (int) p.chan->load();
        bp.solo   = p.solo->load() > 0.5f;
        bp.dyn        = p.dyn->load() > 0.5f;
        bp.dynThreshDb= p.dthr->load();
        bp.dynRangeDb = p.drng->load();
        bp.dynAttackMs= p.datt->load();
        bp.dynReleaseMs=p.drel->load();
        bp.dynSidechain=p.dsc->load() > 0.5f;
        anySolo = anySolo || bp.solo;
    }
    s.anySolo   = anySolo;
    s.inGainDb  = pInGain->load();
    s.outGainDb = pOutGain->load();
    s.bypass    = pBypass->load() > 0.5f;
    s.mix       = pMix->load();
    s.phase     = (PhaseMode) (int) pPhase->load();
    s.osFactor  = (int) pOs->load();
    s.autoGain  = pAutoGain->load() > 0.5f;
    s.scListen  = pScListen->load() > 0.5f;
    s.gainScale = pGainScale->load();
    return s;
}

void WhyshoEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // ---- sidechain extraction ----
    scActive = false;
    sidechainCopy.clear();
    if (getBusCount (true) > 1)
    {
        auto scBus = getBus (true, 1);
        if (scBus != nullptr && scBus->isEnabled())
        {
            auto scBuf = getBusBuffer (buffer, true, 1);
            const int scCh = juce::jmin (2, scBuf.getNumChannels());
            const int n = juce::jmin (sidechainCopy.getNumSamples(), scBuf.getNumSamples());
            if (scCh > 0 && n > 0)
            {
                for (int c = 0; c < 2; ++c)
                    sidechainCopy.copyFrom (c, 0, scBuf, juce::jmin (c, scCh - 1), 0, n);
                scActive = true;
            }
        }
    }

    auto mainBuf = getBusBuffer (buffer, false, 0);

    // ---- analyzer: pre tap ----
    const int analyzerSrc = (int) pAnalyzer->load();
    if (analyzerSrc == 1 || analyzerSrc == 3)
        for (int i = 0; i < mainBuf.getNumSamples(); ++i)
            analyzerPre.push (0.5f * (mainBuf.getSample (0, i) + mainBuf.getSample (juce::jmin (1, mainBuf.getNumChannels()-1), i)));
    if (analyzerSrc == 4 && scActive)
        analyzerSC.pushBlock (sidechainCopy.getReadPointer (0), mainBuf.getNumSamples());

    // ---- DSP ----
    eqEngine.setParameters (buildSnapshot());
    setLatencySamples (eqEngine.getLatencySamples());
    eqEngine.process (mainBuf, scActive ? sidechainCopy : juce::AudioBuffer<float>());

    // ---- licensing gate (deterrent degradation when unlicensed) ----
    guard.applyDegradation (mainBuf);

    // ---- analyzer: post tap ----
    if (analyzerSrc == 2 || analyzerSrc == 3)
        for (int i = 0; i < mainBuf.getNumSamples(); ++i)
            analyzerPost.push (0.5f * (mainBuf.getSample (0, i) + mainBuf.getSample (juce::jmin (1, mainBuf.getNumChannels()-1), i)));

    // periodic re-check of license state (cheap, every ~500 blocks)
    if (++blockCounter >= 500) { blockCounter = 0; guard.setLicensed (licenseInfo.isUsable()); }
}

void WhyshoEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WhyshoEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* WhyshoEQAudioProcessor::createEditor()
{
    return new WhyshoEQAudioProcessorEditor (*this);
}
} // namespace wsho

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wsho::WhyshoEQAudioProcessor();
}
