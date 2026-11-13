#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace wsho::gui
{
// The main interactive display: spectrum analyzer + EQ response curve +
// draggable per-band nodes. Emits onBandSelected when a node is clicked.
class AnalyzerComponent : public juce::Component, private juce::Timer
{
public:
    explicit AnalyzerComponent (WhyshoEQAudioProcessor& p);
    ~AnalyzerComponent() override;

    std::function<void (int)> onBandSelected;
    void setSelectedBand (int i) { selected = i; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    int   bandAtPixel (juce::Point<float>) const;
    void  setBandFreqGain (int band, float hz, float db);
    void  setBandParam (int band, const char* suffix, float value01);
    float getParam (const juce::String& id) const;

    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&, dsp::SpectrumAnalyzer&, juce::Colour, bool fill);
    void drawCurve (juce::Graphics&);
    void drawNodes (juce::Graphics&);

    // Display-only band set: recomputed from the current parameters every frame,
    // so the EQ curve tracks the knobs whether or not audio is running.
    void  updateDisplayBands();
    float curveDb (float hz) const;

    WhyshoEQAudioProcessor& proc;
    std::array<dsp::EqBand, kNumBands> displayBands;
    double displaySR = 0.0;
    int selected = 0;
    int dragging = -1;

    static constexpr float minHz = 20.0f, maxHz = 22000.0f;
    static constexpr float minDb = -30.0f, maxDb = 30.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerComponent)
};
} // namespace wsho::gui
