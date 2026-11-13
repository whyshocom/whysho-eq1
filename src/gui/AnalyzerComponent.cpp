#include "AnalyzerComponent.h"
#include "WhyshoLookAndFeel.h"

using namespace wsho;
using namespace wsho::gui;

AnalyzerComponent::AnalyzerComponent (WhyshoEQAudioProcessor& p) : proc (p)
{
    startTimerHz (30);
    setInterceptsMouseClicks (true, true);
}
AnalyzerComponent::~AnalyzerComponent() { stopTimer(); }

void AnalyzerComponent::timerCallback()
{
    proc.preAnalyzer().render();
    proc.postAnalyzer().render();
    proc.scAnalyzer().render();
    updateDisplayBands();
    repaint();
}

void AnalyzerComponent::updateDisplayBands()
{
    double sr = proc.engine().getSampleRate();
    if (sr <= 0.0) sr = 48000.0;
    if (sr != displaySR) { displaySR = sr; for (auto& b : displayBands) b.prepare (sr); }

    const float scale = getParam (pid::gainScale);
    for (int i = 0; i < kNumBands; ++i)
    {
        dsp::BandParams bp;
        bp.on     = getParam (pid::band (i, pid::on)) > 0.5f;
        bp.type   = (FilterType) (int) getParam (pid::band (i, pid::type));
        bp.freq   = getParam (pid::band (i, pid::freq));
        bp.gainDb = getParam (pid::band (i, pid::gain)) * scale;
        bp.q      = getParam (pid::band (i, pid::q));
        bp.order  = kSlopeOrders[(size_t) juce::jlimit (0, 7, (int) getParam (pid::band (i, pid::slope)))];
        bp.dyn    = false;   // static curve; dynamic movement shown via node colour
        displayBands[(size_t) i].updateCoefficients (bp);
    }
}

float AnalyzerComponent::curveDb (float hz) const
{
    const double sr = displaySR > 0.0 ? displaySR : 48000.0;
    const double w = 2.0 * M_PI * juce::jlimit (1.0, sr * 0.5 - 1.0, (double) hz) / sr;
    double mag = 1.0;
    for (auto& b : displayBands)
        if (b.isActive())
            mag *= std::abs (b.response (w));
    return (float) juce::Decibels::gainToDecibels (juce::jmax (1.0e-6, mag));
}

float AnalyzerComponent::freqToX (float hz) const
{
    const float t = std::log (hz / minHz) / std::log (maxHz / minHz);
    return juce::jmap (t, 0.0f, 1.0f, (float) 0.0f, (float) getWidth());
}
float AnalyzerComponent::xToFreq (float x) const
{
    const float t = juce::jmap (x, 0.0f, (float) getWidth(), 0.0f, 1.0f);
    return minHz * std::pow (maxHz / minHz, t);
}
float AnalyzerComponent::dbToY (float db) const
{
    return juce::jmap (db, maxDb, minDb, 0.0f, (float) getHeight());
}
float AnalyzerComponent::yToDb (float y) const
{
    return juce::jmap (y, 0.0f, (float) getHeight(), maxDb, minDb);
}

float AnalyzerComponent::getParam (const juce::String& id) const
{
    if (auto* v = proc.state().getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void AnalyzerComponent::setBandParam (int band, const char* suffix, float value01)
{
    if (auto* p = proc.state().getParameter (pid::band (band, suffix)))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
}

void AnalyzerComponent::setBandFreqGain (int band, float hz, float db)
{
    if (auto* fp = proc.state().getParameter (pid::band (band, pid::freq)))
        fp->setValueNotifyingHost (fp->convertTo0to1 (juce::jlimit (minHz, 20000.0f, hz)));
    if (auto* gp = proc.state().getParameter (pid::band (band, pid::gain)))
        gp->setValueNotifyingHost (gp->convertTo0to1 (juce::jlimit (-30.0f, 30.0f, db)));
}

int AnalyzerComponent::bandAtPixel (juce::Point<float> pt) const
{
    int best = -1; float bestD = 18.0f;
    for (int i = 0; i < kNumBands; ++i)
    {
        if (getParam (pid::band (i, pid::on)) < 0.5f) continue;
        const float x = freqToX (getParam (pid::band (i, pid::freq)));
        const float y = dbToY (getParam (pid::band (i, pid::gain)));
        const float d = pt.getDistanceFrom ({ x, y });
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
void AnalyzerComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    drawGrid (g);

    const int src = (int) getParam (pid::analyzer);
    if (src == 4) drawSpectrum (g, proc.scAnalyzer(),  theme().accent2.withAlpha (0.5f), true);
    if (src == 1 || src == 3) drawSpectrum (g, proc.preAnalyzer(),  theme().specPre, true);
    if (src == 2 || src == 3) drawSpectrum (g, proc.postAnalyzer(), theme().specPost, false);

    drawCurve (g);
    drawNodes (g);
}

void AnalyzerComponent::drawGrid (juce::Graphics& g)
{
    g.setColour (theme().grid.withAlpha (0.6f));
    const float decades[] = { 20,30,50,100,200,300,500,1000,2000,3000,5000,10000,20000 };
    g.setFont (10.0f);
    for (float f : decades)
    {
        const float x = freqToX (f);
        g.setColour (theme().grid.withAlpha (f == 100 || f == 1000 || f == 10000 ? 0.9f : 0.4f));
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        g.setColour (theme().textDim);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f),
                    (int) x + 2, getHeight() - 14, 36, 12, juce::Justification::left);
    }
    for (int db = -24; db <= 24; db += 6)
    {
        const float y = dbToY ((float) db);
        g.setColour (theme().grid.withAlpha (db == 0 ? 0.9f : 0.35f));
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
        g.setColour (theme().textDim);
        g.drawText (juce::String (db), 2, (int) y - 6, 28, 12, juce::Justification::left);
    }
}

void AnalyzerComponent::drawSpectrum (juce::Graphics& g, dsp::SpectrumAnalyzer& an, juce::Colour c, bool fill)
{
    const auto& mags = an.magnitudes();
    juce::Path p;
    bool started = false;
    const float bottom = (float) getHeight();
    for (int bin = 1; bin < dsp::SpectrumAnalyzer::numBins; ++bin)
    {
        const float f = an.binToFreq (bin);
        if (f < minHz || f > maxHz) continue;
        const float x = freqToX (f);
        // map magnitude dB (approx -100..0) to our view with a tilt for musical balance
        const float tilt = 4.5f * std::log2 (f / 1000.0f);
        const float db = juce::jlimit (minDb, maxDb, mags[(size_t) bin] + 30.0f + tilt);
        const float y = dbToY (db);
        if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
    }
    if (fill)
    {
        juce::Path f2 = p;
        f2.lineTo ((float) getWidth(), bottom);
        f2.lineTo (0.0f, bottom);
        f2.closeSubPath();
        g.setColour (c.withMultipliedAlpha (0.45f));
        g.fillPath (f2);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void AnalyzerComponent::drawCurve (juce::Graphics& g)
{
    const auto& t = theme();
    juce::Path p;
    const int steps = juce::jmax (64, getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / steps * getWidth();
        const float f = xToFreq (x);
        const float db = curveDb (f);
        const float y = dbToY (juce::jlimit (minDb, maxDb, db));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }

    // Fill the area between the curve and the 0 dB line, tinted toward boosts
    // (warm) and cuts — editorial look. Subtle in both light and dark themes.
    const float zeroY = dbToY (0.0f);
    juce::Path fillPath = p;
    fillPath.lineTo ((float) getWidth(), zeroY);
    fillPath.lineTo (0.0f, zeroY);
    fillPath.closeSubPath();
    g.setColour ((t.light ? t.accent : t.curve).withAlpha (t.light ? 0.10f : 0.14f));
    g.fillPath (fillPath);

    if (t.curveGlow)
    {
        g.setColour (t.curve.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (6.0f));
    }
    g.setColour (t.curve);
    g.strokePath (p, juce::PathStrokeType (t.light ? 2.6f : 2.2f));
}

void AnalyzerComponent::drawNodes (juce::Graphics& g)
{
    const auto& t = theme();
    for (int i = 0; i < kNumBands; ++i)
    {
        if (getParam (pid::band (i, pid::on)) < 0.5f) continue;
        const float x = freqToX (getParam (pid::band (i, pid::freq)));
        const float y = dbToY (getParam (pid::band (i, pid::gain)));
        const bool sel = (i == selected);
        const bool dyn = getParam (pid::band (i, pid::dyn)) > 0.5f;
        const float r = sel ? 11.0f : 9.0f;
        const auto nodeCol = bandColour (i);

        if (dyn) // outer ring marks a dynamic band
        {
            g.setColour (nodeCol.withAlpha (0.35f));
            g.fillEllipse (x - r - 3, y - r - 3, (r + 3) * 2, (r + 3) * 2);
        }
        g.setColour (nodeCol);
        g.fillEllipse (x - r, y - r, r * 2, r * 2);
        g.setColour (t.light ? t.text.withAlpha (0.85f) : t.bg);
        g.drawEllipse (x - r, y - r, r * 2, r * 2, sel ? 2.4f : 1.6f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), (int) (x - r), (int) (y - r), (int) (r * 2), (int) (r * 2),
                    juce::Justification::centred);
    }
}

// ---- interaction ----------------------------------------------------------
void AnalyzerComponent::mouseDown (const juce::MouseEvent& e)
{
    int b = bandAtPixel (e.position.toFloat());
    if (e.mods.isPopupMenu())
    {
        if (b < 0) return;
        juce::PopupMenu m;
        for (int t = 0; t < kFilterTypeNames.size(); ++t)
            m.addItem (100 + t, kFilterTypeNames[t], true, (int) getParam (pid::band (b, pid::type)) == t);
        m.addSeparator();
        for (int c = 0; c < kChannelModeNames.size(); ++c)
            m.addItem (200 + c, "Chan: " + kChannelModeNames[c], true, (int) getParam (pid::band (b, pid::chan)) == c);
        m.addSeparator();
        m.addItem (300, "Dynamic", true, getParam (pid::band (b, pid::dyn)) > 0.5f);
        m.addItem (301, "Disable band");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, b] (int r)
        {
            if (r >= 100 && r < 200) setBandParam (b, pid::type, (float) (r - 100) / (kFilterTypeNames.size() - 1));
            else if (r >= 200 && r < 300) setBandParam (b, pid::chan, (float) (r - 200) / (kChannelModeNames.size() - 1));
            else if (r == 300) { auto cur = getParam (pid::band (b, pid::dyn)) > 0.5f; setBandParam (b, pid::dyn, cur ? 0.0f : 1.0f); }
            else if (r == 301) setBandParam (b, pid::on, 0.0f);
        });
        return;
    }

    if (b < 0) // create a band at click position on empty space
    {
        for (int i = 0; i < kNumBands; ++i)
            if (getParam (pid::band (i, pid::on)) < 0.5f)
            {
                setBandParam (i, pid::on, 1.0f);
                setBandFreqGain (i, xToFreq (e.position.x), yToDb (e.position.y));
                b = i; break;
            }
    }
    if (b >= 0)
    {
        dragging = b; selected = b;
        if (onBandSelected) onBandSelected (b);
        repaint();
    }
}

void AnalyzerComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
        setBandFreqGain (dragging, xToFreq (e.position.x), yToDb (e.position.y));
}

void AnalyzerComponent::mouseUp (const juce::MouseEvent&) { dragging = -1; }

void AnalyzerComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int b = bandAtPixel (e.position.toFloat());
    if (b >= 0) setBandParam (b, pid::on, 0.0f); // double-click node removes it
}

void AnalyzerComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = bandAtPixel (e.position.toFloat());
    if (b < 0) b = selected;
    if (b < 0) return;
    if (auto* qp = proc.state().getParameter (pid::band (b, pid::q)))
    {
        const float cur = qp->getValue();
        qp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, cur + w.deltaY * 0.5f));
    }
}
