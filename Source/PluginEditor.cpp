#include "PluginEditor.h"
#include "SoftwareCachedImage.h"

using namespace muzi::ui;
using E = muzi::FrankenTameEngine;

namespace
{
constexpr float kBaseW = 960.0f, kBaseH = 600.0f;
constexpr double fMin = 20.0, fMax = 20000.0;
constexpr float cutTop = 3.0f, cutBottom = -18.0f;          // cut-curve scale (dB)
constexpr float specTop = 0.0f, specRange = 96.0f;          // analyser scale (dBFS, tilted)
constexpr float tiltPerOct = 4.5f;                          // makes pink noise read flat

juce::String freqText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}
}

// ================================================================ Knob
Knob::Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& n,
            const juce::String& tip, bool bipolar)
    : name (n.toUpperCase())
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setTooltip (tip);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, slider);
    if (auto* p = st.getParameter (id))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        const auto unit = p->getLabel();
        slider.textFromValueFunction = [p, unit] (double v)
        {
            auto t = p->getText (p->convertTo0to1 ((float) v), 16);
            return unit.isEmpty() || t.endsWith (unit) ? t : t + " " + unit;
        };
    }
    slider.onValueChange = [this] { repaint(); };
    setTitle (n);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    const int nameH = juce::roundToInt ((hero ? 18.0f : 16.0f) * textScale);
    const int valH = juce::roundToInt ((hero ? 22.0f : 18.0f) * textScale);
    r.removeFromTop (nameH);
    r.removeFromBottom (valH);
    const int side = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void Knob::paint (juce::Graphics& g)
{
    const int nameH = juce::roundToInt ((hero ? 18.0f : 16.0f) * textScale);
    const int valH = juce::roundToInt ((hero ? 22.0f : 18.0f) * textScale);
    g.setColour (slider.isMouseOverOrDragging() ? col::text : col::quiet);
    g.setFont (font ((hero ? 12.0f : 10.5f) * textScale, true, 0.12f));
    g.drawText (name, getLocalBounds().removeFromTop (nameH), juce::Justification::centred);
    g.setColour (col::text);
    g.setFont (font ((hero ? 15.0f : 12.5f) * textScale, hero));
    g.drawText (slider.getTextFromValue (slider.getValue()), getLocalBounds().removeFromBottom (valH), juce::Justification::centred);
}

// ================================================================ bypass button
void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const bool bypassed = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (bypassed ? col::red.withAlpha (0.14f) : col::bg2.brighter (over ? 0.08f : 0.0f));
    g.fillEllipse (r);
    g.setColour (bypassed ? col::red.withAlpha (0.8f) : col::line);
    g.drawEllipse (r, 1.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.24f;
    juce::Path icon;
    icon.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    icon.startNewSubPath (c.x, c.y - rad * 1.25f);
    icon.lineTo (c.x, c.y - rad * 0.2f);
    g.setColour (bypassed ? col::red : col::accent);
    g.strokePath (icon, juce::PathStrokeType (r.getWidth() * 0.075f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ================================================================ meter
void LevelMeter::tick()
{
    const float db = juce::Decibels::gainToDecibels (source.load(), -60.0f);
    const float before = shown, holdBefore = hold;
    shown = db > shown ? db : juce::jmax (db, shown - 1.2f);
    if (db >= hold) { hold = db; holdTicks = 45; }
    else if (--holdTicks <= 0) hold = juce::jmax (-60.0f, hold - 0.8f);
    if (std::abs (shown - before) > 0.05f || std::abs (hold - holdBefore) > 0.05f) repaint();   // silent meters cost nothing
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto lab = r.removeFromBottom (16.0f * textScale);
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 12.0f * textScale), r.getHeight());
    g.setColour (col::faint);
    g.setFont (font (9.5f * textScale, true, 0.1f));
    g.drawText (label, lab, juce::Justification::centred);

    g.setColour (col::bg0);
    g.fillRoundedRectangle (r, 3.0f);
    auto toY = [&] (float db) { return r.getBottom() - (juce::jlimit (-60.0f, 3.0f, db) + 60.0f) / 63.0f * r.getHeight(); };
    const float y = toY (shown);
    juce::ColourGradient grad (col::accent, 0, r.getBottom(), col::red, 0, r.getY(), false);
    grad.addColour ((double) ((r.getBottom() - toY (-6.0f)) / r.getHeight()), col::amber);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.withTop (y), 3.0f);
    if (hold > -59.0f)
    {
        g.setColour (hold > -0.1f ? col::red : col::text.withAlpha (0.8f));
        g.fillRect (r.getX(), toY (hold) - 1.0f, r.getWidth(), 2.0f);
    }
}

// ================================================================ analyser
AnalyzerDisplay::AnalyzerDisplay (MuziTameProcessor& p) : proc (p) { setOpaque (true); }

juce::Rectangle<float> AnalyzerDisplay::plot() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (34.0f * textScale).withTrimmedBottom (20.0f * textScale);
}
float AnalyzerDisplay::xForFreq (double f) const
{
    const auto p = plot();
    return p.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * p.getWidth();
}
double AnalyzerDisplay::freqForX (float x) const
{
    const auto p = plot();
    return fMin * std::pow (fMax / fMin, (double) ((x - p.getX()) / p.getWidth()));
}
float AnalyzerDisplay::ySpec (float db) const
{
    const auto p = plot();
    return p.getBottom() - juce::jlimit (0.0f, 1.0f, (db - (specTop - specRange)) / specRange) * p.getHeight() * 0.92f;
}
float AnalyzerDisplay::yCut (float db) const
{
    const auto p = plot();
    const float top = p.getY() + p.getHeight() * 0.06f, bot = p.getBottom() - p.getHeight() * 0.04f;
    return top + (cutTop - juce::jlimit (cutBottom, cutTop, db)) / (cutTop - cutBottom) * (bot - top);
}

void AnalyzerDisplay::resized()
{
    const auto n = (size_t) juce::jmax (8, (int) plot().getWidth());
    colIn.assign (n, -120.0f); colOut.assign (n, -120.0f); colCut.assign (n, 0.0f); colBroad.assign (n, 0.0f);
    staticScale = 0.0f; cosRate = 0.0;
}

void AnalyzerDisplay::computeSpectrum (std::vector<float>& ring, std::vector<float>& spec)
{
    for (int i = 0; i < kN; ++i) work[(size_t) i] = ring[(size_t) ((ringPos + i) % kN)];
    std::fill (work.begin() + kN, work.end(), 0.0f);
    window.multiplyWithWindowingTable (work.data(), (size_t) kN);
    fft.performFrequencyOnlyForwardTransform (work.data());
    for (int k = 0; k <= kN / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (work[(size_t) k] * 4.0f / (float) kN, -140.0f);
        float& sv = spec[(size_t) k];
        sv = db > sv ? sv + 0.6f * (db - sv) : juce::jmax (db, sv - 1.1f);      // fast rise, slow fall
    }
}

void AnalyzerDisplay::toColumns (const std::vector<float>& spec, std::vector<float>& cols)
{
    const double fs = proc.getCurrentSampleRate(), binHz = fs / kN;
    const auto p = plot();
    const int n = (int) cols.size();
    for (int c = 0; c < n; ++c)
    {
        const double f0 = freqForX (p.getX() + (float) c), f1 = freqForX (p.getX() + (float) c + 1.0f);
        const int k0 = (int) std::floor (f0 / binHz), k1 = (int) std::floor (f1 / binHz);
        float v;
        if (k1 > k0 + 1)
        {
            v = -140.0f;
            for (int k = k0 + 1; k <= juce::jmin (k1, kN / 2); ++k) v = juce::jmax (v, spec[(size_t) k]);
        }
        else
        {
            const double x = juce::jlimit (0.0, (double) (kN / 2 - 1), f0 / binHz);
            const int k = (int) x; const float fr = (float) (x - k);
            v = spec[(size_t) k] * (1.0f - fr) + spec[(size_t) k + 1] * fr;
        }
        cols[(size_t) c] = v + tiltPerOct * (float) std::log2 (juce::jmax (f0, 1.0) / 1000.0);
    }
    for (int pass = 0; pass < 2; ++pass)                                    // light smoothing
        for (int c = 1; c < n - 1; ++c)
            cols[(size_t) c] = 0.25f * cols[(size_t) c - 1] + 0.5f * cols[(size_t) c] + 0.25f * cols[(size_t) c + 1];
}

void AnalyzerDisplay::update()
{
    // both taps advance the same ring position (pre and post arrive in lock-step)
    const int nIn = proc.preTap.pull (scratch.data(), (int) scratch.size());
    int pos = ringPos;
    for (int i = juce::jmax (0, nIn - kN); i < nIn; ++i) { ringIn[(size_t) pos] = scratch[(size_t) i]; pos = (pos + 1) % kN; }
    const int nOut = proc.postTap.pull (scratch.data(), (int) scratch.size());
    int pos2 = ringPos;
    for (int i = juce::jmax (0, nOut - kN); i < nOut; ++i) { ringOut[(size_t) pos2] = scratch[(size_t) i]; pos2 = (pos2 + 1) % kN; }
    ringPos = pos;

    // nothing playing: let the curves fall for ~3 s, then stop redrawing until audio returns
    idleFrames = (nIn == 0 && nOut == 0) ? idleFrames + 1 : 0;
    if (idleFrames > 90) return;

    if (quality < 2)
    {
        computeSpectrum (ringIn, specIn);
        computeSpectrum (ringOut, specOut);
        toColumns (specIn, colIn);
        toColumns (specOut, colOut);
    }
    else
    {
        std::fill (colIn.begin(), colIn.end(), -120.0f);
        std::fill (colOut.begin(), colOut.end(), -120.0f);
    }

    const auto& eng = proc.activeEngine();
    const double fs = proc.getCurrentSampleRate(), q = proc.getSharpness();
    const auto p = plot();
    const size_t n = colCut.size();
    if (std::abs (cosRate - fs) > 0.5 || cosW.size() != n)
    {
        cosW.resize (n); cos2W.resize (n);
        for (size_t c = 0; c < n; ++c)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * juce::jmin (freqForX (p.getX() + (float) c), fs * 0.49) / fs;
            cosW[c] = std::cos (w); cos2W[c] = std::cos (2.0 * w);
        }
        cosRate = fs;
    }
    std::fill (colCut.begin(), colCut.end(), 0.0f);
    std::fill (colBroad.begin(), colBroad.end(), 0.0f);
    // |H(e^jw)|^2 of an RBJ bell from its coefficients: the same curve as bellResponseDb(),
    // with the per-filter maths done once instead of once per screen column
    auto addBell = [&] (double fc, double bq, double gainDb, bool broadLayer)
    {
        const double A_ = std::pow (10.0, gainDb / 40), w0 = 2 * juce::MathConstants<double>::pi * fc / fs, al = std::sin (w0) / (2 * bq);
        const double b0 = 1 + al * A_, b1 = -2 * std::cos (w0), b2 = 1 - al * A_;
        const double a0 = 1 + al / A_, a1 = b1, a2 = 1 - al / A_;
        const double nb0 = b0 * b0 + b1 * b1 + b2 * b2, nb1 = 2 * b1 * (b0 + b2), nb2 = 2 * b0 * b2;
        const double na0 = a0 * a0 + a1 * a1 + a2 * a2, na1 = 2 * a1 * (a0 + a2), na2 = 2 * a0 * a2;
        for (size_t c = 0; c < n; ++c)
        {
            const double num = nb0 + nb1 * cosW[c] + nb2 * cos2W[c], den = na0 + na1 * cosW[c] + na2 * cos2W[c];
            const float db = (float) (10.0 * std::log10 (juce::jmax (num, 1e-24) / juce::jmax (den, 1e-24)));
            colCut[c] += db;
            if (broadLayer) colBroad[c] += db;
        }
    };
    float gB[E::kBroad];
    for (int i = 0; i < E::kNarrow; ++i) { nGainN[i] = eng.meterGainN[i].load(); nFreqN[i] = eng.meterFreqN[i].load(); }
    for (int j = 0; j < E::kBroad; ++j) gB[j] = eng.meterGainB[j].load();
    for (int i = 0; i < E::kNarrow; ++i) if (nGainN[i] < -0.05f) addBell (nFreqN[i], q, nGainN[i], false);
    for (int j = 0; j < E::kBroad; ++j) if (std::abs (gB[j]) > 0.02f) addBell (eng.getBroadFc (j), eng.getBroadQ (j), gB[j], true);
    // columns above Nyquist show no cut (as before)
    for (size_t c = 0; c < n; ++c) if (freqForX (p.getX() + (float) c) >= fs * 0.49) { colCut[c] = 0.0f; colBroad[c] = 0.0f; }
    repaint();
}

juce::RangedAudioParameter* AnalyzerDisplay::param (int which) const
{
    return proc.apvts.getParameter (which == 1 ? "low" : "high");
}
float AnalyzerDisplay::limitHz (int which) const
{
    return proc.apvts.getRawParameterValue (which == 1 ? "low" : "high")->load();
}
int AnalyzerDisplay::handleAt (float x) const
{
    const float tol = 9.0f * textScale;
    const float dl = std::abs (x - xForFreq (limitHz (1))), dh = std::abs (x - xForFreq (limitHz (2)));
    if (dl > tol && dh > tol) return 0;
    return dl <= dh ? 1 : 2;
}

void AnalyzerDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverX = plot().contains (e.position) ? (int) e.position.x : -1;
    setMouseCursor (handleAt (e.position.x) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}
void AnalyzerDisplay::mouseExit (const juce::MouseEvent&) { hoverX = -1; repaint(); }
void AnalyzerDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position.x);
    if (dragging) param (dragging)->beginChangeGesture();
}
void AnalyzerDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    double f = juce::jlimit (fMin, fMax, freqForX (e.position.x));
    if (dragging == 1) f = juce::jmin (f, (double) limitHz (2) / 1.26);     // keep at least 1/3 octave open
    else f = juce::jmax (f, (double) limitHz (1) * 1.26);
    auto* p = param (dragging);
    p->setValueNotifyingHost (p->convertTo0to1 ((float) f));
    hoverX = (int) e.position.x;
    repaint();
}
void AnalyzerDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging) param (dragging)->endChangeGesture();
    dragging = 0;
}
void AnalyzerDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int h = handleAt (e.position.x))
    {
        auto* p = param (h);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void AnalyzerDisplay::renderStaticLayer (float pixelScale)
{
    const auto b = getLocalBounds().toFloat();
    const auto p = plot();
    const float ts = textScale;
    staticLayer = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt (b.getWidth() * pixelScale)),
                               juce::jmax (1, juce::roundToInt (b.getHeight() * pixelScale)), false, juce::SoftwareImageType());
    staticScale = pixelScale;
    juce::Graphics g (staticLayer);
    g.addTransform (juce::AffineTransform::scale (pixelScale));
    // continue the editor's background behind the rounded corners, so the display can be opaque
    g.setGradientFill (juce::ColourGradient (col::bg0.brighter (0.05f), 0, (float) -getY(), col::bg0, 0, (float) (editorHeight - getY()), false));
    g.fillAll();
    juce::ColourGradient bg (col::bg1.brighter (0.04f), 0, b.getY(), col::bg0, 0, b.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (b, 10.0f * ts);
    g.setFont (font (10.0f * ts));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForFreq (f);
        g.setColour (col::line.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (x), p.getY(), p.getBottom());
        g.setColour (col::faint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20 * ts, p.getBottom() + 3 * ts, 40 * ts, 14 * ts), juce::Justification::centred);
    }
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f })
    {
        const float y = yCut (db);
        g.setColour (db > -0.5f ? col::faint.withAlpha (0.6f) : col::line.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (y), p.getX(), p.getRight());
        g.setColour (col::faint);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 4 * ts, y - 7 * ts, 28 * ts, 14 * ts), juce::Justification::centredLeft);
    }
    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f * ts, 1.0f);
}

// macOS's CoreGraphics is slow at stroking and filling these long, animated paths at Retina
// resolution (measured on an Intel Mac: about half a CPU core at 30 fps). JUCE's own software
// renderer draws the same frame several times faster, so the analyser is drawn into an
// image at the screen's pixel density and handed to the system as a single bitmap.
void AnalyzerDisplay::paint (juce::Graphics& g)
{
    const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr)
    {
        paintContent (g, pixelScale);           // already drawing with the software renderer (editor cache)
        return;
    }
    const int W = juce::jmax (1, juce::roundToInt ((float) getWidth() * pixelScale));
    const int H = juce::jmax (1, juce::roundToInt ((float) getHeight() * pixelScale));
    // The bitmap is the platform's own image type (so the system can show it without converting it),
    // but JUCE's software renderer draws into its pixels directly.
    if (! frame.isValid() || frame.getWidth() != W || frame.getHeight() != H)
        frame = juce::Image (juce::Image::RGB, W, H, false, juce::NativeImageType());
    {
        juce::LowLevelGraphicsSoftwareRenderer renderer (frame);
        juce::Graphics fg (renderer);
        fg.addTransform (juce::AffineTransform::scale (pixelScale));
        paintContent (fg, pixelScale);
    }
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // 1:1 pixels, no filtering needed
    g.drawImage (frame, getLocalBounds().toFloat());
}

void AnalyzerDisplay::paintContent (juce::Graphics& g, float pixelScale)
{
    const auto b = getLocalBounds().toFloat();
    const auto p = plot();
    const float ts = textScale;

    if (! staticLayer.isValid() || std::abs (pixelScale - staticScale) > 0.01f) renderStaticLayer (pixelScale);
    g.drawImage (staticLayer, b);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (p.toNearestInt().expanded (2));

        const int n = (int) colIn.size();
        if (quality < 2)                            // analyser off: only the cut curve
        {
            // input spectrum, filled
            juce::Path in;
            in.startNewSubPath (p.getX(), p.getBottom());
            for (int c = 0; c < n; c = (c + 2 < n || c == n - 1) ? c + 2 : n - 1) in.lineTo (p.getX() + (float) c, ySpec (colIn[(size_t) c]));
            in.lineTo (p.getX() + (float) (n - 1), p.getBottom());
            in.closeSubPath();
            juce::ColourGradient sg (juce::Colour (0xff4a5468).withAlpha (0.55f), 0, p.getY() + p.getHeight() * 0.3f,
                                     juce::Colour (0xff2a303c).withAlpha (0.35f), 0, p.getBottom(), false);
            g.setGradientFill (sg);
            g.fillPath (in);

            // output spectrum, line
            juce::Path out;
            for (int c = 0; c < n; c = (c + 2 < n || c == n - 1) ? c + 2 : n - 1)
            {
                const float x = p.getX() + (float) c, y = ySpec (colOut[(size_t) c]);
                c == 0 ? out.startNewSubPath (x, y) : out.lineTo (x, y);
            }
            g.setColour (col::text.withAlpha (0.38f));
            g.strokePath (out, juce::PathStrokeType (1.1f * ts));
        }

        // out-of-range shading
        const float xl = xForFreq (limitHz (1)), xh = xForFreq (limitHz (2));
        g.setColour (col::bg0.withAlpha (0.62f));
        if (xl > p.getX()) g.fillRect (juce::Rectangle<float> (p.getX(), p.getY(), xl - p.getX(), p.getHeight()));
        if (xh < p.getRight()) g.fillRect (juce::Rectangle<float> (xh, p.getY(), p.getRight() - xh, p.getHeight()));

        // cut curve: filled from 0 dB, glow, line
        const float y0 = yCut (0.0f);
        juce::Path cutLine, cutFill;
        cutFill.startNewSubPath (p.getX(), y0);
        for (int c = 0; c < n; c = (c + 2 < n || c == n - 1) ? c + 2 : n - 1)     // smooth curve: every 2nd column
        {
            const float x = p.getX() + (float) c, y = yCut (colCut[(size_t) c]);
            c == 0 ? cutLine.startNewSubPath (x, y) : cutLine.lineTo (x, y);
            cutFill.lineTo (x, y);
        }
        cutFill.lineTo (p.getX() + (float) (n - 1), y0);
        cutFill.closeSubPath();
        juce::ColourGradient cg (col::accent.withAlpha (0.10f), 0, y0, col::accent.withAlpha (0.42f), 0, yCut (-12.0f), false);
        g.setGradientFill (cg);
        g.fillPath (cutFill);
        g.setColour (col::accent.withAlpha (0.22f));
        g.strokePath (cutLine, juce::PathStrokeType (6.0f * ts, juce::PathStrokeType::beveled));
        g.setColour (col::accent);
        g.strokePath (cutLine, juce::PathStrokeType (2.0f * ts, juce::PathStrokeType::beveled));

        juce::Path broadLine;
        for (int c = 0; c < n; c = (c + 2 < n || c == n - 1) ? c + 2 : n - 1)
        {
            const float x = p.getX() + (float) c, y = yCut (colBroad[(size_t) c]);
            c == 0 ? broadLine.startNewSubPath (x, y) : broadLine.lineTo (x, y);
        }
        g.setColour (col::amber.withAlpha (0.85f));
        g.strokePath (broadLine, juce::PathStrokeType (1.3f * ts));

        for (int i = 0; i < E::kNarrow; ++i)
            if (nGainN[i] < -0.4f && nFreqN[i] > fMin && nFreqN[i] < fMax)
            {
                const float x = xForFreq (nFreqN[i]), y = yCut (nGainN[i]);
                const float r = 3.2f * ts;
                g.setColour (col::bg0);
                g.fillEllipse (x - r - 1, y - r - 1, 2 * r + 2, 2 * r + 2);
                g.setColour (col::accent);
                g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
            }

        // range handles
        auto drawHandle = [&] (float x, int which)
        {
            const bool active = dragging == which || (hoverX >= 0 && handleAt ((float) hoverX) == which);
            g.setColour (col::amber.withAlpha (active ? 1.0f : 0.7f));
            g.fillRect (juce::Rectangle<float> (x - 0.75f * ts, p.getY(), 1.5f * ts, p.getHeight()));
            const juce::String txt = (which == 1 ? "LOW  " : "HIGH  ") + freqText (limitHz (which));
            g.setFont (font (10.0f * ts, true, 0.06f));
            const float tw = (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 14.0f * ts;
            auto tab = juce::Rectangle<float> (which == 1 ? x : x - tw, p.getY() + 8.0f * ts, tw, 18.0f * ts);
            tab = tab.constrainedWithin (p.reduced (2.0f));
            g.setColour (col::bg2.withAlpha (0.95f));
            g.fillRoundedRectangle (tab, 4.0f * ts);
            g.setColour (col::amber.withAlpha (active ? 1.0f : 0.75f));
            g.drawRoundedRectangle (tab, 4.0f * ts, 1.0f);
            g.drawText (txt, tab, juce::Justification::centred);
        };
        drawHandle (xl, 1);
        drawHandle (xh, 2);

        // legend
        float lx = p.getX() + 12.0f * ts;
        const float ly = p.getBottom() - 18.0f * ts;
        auto legend = [&] (juce::Colour c, const juce::String& t)
        {
            g.setColour (c);
            g.fillRoundedRectangle (lx, ly + 4.0f * ts, 10.0f * ts, 3.0f * ts, 1.5f);
            g.setColour (col::quiet);
            g.setFont (font (9.5f * ts, true, 0.1f));
            g.drawText (t, juce::Rectangle<float> (lx + 14 * ts, ly - 1 * ts, 60 * ts, 13 * ts), juce::Justification::centredLeft);
            lx += 14.0f * ts + (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 14.0f * ts;
        };
        legend (juce::Colour (0xff6b7588), "INPUT");
        legend (col::text.withAlpha (0.6f), "OUTPUT");
        legend (col::accent, "CUT");
        legend (col::amber, "BROAD");

        // hover readout
        if (hoverX >= 0 && ! dragging)
        {
            const int c = juce::jlimit (0, n - 1, hoverX - (int) p.getX());
            const double f = freqForX ((float) hoverX);
            g.setColour (col::text.withAlpha (0.25f));
            g.drawVerticalLine (hoverX, p.getY(), p.getBottom());
            const juce::String t = freqText (f) + "   " + juce::String (colCut[(size_t) c], 1) + " dB";
            g.setFont (font (11.0f * ts, true));
            const float tw = (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 16.0f * ts;
            auto tag = juce::Rectangle<float> ((float) hoverX + 8 * ts, yCut (colCut[(size_t) c]) + 10 * ts, tw, 20 * ts)
                           .constrainedWithin (p.reduced (4.0f));
            g.setColour (col::bg2);
            g.fillRoundedRectangle (tag, 4.0f * ts);
            g.setColour (col::text);
            g.drawText (t, tag, juce::Justification::centred);
        }
    }

}

// ================================================================ editor
MuziTameEditor::MuziTameEditor (MuziTameProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p),
      depth     (p.apvts, "depth", "Depth", "How hard resonances are cut. 100% is the calibrated amount; go past it for heavier taming."),
      threshold (p.apvts, "sensitivity", "Threshold", "How far a peak must rise above its surroundings before it is cut. Lower catches more."),
      sharpness (p.apvts, "sharpness", "Sharpness", "Width of each moving cut. Higher = narrower, more surgical cuts."),
      attack    (p.apvts, "attack", "Attack", "How quickly cuts engage once a resonance appears."),
      release   (p.apvts, "release", "Release", "How quickly cuts let go after a resonance fades."),
      punch     (p.apvts, "punch", "Punch", "Keeps drum hits and plucks snappy by easing every cut for the moment a transient passes."),
      broad     (p.apvts, "broad", "Broad", "Wide, gentle tonal balancing on top of the narrow cuts (gold curve)."),
      guard     (p.apvts, "guard", "Tonal Guard", "Protects musical notes (melodies, chords, harmonics) so only real resonances are cut. Lower it to chase moving or on-and-off resonances harder."),
      maxcut    (p.apvts, "maxcut", "Max Cut", "The deepest any single cut is allowed to go."),
      mix       (p.apvts, "mix", "Mix", "Blend between the original and the processed signal."),
      trim      (p.apvts, "trim", "Output", "Output level, to level-match against bypass.", true),
      inMeter (p.inLevel, "IN"), outMeter (p.outLevel, "OUT")
{
    setLookAndFeel (&lnf);
    depth.hero = true;

    addAndMakeVisible (display);
    for (auto* k : { &depth, &threshold, &sharpness, &attack, &release, &punch, &broad, &guard, &maxcut, &mix, &trim })
        addAndMakeVisible (k);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    // presets: factory + your own (saved as files, see the manual)
    presetBox.setTooltip ("Presets: factory starting points and your own saved presets. A * means a knob has moved since the preset was loaded.");
    presetBox.onAboutToOpen = [this] { rebuildPresetMenu(); };
    presetBox.onChange = [this] { presetChosen (presetBox.getSelectedId()); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    prevPreset.setTooltip ("Previous preset (factory, then your own).");
    nextPreset.setTooltip ("Next preset (factory, then your own).");
    savePreset.setTooltip ("Save the current settings as your own preset.");
    savePreset.onClick = [this] { showSaveDialog(); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevPreset, &nextPreset, &savePreset })
        addAndMakeVisible (c);
    rebuildPresetMenu();

    // mode: segmented control bound to the choice parameter
    modeLive.getProperties().set ("edges", 1);
    modeStd.getProperties().set ("edges", 3);
    modeHi.getProperties().set ("edges", 2);
    modeLive.setTooltip ("Live: zero latency. For recording, monitoring and live sound. Reacts a little later to short, sudden harshness.");
    modeStd.setTooltip ("Standard: 32 ms latency. The everyday mixing mode.");
    modeHi.setTooltip ("High-res: 91 ms latency, faster attack and finer low-end detail. For mixing and mastering.");
    modeAttachment = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter ("mode"), [this] (float v)
    {
        const int m = juce::roundToInt (v);
        modeStd.setToggleState (m == 0, juce::dontSendNotification);
        modeHi.setToggleState (m == 1, juce::dontSendNotification);
        modeLive.setToggleState (m == 2, juce::dontSendNotification);
    });
    modeStd.onClick  = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    modeHi.onClick   = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    modeLive.onClick = [this] { modeAttachment->setValueAsCompleteGesture (2.0f); };
    modeAttachment->sendInitialUpdate();

    deltaBtn.setClickingTogglesState (true);
    deltaBtn.setTooltip ("Delta: listen to only what Muzi Tame is removing.");
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "delta", deltaBtn);
    bypassBtn.setTooltip ("Bypass (latency is kept, so A/B comparisons stay in time).");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "bypass", bypassBtn);
    for (auto* c : std::initializer_list<juce::Component*> { &modeLive, &modeStd, &modeHi, &deltaBtn, &bypassBtn, &analyserBtn })
        addAndMakeVisible (c);
    analyserBtn.setTooltip ("Analyser display: Full, Light (half the frame rate) or Off. Lighter settings help older or busy computers. The sound does not change.");
    analyserBtn.onClick = [this] { setAnalyserQuality ((display.quality + 1) % 3); };
    setAnalyserQuality ((int) proc.apvts.state.getProperty ("analyser", 0));

    // v0.6.3 drew the whole window through a software cache; measured on an Intel Mac that cost
    // more than it saved, so it is off (MUZI_SOFTWARE_CACHE=1 brings it back for comparison)
    if (juce::SystemStats::getEnvironmentVariable ("MUZI_SOFTWARE_CACHE", {}) == "1")
        setCachedComponentImage (new SoftwareCachedImage (*this));

    setResizable (true, true);
    setResizeLimits (720, 450, 1440, 900);
    getConstrainer()->setFixedAspectRatio (kBaseW / kBaseH);
    setSize ((int) kBaseW, (int) kBaseH);
    startTimerHz (60);
}

MuziTameEditor::~MuziTameEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MuziTameEditor::rebuildPresetMenu()
{
    userFiles = MuziTameProcessor::getUserPresets();
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < MuziTameProcessor::getNumFactoryPresets(); ++i)
        presetBox.addItem (MuziTameProcessor::getFactoryPreset (i).name, i + 1);
    presetBox.addSectionHeading ("Your presets");
    if (userFiles.isEmpty())
    {
        presetBox.addItem ("(none yet - click SAVE)", kUserBase - 1);
        presetBox.setItemEnabled (kUserBase - 1, false);
    }
    for (int i = 0; i < userFiles.size(); ++i)
        presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), kUserBase + i);
    presetBox.addSeparator();
    presetBox.addItem ("Save as new preset...", kSaveId);
    if (proc.currentPreset < 0 && proc.userPresetName.isNotEmpty())
        presetBox.addItem ("Delete \"" + proc.userPresetName + "\"...", kDeleteId);
    presetBox.addItem ("Show presets folder", kFolderId);
    shownPresetText = {};
    refreshPresetBox();
}

void MuziTameEditor::refreshPresetBox()
{
    shownPresetVersion = proc.presetVersion.load();
    juce::String text = proc.currentPresetName();
    if (text.isEmpty()) text = "Default";
    if (proc.presetModified()) text << " *";
    if (text == shownPresetText) return;
    shownPresetText = text;
    presetBox.setText (text, juce::dontSendNotification);      // shows the name (and *) without selecting an item
}

void MuziTameEditor::presetChosen (int id)
{
    if (id >= 1 && id <= MuziTameProcessor::getNumFactoryPresets())
        proc.loadFactoryPreset (id - 1);
    else if (id >= kUserBase && id < kUserBase + userFiles.size())
    {
        if (! proc.loadUserPreset (userFiles[id - kUserBase]))
            showDialog (std::make_unique<juce::AlertWindow> ("Muzi Tame", "That preset file could not be read.", juce::MessageBoxIconType::WarningIcon), {});
    }
    else if (id == kSaveId)   showSaveDialog();
    else if (id == kDeleteId) confirmDelete();
    else if (id == kFolderId)
    {
        const auto folder = MuziTameProcessor::getUserPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
    shownPresetText = {};
    refreshPresetBox();
}

void MuziTameEditor::stepPreset (int delta)
{
    userFiles = MuziTameProcessor::getUserPresets();
    const int nf = MuziTameProcessor::getNumFactoryPresets();
    const int total = nf + userFiles.size();
    int pos = proc.currentPreset;                                  // position in factory + user list
    if (pos < 0)
    {
        pos = nf;                                                  // a user preset: find it
        for (int i = 0; i < userFiles.size(); ++i)
            if (userFiles[i].getFileNameWithoutExtension() == proc.userPresetName) { pos = nf + i; break; }
    }
    pos = (pos + delta + total) % total;
    if (pos < nf) proc.loadFactoryPreset (pos);
    else          proc.loadUserPreset (userFiles[pos - nf]);
    shownPresetText = {};
    refreshPresetBox();
}

void MuziTameEditor::showDialog (std::unique_ptr<juce::AlertWindow> w, std::function<void (int)> onResult)
{
    if (w->getNumButtons() == 0) w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog = std::move (w);
    dialog->setLookAndFeel (&lnf);
    addAndMakeVisible (*dialog);                     // inside the plugin window, so no host can hide it behind
    dialog->setCentrePosition (getLocalBounds().getCentre());
    juce::Component::SafePointer<MuziTameEditor> safe (this);
    auto* shown = dialog.get();
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, onResult, shown] (int result)
    {
        if (safe == nullptr) return;
        if (onResult) onResult (result);
        // remove this dialog later (not from inside its own callback), unless another replaced it already
        juce::MessageManager::callAsync ([safe, shown] { if (safe != nullptr && safe->dialog.get() == shown) safe->dialog.reset(); });
    }), false);
}

void MuziTameEditor::showSaveDialog()
{
    auto w = std::make_unique<juce::AlertWindow> ("Save preset", "Name your preset. It will appear under \"Your presets\" in every Muzi Tame.",
                                                  juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.currentPreset < 0 ? proc.userPresetName : juce::String(), "Name:");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    auto* ed = w->getTextEditor ("name");
    showDialog (std::move (w), [this, ed] (int r)
    {
        if (r != 1) return;
        const auto name = juce::File::createLegalFileName (ed->getText().trim());
        if (name.isEmpty()) return;
        const auto doSave = [this, name]
        {
            if (proc.saveUserPreset (name) == juce::File())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MuziTameEditor> (this)]
                {
                    if (safe != nullptr)
                        safe->showDialog (std::make_unique<juce::AlertWindow> ("Muzi Tame", "The preset could not be saved in:\n"
                                              + MuziTameProcessor::getUserPresetFolder().getFullPathName(), juce::MessageBoxIconType::WarningIcon), {});
                });
            rebuildPresetMenu();
        };
        const auto existing = MuziTameProcessor::getUserPresetFolder().getChildFile (name + MuziTameProcessor::kUserPresetExt);
        if (! existing.existsAsFile()) { doSave(); return; }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MuziTameEditor> (this), name, doSave]
        {
            if (safe == nullptr) return;
            auto c = std::make_unique<juce::AlertWindow> ("Replace preset?", "\"" + name + "\" already exists. Replace it with the current settings?",
                                                          juce::MessageBoxIconType::QuestionIcon);
            c->addButton ("Replace", 1, juce::KeyPress (juce::KeyPress::returnKey));
            c->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            safe->showDialog (std::move (c), [doSave] (int r2) { if (r2 == 1) doSave(); });
        });
    });
    if (dialog != nullptr) ed->grabKeyboardFocus();
}

void MuziTameEditor::confirmDelete()
{
    const auto name = proc.userPresetName;
    const auto file = MuziTameProcessor::getUserPresetFolder().getChildFile (name + MuziTameProcessor::kUserPresetExt);
    if (name.isEmpty() || ! file.existsAsFile()) return;
    auto c = std::make_unique<juce::AlertWindow> ("Delete preset?", "Delete \"" + name + "\"? Your current settings stay as they are.",
                                                  juce::MessageBoxIconType::WarningIcon);
    c->addButton ("Delete", 1);
    c->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    showDialog (std::move (c), [this, file] (int r)
    {
        if (r != 1) return;
        file.moveToTrash() || file.deleteFile();
        rebuildPresetMenu();
    });
}

juce::Component* MuziTameEditor::findPart (const juce::String& id)
{
    const std::pair<const char*, juce::Component*> parts[] = {
        { "depth", &depth }, { "threshold", &threshold }, { "sharpness", &sharpness }, { "attack", &attack },
        { "release", &release }, { "punch", &punch }, { "broad", &broad }, { "guard", &guard }, { "maxcut", &maxcut },
        { "mix", &mix }, { "trim", &trim }, { "inMeter", &inMeter }, { "outMeter", &outMeter }, { "display", &display },
        { "presetBox", &presetBox }, { "prev", &prevPreset }, { "next", &nextPreset }, { "save", &savePreset },
        { "live", &modeLive }, { "standard", &modeStd }, { "hires", &modeHi }, { "delta", &deltaBtn },
        { "bypass", &bypassBtn }, { "analyser", &analyserBtn } };
    for (auto& p : parts) if (id == p.first) return p.second;
    return nullptr;
}

void MuziTameEditor::setAnalyserQuality (int q)
{
    display.quality = juce::jlimit (0, 2, q);
    proc.apvts.state.setProperty ("analyser", display.quality, nullptr);
    analyserBtn.setButtonText (juce::StringArray { "ANALYSER: FULL", "ANALYSER: LIGHT", "ANALYSER: OFF" }[display.quality]);
    display.repaint();
}

void MuziTameEditor::timerCallback()
{
    // The timer runs at 60 Hz and the analyser and the meters take turns, each at 30 fps.
    // If both changed in the same frame, macOS would merge them into one rectangle and
    // redraw everything between them (every knob) each frame; taking turns, it redraws
    // only the part that changed.
    ++frameCount;
    if ((frameCount & 1) == 0)
    {
        // Full redraws at 30 fps, Light at 15 fps, Off shows only the cut curve at 10 fps
        const int every = display.quality == 0 ? 1 : display.quality == 1 ? 2 : 3;
        if ((frameCount / 2) % every == 0) display.update();
        return;
    }
    inMeter.tick();
    outMeter.tick();
    if (proc.presetVersion.load() != shownPresetVersion || (frameCount % 8) == 1) refreshPresetBox();   // name, and * when changed
    if (auto t = footerText(); t != shownFooter) { shownFooter = t; repaint (footer); }
}

void MuziTameEditor::refreshForSnapshot()
{
    for (int i = 0; i < 40; ++i) { display.update(); inMeter.tick(); outMeter.tick(); }
}

juce::Rectangle<int> MuziTameEditor::sc (float x, float y, float w, float h) const
{
    return juce::Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt();
}

void MuziTameEditor::resized()
{
    s = (float) getWidth() / kBaseW;
    lnf.scale = s;
    display.textScale = inMeter.textScale = outMeter.textScale = s;
    for (auto* k : { &depth, &threshold, &sharpness, &attack, &release, &punch, &broad, &guard, &maxcut, &mix, &trim })
        k->textScale = s;

    header = sc (0, 0, kBaseW, 56);
    footer = sc (0, 574, kBaseW, 26);
    analyserBtn.setBounds (sc (kBaseW * 0.5f - 66, 577, 132, 20));

    prevPreset.setBounds (sc (268, 14, 28, 28));
    presetBox.setBounds (sc (300, 14, 220, 28));
    nextPreset.setBounds (sc (524, 14, 28, 28));
    savePreset.setBounds (sc (556, 14, 54, 28));
    modeLive.setBounds (sc (620, 14, 56, 28));
    modeStd.setBounds (sc (676, 14, 90, 28));
    modeHi.setBounds (sc (766, 14, 64, 28));
    deltaBtn.setBounds (sc (836, 14, 62, 28));
    bypassBtn.setBounds (sc (912, 14, 28, 28));

    display.editorHeight = getHeight();
    display.setBounds (sc (16, 60, 928, 300));

    panelDyn  = sc (16, 370, 230, 200);
    panelTame = sc (254, 370, 320, 200);
    panelChar = sc (582, 370, 232, 200);
    panelOut  = sc (822, 370, 122, 200);

    attack.setBounds (sc (22, 412, 72, 124));
    release.setBounds (sc (94, 412, 72, 124));
    punch.setBounds (sc (166, 412, 72, 124));

    threshold.setBounds (sc (262, 412, 88, 124));
    depth.setBounds (sc (350, 394, 134, 168));
    sharpness.setBounds (sc (484, 412, 88, 124));

    broad.setBounds (sc (586, 412, 74, 124));
    guard.setBounds (sc (658, 412, 84, 124));
    maxcut.setBounds (sc (740, 412, 74, 124));

    mix.setBounds (sc (824, 394, 70, 86));
    trim.setBounds (sc (824, 480, 70, 86));
    inMeter.setBounds (sc (894, 398, 24, 162));
    outMeter.setBounds (sc (918, 398, 24, 162));

    for (auto* k : { &depth, &threshold, &sharpness, &attack, &release, &punch, &broad, &guard, &maxcut, &mix, &trim })
        k->resized();
}

juce::String MuziTameEditor::footerText() const
{
    const double rate = proc.getCurrentSampleRate();
    const double ms = rate > 0 ? 1000.0 * proc.getLatencySamples() / rate : 0.0;
    return "Latency " + juce::String (ms, 0) + " ms   |   " + juce::String (rate / 1000.0, 1) + " kHz";
}

void MuziTameEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg (col::bg0.brighter (0.05f), 0, 0, col::bg0, 0, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // logo mark: a resonant peak and its tamed version
    const auto m = sc (18, 14, 30, 28).toFloat();
    juce::Path raw, tamed;
    raw.startNewSubPath (m.getX(), m.getBottom() - 6 * s);
    raw.lineTo (m.getX() + 9 * s, m.getBottom() - 6 * s);
    raw.lineTo (m.getCentreX(), m.getY());
    raw.lineTo (m.getRight() - 9 * s, m.getBottom() - 6 * s);
    raw.lineTo (m.getRight(), m.getBottom() - 6 * s);
    tamed.startNewSubPath (m.getX(), m.getBottom() - 6 * s);
    tamed.cubicTo (m.getX() + 9 * s, m.getBottom() - 6 * s, m.getCentreX() - 4 * s, m.getY() + 12 * s, m.getCentreX(), m.getY() + 12 * s);
    tamed.cubicTo (m.getCentreX() + 4 * s, m.getY() + 12 * s, m.getRight() - 9 * s, m.getBottom() - 6 * s, m.getRight(), m.getBottom() - 6 * s);
    g.setColour (col::faint);
    g.strokePath (raw, juce::PathStrokeType (1.4f * s, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.setColour (col::accent);
    g.strokePath (tamed, juce::PathStrokeType (2.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (col::text);
    g.setFont (font (19.0f * s, true, 0.14f));
    g.drawText ("MUZI TAME", sc (56, 11, 200, 22), juce::Justification::centredLeft);
    g.setColour (col::faint);
    g.setFont (font (9.5f * s, true, 0.22f));
    g.drawText ("RESONANCE SUPPRESSOR", sc (57, 32, 200, 14), juce::Justification::centredLeft);

    // control panels
    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        const auto rf = r.toFloat();
        g.setColour (col::bg1);
        g.fillRoundedRectangle (rf, 10.0f * s);
        g.setColour (col::line);
        g.drawRoundedRectangle (rf.reduced (0.5f), 10.0f * s, 1.0f);
        g.setColour (col::faint);
        g.setFont (font (9.5f * s, true, 0.22f));
        g.drawText (title, r.withHeight ((int) (30 * s)).reduced ((int) (12 * s), 0), juce::Justification::centredLeft);
    };
    panel (panelDyn, "DYNAMICS");
    panel (panelTame, "TAME");
    panel (panelChar, "CHARACTER");
    panel (panelOut, "OUTPUT");

    // footer
    const double rate = proc.getCurrentSampleRate();
    const double ms = rate > 0 ? 1000.0 * proc.getLatencySamples() / rate : 0.0;
    g.setColour (col::faint);
    g.setFont (font (10.5f * s));
    juce::ignoreUnused (ms);
    g.drawText (footerText(), footer.reduced ((int) (18 * s), 0), juce::Justification::centredLeft);
    g.drawText ("v" JucePlugin_VersionString "   |   double-click any control to reset", footer.reduced ((int) (18 * s), 0), juce::Justification::centredRight);

   #if (defined (__clang__) || defined (__GNUC__)) && ! defined (__OPTIMIZE__)
    // An unoptimised (-O0) build runs the DSP and the analyser several times slower
    // and makes the whole computer stutter. Say so loudly instead of failing silently.
    g.setColour (juce::Colour (0xffff5a5a));
    g.setFont (font (11.0f * s).boldened());
    g.drawText ("SLOW UNOPTIMISED BUILD", footer, juce::Justification::centred);
   #endif
}
