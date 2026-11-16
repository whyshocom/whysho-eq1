#include "AnalyzerComponent.h"
#include "WhyshoLookAndFeel.h"

using namespace wsho;
using namespace wsho::gui;

namespace
{
// Types whose gain parameter does something (the others ignore it).
bool hasGain (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf || t == FilterType::TiltShelf;
}

const juce::Identifier dbRangeId ("dbRange");

double magnitudeDb (double mag) { return 20.0 * std::log10 (juce::jmax (1.0e-6, mag)); }
}

AnalyzerComponent::AnalyzerComponent (WhyshoEQAudioProcessor& p) : proc (p)
{
    auto& s = proc.state();
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& r = bandRefs[(size_t) i];
        r.on       = s.getRawParameterValue (pid::band (i, pid::on));
        r.type     = s.getRawParameterValue (pid::band (i, pid::type));
        r.freq     = s.getRawParameterValue (pid::band (i, pid::freq));
        r.gain     = s.getRawParameterValue (pid::band (i, pid::gain));
        r.q        = s.getRawParameterValue (pid::band (i, pid::q));
        r.slope    = s.getRawParameterValue (pid::band (i, pid::slope));
        r.dyn      = s.getRawParameterValue (pid::band (i, pid::dyn));
        r.dynRange = s.getRawParameterValue (pid::band (i, pid::dynRange));
        r.freqParam = s.getParameter (pid::band (i, pid::freq));
        r.gainParam = s.getParameter (pid::band (i, pid::gain));
    }
    gainScaleRef = s.getRawParameterValue (pid::gainScale);
    analyzerRef  = s.getRawParameterValue (pid::analyzer);

    syncDbRange();
    startTimerHz (30);
    setInterceptsMouseClicks (true, true);
}
AnalyzerComponent::~AnalyzerComponent() { stopTimer(); }

void AnalyzerComponent::timerCallback()
{
    // Not on screen: nothing to draw, so no FFTs and no curve work.
    if (isShowing() && refresh()) repaint();
}

bool AnalyzerComponent::refresh()
{
    syncDbRange();

    // Only the taps that are displayed get analysed.
    const int src = (int) analyzerRef->load();
    const int was = std::exchange (lastSource, src);
    bool dirty = src != was;
    auto run = [&dirty] (dsp::SpectrumAnalyzer& an, bool shown, bool wasShown)
    {
        if (! shown) return;
        if (! wasShown) an.clear();
        if (an.render()) dirty = true;
    };
    run (proc.preAnalyzer(),  src == 1 || src == 3, was == 1 || was == 3);
    run (proc.postAnalyzer(), src == 2 || src == 3, was == 2 || was == 3);
    run (proc.scAnalyzer(),   src == 4,             was == 4);

    if (updateCurves()) dirty = true;
    return dirty;
}

// ---- display range --------------------------------------------------------
void AnalyzerComponent::syncDbRange()
{
    // The state tree can be replaced under us (session or preset load), so this
    // is checked every frame: follow a stored range, re-store ours if it is gone.
    auto& tree = proc.state().state;
    const int stored = tree.getProperty (dbRangeId, 0);
    if (stored == (int) rangeDb) return;

    if (std::find (dbRanges.begin(), dbRanges.end(), stored) != dbRanges.end())
    {
        rangeDb = (float) stored;
        repaint();
    }
    else tree.setProperty (dbRangeId, (int) rangeDb, nullptr);
}

void AnalyzerComponent::setDbRange (int range)
{
    rangeDb = (float) range;
    proc.state().state.setProperty (dbRangeId, range, nullptr);
    repaint();
}

juce::Rectangle<int> AnalyzerComponent::rangeButtonBounds (int index) const
{
    const int w = 30, n = (int) dbRanges.size();
    return { getWidth() - 8 - (n - index) * w, 8, w, 17 };
}

// ---- curves ---------------------------------------------------------------
bool AnalyzerComponent::updateCurves()
{
    const int width = getWidth();
    if (width <= 0 || getHeight() <= 0) return false;

    double sr = proc.engine().getSampleRate();
    if (sr <= 0.0) sr = 48000.0;

    // Draw what is actually heard. Zero Latency filters squeeze toward Nyquist
    // (less so when oversampled); Natural and Linear Phase follow the analog
    // curve, which a much higher display rate reproduces across the audible band.
    static const std::atomic<float> zero { 0.0f };
    const auto* phaseRef = proc.state().getRawParameterValue (pid::phase);
    const auto* osRef    = proc.state().getRawParameterValue (pid::os);
    const int phase = (int) (phaseRef != nullptr ? phaseRef : &zero)->load();
    const int os    = (int) (osRef    != nullptr ? osRef    : &zero)->load();
    sr *= (phase == (int) PhaseMode::ZeroLatency) ? (double) (1 << juce::jlimit (0, 4, os)) : 16.0;
    const float scale = gainScaleRef->load();

    // ---- the static curve: the EQ as set ----
    decltype (staticKey) sKey;
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& r = bandRefs[(size_t) i];
        auto& bp = displayParams[(size_t) i];
        bp.on     = r.on->load() > 0.5f;
        bp.type   = (FilterType) (int) r.type->load();
        bp.freq   = r.freq->load();
        bp.gainDb = r.gain->load() * scale;
        bp.q      = r.q->load();
        bp.order  = kSlopeOrders[(size_t) juce::jlimit (0, 7, (int) r.slope->load())];
        bp.dyn    = false;   // the display works out the dynamic movement itself

        float* k = sKey.data() + i * 6;
        k[0] = bp.on ? 1.0f : 0.0f;  k[1] = (float) bp.type;  k[2] = bp.freq;
        k[3] = bp.gainDb;            k[4] = bp.q;             k[5] = (float) bp.order;
    }
    float* tail = sKey.data() + kNumBands * 6;
    tail[0] = (float) sr;  tail[1] = (float) width;  tail[2] = (float) getHeight();  tail[3] = rangeDb;

    const bool staticChanged = sKey != staticKey || staticDb.empty();
    if (staticChanged)
    {
        staticKey = sKey;
        if (! juce::exactlyEqual (sr, displaySR)) { displaySR = sr; for (auto& b : displayBands) b.prepare (sr); }
        for (int i = 0; i < kNumBands; ++i)
            displayBands[(size_t) i].updateCoefficients (displayParams[(size_t) i]);

        staticDb.resize ((size_t) width + 1);
        for (int x = 0; x <= width; ++x)
            staticDb[(size_t) x] = curveDb (xToFreq ((float) x));
        staticPath = curvePath (staticDb);
    }

    // ---- the live curve: the same, with each dynamic band where it is right now ----
    decltype (liveKey) lKey;
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& r = bandRefs[(size_t) i];
        const bool dyn = displayParams[(size_t) i].on && r.dyn->load() > 0.5f;
        float* k = lKey.data() + i * 3;
        k[0] = dyn ? 1.0f : 0.0f;
        k[1] = dyn ? std::round (juce::jlimit (0.0f, 1.0f, proc.engine().dynamicAmount (i)) * 512.0f) / 512.0f : 0.0f;
        k[2] = dyn ? r.dynRange->load() : 0.0f;
    }
    if (! staticChanged && lKey == liveKey) return false;
    liveKey = lKey;

    liveDb = staticDb;
    liveDiffers = false;
    const double toW = 2.0 * M_PI / sr, topHz = sr * 0.5 - 1.0;
    auto wAt = [&] (int x) { return toW * juce::jlimit (1.0, topHz, (double) xToFreq ((float) x)); };

    for (int i = 0; i < kNumBands; ++i)
    {
        const float* k = lKey.data() + i * 3;
        const float amount = liveAmount[(size_t) i] = k[1], range = k[2];
        if (k[0] < 0.5f) continue;
        const auto& band = displayBands[(size_t) i];

        if (hasGain (displayParams[(size_t) i].type))
        {
            // Gain types: the dynamics move the gain toward gain + Range.
            if (juce::exactlyEqual (amount * range, 0.0f)) continue;
            auto bp = displayParams[(size_t) i];
            bp.gainDb += amount * range;
            dsp::EqBand moved;
            moved.prepare (sr);
            moved.updateCoefficients (bp);
            for (int x = 0; x <= width; ++x)
            {
                const double w = wAt (x);
                liveDb[(size_t) x] += (float) (magnitudeDb (std::abs (moved.response (w))) - magnitudeDb (std::abs (band.response (w))));
            }
        }
        else
        {
            // The others are blended in (negative Range) or out (positive Range).
            const double a = range < 0.0f ? amount : range > 0.0f ? 1.0f - amount : 1.0f;
            if (a >= 1.0) continue;
            for (int x = 0; x <= width; ++x)
            {
                const auto h = band.response (wAt (x));
                liveDb[(size_t) x] += (float) (magnitudeDb (std::abs (1.0 + a * (h - 1.0))) - magnitudeDb (std::abs (h)));
            }
        }
        liveDiffers = true;
    }

    if (liveDiffers)
    {
        livePath = curvePath (liveDb);
        liveFill = staticPath;                       // the area between the two curves
        for (int x = width; x >= 0; --x)
            liveFill.lineTo ((float) x, dbToY (juce::jlimit (-rangeDb, rangeDb, liveDb[(size_t) x])));
        liveFill.closeSubPath();
    }
    return true;
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

juce::Path AnalyzerComponent::curvePath (const std::vector<float>& db) const
{
    juce::Path p;
    for (size_t x = 0; x < db.size(); ++x)
    {
        const float y = dbToY (juce::jlimit (-rangeDb, rangeDb, db[x]));
        if (x == 0) p.startNewSubPath (0.0f, y); else p.lineTo ((float) x, y);
    }
    return p;
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
    return juce::jmap (db, rangeDb, -rangeDb, 0.0f, (float) getHeight());
}
float AnalyzerComponent::yToDb (float y) const
{
    return juce::jmap (y, 0.0f, (float) getHeight(), rangeDb, -rangeDb);
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
    const auto& r = bandRefs[(size_t) band];
    r.freqParam->setValueNotifyingHost (r.freqParam->convertTo0to1 (juce::jlimit (minHz, 20000.0f, hz)));
    if (hasGain ((FilterType) (int) r.type->load()))    // elsewhere the node stays on 0 dB, so leave the hidden gain alone
        r.gainParam->setValueNotifyingHost (r.gainParam->convertTo0to1 (juce::jlimit (-gainLimitDb, gainLimitDb, db)));
}

float AnalyzerComponent::nodeGainDb (int band) const
{
    const auto& r = bandRefs[(size_t) band];
    return hasGain ((FilterType) (int) r.type->load()) ? r.gain->load() * gainScaleRef->load() : 0.0f;
}

// Nodes beyond the visible range sit on the edge of the graph.
juce::Point<float> AnalyzerComponent::nodePosition (int band) const
{
    return { freqToX (bandRefs[(size_t) band].freq->load()),
             dbToY (juce::jlimit (-rangeDb, rangeDb, nodeGainDb (band))) };
}

float AnalyzerComponent::gainDragScale() const
{
    const float scale = gainScaleRef->load();
    return scale > 0.05f ? scale : 1.0f;    // with Scale near zero the nodes cannot follow anyway
}

int AnalyzerComponent::bandAtPixel (juce::Point<float> pt) const
{
    int best = -1; float bestD = 18.0f;
    for (int i = 0; i < kNumBands; ++i)
    {
        if (bandRefs[(size_t) i].on->load() < 0.5f) continue;
        const float d = pt.getDistanceFrom (nodePosition (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
void AnalyzerComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    if (! isShowing()) refresh();   // drawn offscreen (a snapshot): the timer is idle, so catch up here
    updateCurves();
    drawGrid (g);

    const int src = (int) analyzerRef->load();
    if (src == 4) drawSpectrum (g, proc.scAnalyzer(),  theme().accent2.withAlpha (0.8f));
    if (src == 1 || src == 3) drawSpectrum (g, proc.preAnalyzer(),  theme().specPre);
    if (src == 2 || src == 3) drawSpectrum (g, proc.postAnalyzer(), theme().specPost);

    drawCurve (g);
    drawNodes (g);
    drawRangeControl (g);
}

void AnalyzerComponent::drawGrid (juce::Graphics& g)
{
    g.setColour (theme().grid.withAlpha (0.6f));
    const float decades[] = { 20,30,50,100,200,300,500,1000,2000,3000,5000,10000,20000 };
    g.setFont (themeFont (9.5f));
    for (float f : decades)
    {
        const float x = freqToX (f);
        g.setColour (theme().grid.withAlpha (f == 100 || f == 1000 || f == 10000 ? 0.9f : 0.4f));
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        g.setColour (theme().textDim);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f),
                    (int) x + 2, getHeight() - 14, 36, 12, juce::Justification::left);
    }
    // A line every 2 dB at +/-6, 3 dB at +/-12, 6 dB at +/-30.
    const int range = (int) rangeDb, step = range <= 6 ? 2 : range <= 12 ? 3 : 6;
    for (int db = -range + step; db < range; db += step)
    {
        const float y = dbToY ((float) db);
        g.setColour (theme().grid.withAlpha (db == 0 ? 0.9f : 0.35f));
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
        g.setColour (theme().textDim);
        g.drawText (juce::String (db), 2, (int) y - 6, 28, 12, juce::Justification::left);
    }
}

// The spectrum has its own scale (specMinDb .. specMaxDb over the full height),
// so zooming the EQ range does not move it.
void AnalyzerComponent::drawSpectrum (juce::Graphics& g, const dsp::SpectrumAnalyzer& an, juce::Colour c)
{
    const auto& levels = an.levels();
    const float w = (float) getWidth(), h = (float) getHeight();
    const int n = dsp::SpectrumAnalyzer::numPoints;

    juce::Path p;
    for (int i = 0; i < n; ++i)
    {
        const float x = w * (float) i / (float) (n - 1);    // the points span minHz .. maxHz, like the graph
        const float y = juce::jmap (juce::jlimit (specMinDb, specMaxDb, levels[(size_t) i]), specMaxDb, specMinDb, 0.0f, h);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }

    juce::Path fill = p;
    fill.lineTo (w, h);
    fill.lineTo (0.0f, h);
    fill.closeSubPath();
    g.setColour (c.withMultipliedAlpha (0.2f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
}

void AnalyzerComponent::drawCurve (juce::Graphics& g)
{
    const auto& t = theme();
    const auto& p = staticPath;

    // Fill the area between the curve and the 0 dB line, tinted toward boosts
    // (warm) and cuts — editorial look. Subtle in both light and dark themes.
    const float zeroY = dbToY (0.0f);
    juce::Path fillPath = p;
    fillPath.lineTo ((float) getWidth(), zeroY);
    fillPath.lineTo (0.0f, zeroY);
    fillPath.closeSubPath();
    g.setColour ((t.light ? t.accent : t.curve).withAlpha (t.light ? 0.10f : 0.14f));
    g.fillPath (fillPath);

    // Where the dynamics have moved the response to: shaded from the static
    // curve, with a thin line of its own (under the main curve).
    if (liveDiffers)
    {
        g.setColour (t.accent2.withAlpha (0.22f));
        g.fillPath (liveFill);
        g.setColour (t.accent2);
        g.strokePath (livePath, juce::PathStrokeType (1.6f));
    }

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
        const auto& refs = bandRefs[(size_t) i];
        if (refs.on->load() < 0.5f) continue;
        const auto pos = nodePosition (i);
        const float x = pos.x, y = pos.y;
        const bool sel = (i == selected);
        const bool dyn = refs.dyn->load() > 0.5f;
        const float r = sel ? 11.0f : 9.0f;
        const auto nodeCol = bandColour (i);
        const auto outline = t.light ? t.text.withAlpha (0.85f) : t.bg;

        if (dyn)
        {
            const float ring = r + 4.5f, amount = liveAmount[(size_t) i], range = refs.dynRange->load();
            g.setColour (nodeCol.withAlpha (0.35f));    // outer ring marks a dynamic band
            g.drawEllipse (x - ring, y - ring, ring * 2, ring * 2, 3.0f);

            if (hasGain ((FilterType) (int) refs.type->load()))
            {
                // Range bracket: from the node to where the dynamics can take it,
                // with a marker where it is now.
                const float gain = nodeGainDb (i);
                const float yEnd  = dbToY (juce::jlimit (-rangeDb, rangeDb, gain + range));
                const float yLive = dbToY (juce::jlimit (-rangeDb, rangeDb, gain + amount * range));
                g.setColour (nodeCol);
                g.drawLine (x, y, x, yEnd, 2.0f);
                g.drawLine (x - 6.0f, yEnd, x + 6.0f, yEnd, 2.0f);
                g.fillEllipse (x - 5.0f, yLive - 5.0f, 10.0f, 10.0f);
                g.setColour (outline);
                g.drawEllipse (x - 5.0f, yLive - 5.0f, 10.0f, 10.0f, 1.4f);
            }
            else
            {
                // No gain to move: the ring fills as the filter engages.
                const float engaged = range < 0.0f ? amount : range > 0.0f ? 1.0f - amount : 1.0f;
                juce::Path arc;
                arc.addCentredArc (x, y, ring, ring, 0.0f, 0.0f, engaged * juce::MathConstants<float>::twoPi, true);
                g.setColour (nodeCol);
                g.strokePath (arc, juce::PathStrokeType (3.0f));
            }
        }
        g.setColour (nodeCol);
        g.fillEllipse (x - r, y - r, r * 2, r * 2);
        g.setColour (outline);
        g.drawEllipse (x - r, y - r, r * 2, r * 2, sel ? 2.4f : 1.6f);
        g.setColour (onColour (nodeCol));
        g.setFont (themeFont (11.0f, true));
        g.drawText (juce::String (i + 1), (int) (x - r), (int) (y - r), (int) (r * 2), (int) (r * 2),
                    juce::Justification::centred);
    }
}

// Compact +/-6 | 12 | 30 dB switch in the top-right corner of the graph.
void AnalyzerComponent::drawRangeControl (juce::Graphics& g)
{
    const auto& t = theme();
    const int n = (int) dbRanges.size();
    const auto all = rangeButtonBounds (0).getUnion (rangeButtonBounds (n - 1)).toFloat();
    g.setColour (t.panel2.withAlpha (0.9f));
    g.fillRoundedRectangle (all, 4.0f);
    g.setFont (themeFont (9.5f));
    for (int i = 0; i < n; ++i)
    {
        const auto b = rangeButtonBounds (i);
        const bool on = dbRanges[(size_t) i] == (int) rangeDb;
        if (on)
        {
            g.setColour (t.accent);
            g.fillRoundedRectangle (b.toFloat(), 4.0f);
        }
        g.setColour (on ? onColour (t.accent) : t.textDim);
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (dbRanges[(size_t) i]), b, juce::Justification::centred);
    }
    g.setColour (t.grid);
    g.drawRoundedRectangle (all, 4.0f, 1.0f);
}

// ---- interaction ----------------------------------------------------------
void AnalyzerComponent::gesture (int band, const char* suffix, bool begin)
{
    if (auto* p = proc.state().getParameter (pid::band (band, suffix)))
        begin ? p->beginChangeGesture() : p->endChangeGesture();
}

// A single change, reported to the host as one complete gesture (so automation
// recording and host undo see it properly).
void AnalyzerComponent::setBandValue (int band, const char* suffix, float realValue)
{
    if (auto* p = proc.state().getParameter (pid::band (band, suffix)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

void AnalyzerComponent::createBandAt (juce::Point<float> pos)
{
    for (int i = 0; i < kNumBands; ++i)
    {
        if (bandRefs[(size_t) i].on->load() > 0.5f) continue;

        // A fresh bell, whatever this slot was used for before.
        setBandValue (i, pid::type, (float) FilterType::Bell);
        setBandValue (i, pid::q, 1.0f);
        setBandValue (i, pid::chan, 0.0f);
        setBandValue (i, pid::dyn, 0.0f);
        setBandValue (i, pid::solo, 0.0f);
        setBandValue (i, pid::freq, juce::jlimit (minHz, 20000.0f, xToFreq (pos.x)));
        setBandValue (i, pid::gain, juce::jlimit (-gainLimitDb, gainLimitDb, yToDb (pos.y) / gainDragScale()));
        setBandValue (i, pid::on, 1.0f);

        selected = i;
        if (onBandSelected) onBandSelected (i);
        repaint();
        return;
    }
}

void AnalyzerComponent::mouseDown (const juce::MouseEvent& e)
{
    const int b = bandAtPixel (e.position);
    if (e.mods.isPopupMenu())
    {
        if (b < 0) return;
        selected = b;
        if (onBandSelected) onBandSelected (b);
        repaint();

        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        for (int t = 0; t < kFilterTypeNames.size(); ++t)
            m.addItem (100 + t, kFilterTypeNames[t], true, (int) getParam (pid::band (b, pid::type)) == t);
        m.addSeparator();
        for (int c = 0; c < kChannelModeNames.size(); ++c)
            m.addItem (200 + c, "Channel: " + kChannelModeNames[c], true, (int) getParam (pid::band (b, pid::chan)) == c);
        m.addSeparator();
        m.addItem (300, "Dynamic", true, getParam (pid::band (b, pid::dyn)) > 0.5f);
        m.addItem (301, "Remove band");

        // The editor can be closed while the menu is open.
        juce::Component::SafePointer<AnalyzerComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b] (int r)
        {
            if (safe == nullptr) return;
            if (r >= 100 && r < 200)      safe->setBandValue (b, pid::type, (float) (r - 100));
            else if (r >= 200 && r < 300) safe->setBandValue (b, pid::chan, (float) (r - 200));
            else if (r == 300)            safe->setBandValue (b, pid::dyn, safe->getParam (pid::band (b, pid::dyn)) > 0.5f ? 0.0f : 1.0f);
            else if (r == 301)            safe->setBandValue (b, pid::on, 0.0f);
        });
        return;
    }

    if (b < 0)      // empty space: the range switch, else nothing (double-click adds a band)
    {
        for (int i = 0; i < (int) dbRanges.size(); ++i)
            if (rangeButtonBounds (i).contains (e.getPosition()))
                setDbRange (dbRanges[(size_t) i]);
        return;
    }

    // The drag is relative to where the node really is, so a node pinned to the
    // edge (gain outside the visible range) doesn't jump when it is grabbed.
    dragging = b; selected = b;
    dragStart = e.position;
    dragStartX = freqToX (bandRefs[(size_t) b].freq->load());
    dragStartDb = nodeGainDb (b);
    gesture (b, pid::freq, true);
    gesture (b, pid::gain, true);
    if (onBandSelected) onBandSelected (b);
    repaint();
}

void AnalyzerComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const float db = dragStartDb + yToDb (e.position.y) - yToDb (dragStart.y);
    setBandFreqGain (dragging, xToFreq (dragStartX + e.position.x - dragStart.x), db / gainDragScale());
}

void AnalyzerComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        gesture (dragging, pid::freq, false);
        gesture (dragging, pid::gain, false);
    }
    dragging = -1;
}

void AnalyzerComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int b = bandAtPixel (e.position);
    if (b >= 0) { setBandValue (b, pid::on, 0.0f); return; }   // double-click a node: remove it

    for (int i = 0; i < (int) dbRanges.size(); ++i)
        if (rangeButtonBounds (i).contains (e.getPosition())) return;
    createBandAt (e.position);                                 // double-click empty space: add a band there
}

void AnalyzerComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Only a node under the pointer reacts, so scrolling past the graph changes nothing.
    const int b = bandAtPixel (e.position);
    if (b < 0) return;

    // Q moves by a ratio per wheel step (about 12%, a quarter of that with Shift).
    const float steps = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.5f : 2.0f);
    const float q = bandRefs[(size_t) b].q->load() * std::pow (1.25f, steps);
    setBandValue (b, pid::q, juce::jlimit (0.1f, 18.0f, q));
}
