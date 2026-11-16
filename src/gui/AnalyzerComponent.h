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
    bool refresh();     // new spectrum frames + curves; true if the display changed

    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    int   bandAtPixel (juce::Point<float>) const;
    void  setBandFreqGain (int band, float hz, float db);
    void  setBandParam (int band, const char* suffix, float value01);
    void  setBandValue (int band, const char* suffix, float realValue);   // one-shot, wrapped in a gesture
    void  gesture (int band, const char* suffix, bool begin);
    void  createBandAt (juce::Point<float>);
    float getParam (const juce::String& id) const;

    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&, const dsp::SpectrumAnalyzer&, juce::Colour);
    void drawCurve (juce::Graphics&);
    void drawNodes (juce::Graphics&);
    void drawRangeControl (juce::Graphics&);

    // The curves are rebuilt from the current parameters (so they track the knobs
    // whether or not audio is running), but only when something they depend on
    // has changed. Returns true if anything was rebuilt.
    bool  updateCurves();
    float curveDb (float hz) const;
    juce::Path curvePath (const std::vector<float>& db) const;

    // Where a band's node sits: its gain as the engine applies it (gain x Scale),
    // or 0 dB for the filter types that have no gain.
    float nodeGainDb (int band) const;
    juce::Point<float> nodePosition (int band) const;
    float gainDragScale() const;        // dB on the graph per dB of the gain parameter

    // Display range (+/- dB), saved with the project as the "dbRange" state property.
    void setDbRange (int range);
    void syncDbRange();
    juce::Rectangle<int> rangeButtonBounds (int index) const;

    WhyshoEQAudioProcessor& proc;

    // Raw parameter values, looked up once.
    struct BandRefs
    {
        std::atomic<float> *on {}, *type {}, *freq {}, *gain {}, *q {}, *slope {}, *dyn {}, *dynRange {};
        juce::RangedAudioParameter *freqParam {}, *gainParam {};
    };
    std::array<BandRefs, kNumBands> bandRefs;
    std::atomic<float> *gainScaleRef {}, *analyzerRef {};

    std::array<dsp::EqBand, kNumBands> displayBands;
    std::array<dsp::BandParams, kNumBands> displayParams;
    double displaySR = 0.0;

    // What the cached curves were built from.
    std::array<float, kNumBands * 6 + 4> staticKey {};
    std::array<float, kNumBands * 3> liveKey {};
    std::array<float, kNumBands> liveAmount {};      // dynamics engagement shown, 0..1
    std::vector<float> staticDb, liveDb;             // one value per pixel column
    juce::Path staticPath, livePath, liveFill;
    bool liveDiffers = false;                        // dynamics are moving the curve right now

    int selected = 0;
    int dragging = -1;
    juce::Point<float> dragStart;        // mouse position, and the node's frequency (as x)
    float dragStartX = 0.0f, dragStartDb = 0.0f;     // and gain when the drag began

    int lastSource = -1;
    float rangeDb = 12.0f;

    static constexpr float minHz = dsp::SpectrumAnalyzer::minHz, maxHz = dsp::SpectrumAnalyzer::maxHz;
    static constexpr float gainLimitDb = 30.0f;                 // range of the band gain parameter
    static constexpr float specMinDb = -90.0f, specMaxDb = 0.0f; // spectrum scale, bottom .. top of the graph
    static constexpr std::array<int, 3> dbRanges { 6, 12, 30 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerComponent)
};
} // namespace wsho::gui
