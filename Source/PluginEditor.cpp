#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff15171c };
    const juce::Colour panel      { 0xff1d2027 };
    const juce::Colour panelLight { 0xff252932 };
    const juce::Colour grid       { 0xff2a2e37 };
    const juce::Colour text       { 0xffc9ced8 };
    const juce::Colour dimText    { 0xff7d8492 };
    const juce::Colour spectrum   { 0xff4a5568 };
    const juce::Colour accent     { 0xff3fd0c9 };   // reduction, main controls
    const juce::Colour sidechain  { 0xffe0a24a };
    const juce::Colour sensitive  { 0xffb48ce0 };   // detection curve + emphasis point
    const juce::Colour range      { 0xffe47a7a };   // low / high pills
    const juce::Colour tiltColour { 0xff8fd18a };
    const juce::Colour deltaColour{ 0xfff0e6a0 };   // removed signal in delta mode
}

namespace
{
    using Settings = ResonanceSuppressor::Settings;

    juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String formatDb (float db, const char* unit = " dB")
    {
        return (db > 0.04f ? "+" : "") + juce::String (db, 1) + unit;
    }
}

//==============================================================================
SpectrumView::SpectrumView (SenseiSmoothAudioProcessor& p) : processor (p)
{
    for (int i = 0; i < numPoints; ++i)
    {
        pointHz[(size_t) i] = minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
        levelDb[(size_t) i] = sidechainDb[(size_t) i] = outputDb[(size_t) i] = minDb;
    }

    startTimerHz (30);
}

float SpectrumView::xForFreq (float hz) const noexcept
{
    return (float) getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float SpectrumView::freqForX (float x) const noexcept
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth())));
}

float SpectrumView::yForLevel (float db) const noexcept
{
    return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, (float) getHeight(), 0.0f);
}

// The detection curve lives in the middle of the display: +-15 dB -> +-35 % of the height.
float SpectrumView::yForSens (float db) const noexcept
{
    return (float) getHeight() * (0.5f - 0.35f * db / sensRangeDb);
}

float SpectrumView::sensForY (float y) const noexcept
{
    return (0.5f - y / (float) juce::jmax (1, getHeight())) * sensRangeDb / 0.35f;
}

juce::Point<float> SpectrumView::handlePosition (Handle h, const Settings& s) const
{
    auto onCurve = [&] (float hz) { return juce::Point<float> (xForFreq (hz), yForSens (ResonanceSuppressor::sensitivityDb (s, hz))); };

    switch (h)
    {
        case Handle::low:      return onCurve (juce::jmin (s.lowHz, s.highHz));
        case Handle::high:     return onCurve (juce::jmax (s.lowHz, s.highHz));
        case Handle::emphasis: return onCurve (s.emphasisHz);
        case Handle::tilt:     return onCurve (tiltHandleHz);
        case Handle::none:
        default:               return {};
    }
}

SpectrumView::Handle SpectrumView::handleAt (juce::Point<float> p) const
{
    const auto s = processor.readSettings();
    Handle best = Handle::none;
    float bestDist = 14.0f;

    for (auto h : { Handle::emphasis, Handle::tilt, Handle::low, Handle::high })
    {
        const auto pos = handlePosition (h, s);
        // pills are tall: be generous vertically
        const float dx = p.x - pos.x;
        const float dy = (h == Handle::low || h == Handle::high) ? (p.y - pos.y) * 0.5f : p.y - pos.y;
        const float d = std::sqrt (dx * dx + dy * dy);
        if (d < bestDist) { bestDist = d; best = h; }
    }
    return best;
}

juce::StringArray SpectrumView::paramsFor (Handle h) const
{
    switch (h)
    {
        case Handle::low:      return { ParamIDs::lowFreq };
        case Handle::high:     return { ParamIDs::highFreq };
        case Handle::emphasis: return { ParamIDs::emphFreq, ParamIDs::emphGain };
        case Handle::tilt:     return { ParamIDs::tilt };
        case Handle::none:
        default:               return {};
    }
}

juce::String SpectrumView::describe (Handle h, const Settings& s) const
{
    switch (h)
    {
        case Handle::low:      return "Low  " + formatHz (s.lowHz);
        case Handle::high:     return "High  " + formatHz (s.highHz);
        case Handle::emphasis: return "Emphasis  " + formatHz (s.emphasisHz) + "  " + formatDb (s.emphasisDb);
        case Handle::tilt:     return "Tilt  " + formatDb (s.tiltDbPerOct, " dB/oct");
        case Handle::none:
        default:               return {};
    }
}

void SpectrumView::setParam (const char* id, float value)
{
    if (auto* p = processor.getState().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor
                        : h == Handle::tilt ? juce::MouseCursor::UpDownResizeCursor
                        : h == Handle::emphasis ? juce::MouseCursor::DraggingHandCursor
                                                : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    hovered = Handle::none;
    repaint();
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    for (auto& id : paramsFor (dragging))
        if (auto* p = processor.getState().getParameter (id))
            p->beginChangeGesture();
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;

    auto s = processor.readSettings();
    const float hz = freqForX (e.position.x);

    switch (dragging)
    {
        case Handle::low:  setParam (ParamIDs::lowFreq,  juce::jmin (hz, s.highHz)); break;
        case Handle::high: setParam (ParamIDs::highFreq, juce::jmax (hz, s.lowHz));  break;

        case Handle::emphasis:
        {
            const float f = juce::jlimit (100.0f, 12000.0f, hz);
            const float tiltPart = s.tiltDbPerOct * std::log2 (f / 1000.0f);
            setParam (ParamIDs::emphFreq, f);
            setParam (ParamIDs::emphGain, juce::jlimit (-12.0f, 12.0f, sensForY (e.position.y) - tiltPart));
            break;
        }

        case Handle::tilt:
        {
            // y sets the curve value at tiltHandleHz; remove the emphasis share first
            auto noTilt = s;
            noTilt.tiltDbPerOct = 0.0f;
            const float emphPart = ResonanceSuppressor::sensitivityDb (noTilt, tiltHandleHz);
            setParam (ParamIDs::tilt, juce::jlimit (-3.0f, 3.0f, (sensForY (e.position.y) - emphPart) / std::log2 (tiltHandleHz / 1000.0f)));
            break;
        }

        case Handle::none:
        default: break;
    }

    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    for (auto& id : paramsFor (dragging))
        if (auto* p = processor.getState().getParameter (id))
            p->endChangeGesture();

    dragging = Handle::none;
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (auto& id : paramsFor (handleAt (e.position)))
        if (auto* p = processor.getState().getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }

    repaint();
}

void SpectrumView::timerCallback()
{
    const auto& sup = processor.getSuppressor();
    const int numBins = sup.getNumDisplayBins();
    if (numBins <= 1)
        return;

    const float binHz = (float) (sup.getSampleRate() / sup.getFftSize());
    const float edge  = std::sqrt (pointHz[1] / pointHz[0]);   // half a point spacing (ratio)

    for (int i = 0; i < numPoints; ++i)
    {
        const int a = juce::jlimit (1, numBins - 1, (int) std::floor (pointHz[(size_t) i] / edge / binHz));
        const int b = juce::jlimit (a,  numBins - 1, (int) std::ceil  (pointHz[(size_t) i] * edge / binHz));

        float lvl = -200.0f, sc = -200.0f, red = 0.0f, out = -200.0f;
        for (int k = a; k <= b; ++k)
        {
            lvl = juce::jmax (lvl, sup.getDisplayLevelDb (k));
            sc  = juce::jmax (sc,  sup.getDisplaySidechainDb (k));
            red = juce::jmax (red, sup.getDisplayReductionDb (k));
            out = juce::jmax (out, sup.getDisplayOutputDb (k));
        }

        // fast rise, slow fall for a calmer picture
        auto follow = [] (float& v, float target, float fall) { v = target > v ? target : v + fall * (target - v); };
        follow (levelDb[(size_t) i],     lvl, 0.25f);
        follow (outputDb[(size_t) i],    out, 0.25f);
        follow (sidechainDb[(size_t) i], sc,  0.25f);
        follow (reductionDb[(size_t) i], red, 0.35f);
    }

    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    const auto s = processor.readSettings();
    const bool sidechainMode = s.detect == ResonanceSuppressor::DetectSource::sidechain;

    g.setColour (Colours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);

    // frequency grid + labels (top)
    g.setFont (juce::FontOptions (11.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (hz);
        g.setColour (Colours::grid);
        g.drawVerticalLine ((int) x, 0.0f, h);
        g.setColour (Colours::dimText);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x + 3.0f, h - 16.0f, 40.0f, 14.0f), juce::Justification::left);
    }

    // neutral line of the detection curve
    g.setColour (Colours::grid.brighter (0.15f));
    g.drawHorizontalLine ((int) yForSens (0.0f), 0.0f, w);

    // shade the area outside the processing range
    const float xLo = juce::jlimit (0.0f, w, xForFreq (juce::jmin (s.lowHz, s.highHz)));
    const float xHi = juce::jlimit (0.0f, w, xForFreq (juce::jmax (s.lowHz, s.highHz)));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, xLo, h));
    g.fillRect (juce::Rectangle<float> (xHi, 0.0f, w - xHi, h));

    auto curve = [&] (const std::array<float, numPoints>& values, auto&& yFor)
    {
        juce::Path p;
        for (int i = 0; i < numPoints; ++i)
        {
            const float x = xForFreq (pointHz[(size_t) i]), y = yFor (values[(size_t) i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    // main spectrum (filled). In delta mode the input is only a faint outline and
    // the removed signal is shown filled instead.
    {
        auto filled = [&] (const std::array<float, numPoints>& values)
        {
            auto p = curve (values, [this] (float v) { return yForLevel (v); });
            p.lineTo (w, h);
            p.lineTo (0.0f, h);
            p.closeSubPath();
            return p;
        };

        if (s.delta)
        {
            g.setColour (Colours::spectrum.withAlpha (0.18f));
            g.fillPath (filled (levelDb));
            g.setColour (Colours::spectrum.brighter (0.4f).withAlpha (0.6f));
            g.strokePath (curve (levelDb, [this] (float v) { return yForLevel (v); }), juce::PathStrokeType (1.0f));

            g.setColour (Colours::deltaColour.withAlpha (0.45f));
            g.fillPath (filled (outputDb));
            g.setColour (Colours::deltaColour);
            g.strokePath (curve (outputDb, [this] (float v) { return yForLevel (v); }), juce::PathStrokeType (1.4f));
        }
        else
        {
            g.setColour (Colours::spectrum.withAlpha (0.5f));
            g.fillPath (filled (levelDb));
        }
    }

    // sidechain spectrum + its threshold
    if (sidechainMode)
    {
        g.setColour (Colours::sidechain.withAlpha (0.85f));
        g.strokePath (curve (sidechainDb, [this] (float v) { return yForLevel (v); }), juce::PathStrokeType (1.3f));

        const float yThr = yForLevel (ResonanceSuppressor::sidechainThresholdDb (s.threshold));
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (Colours::sidechain.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (0.0f, yThr, w, yThr), dashes, 2, 1.0f);
    }

    // reduction, hanging from the top
    {
        auto red = curve (reductionDb, [h] (float v)
        {
            return juce::jmap (juce::jlimit (0.0f, maxRedDb, v), 0.0f, maxRedDb, 0.0f, h * 0.8f);
        });

        juce::Path fill (red);
        fill.lineTo (w, 0.0f);
        fill.lineTo (0.0f, 0.0f);
        fill.closeSubPath();
        g.setColour (Colours::accent.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (Colours::accent.withAlpha (0.9f));
        g.strokePath (red, juce::PathStrokeType (1.5f));
    }

    // detection curve (editable)
    {
        juce::Path sens;
        for (int i = 0; i < numPoints; ++i)
        {
            const float hz = pointHz[(size_t) i];
            const float y = yForSens (juce::jlimit (-sensRangeDb * 1.3f, sensRangeDb * 1.3f, ResonanceSuppressor::sensitivityDb (s, hz)));
            if (i == 0) sens.startNewSubPath (xForFreq (hz), y); else sens.lineTo (xForFreq (hz), y);
        }
        g.setColour (Colours::sensitive.withAlpha (0.9f));
        g.strokePath (sens, juce::PathStrokeType (2.0f));
    }

    // handles
    auto drawHandle = [&] (Handle hd, juce::Colour c)
    {
        const auto pos = handlePosition (hd, s);
        const bool active = hd == hovered || hd == dragging;
        const float grow = active ? 1.25f : 1.0f;

        juce::Rectangle<float> r;
        if (hd == Handle::low || hd == Handle::high)
            r = juce::Rectangle<float> (8.0f * grow, 22.0f * grow).withCentre (pos);
        else
            r = juce::Rectangle<float> (13.0f * grow, 13.0f * grow).withCentre (pos);

        if (active)
        {
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (r.expanded (5.0f), r.getWidth() * 0.5f + 5.0f);
        }

        g.setColour (c);
        g.fillRoundedRectangle (r, r.getWidth() * 0.5f);
        g.setColour (Colours::background);
        g.drawRoundedRectangle (r, r.getWidth() * 0.5f, 1.5f);
    };

    drawHandle (Handle::low,      Colours::range);
    drawHandle (Handle::high,     Colours::range);
    drawHandle (Handle::tilt,     Colours::tiltColour);
    drawHandle (Handle::emphasis, Colours::sensitive);

    // read-out for the active handle, or a hint
    const auto shown = dragging != Handle::none ? dragging : hovered;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    if (shown != Handle::none)
    {
        const auto pos  = handlePosition (shown, s);
        const auto text = describe (shown, s);
        const float tw  = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 16.0f;
        auto box = juce::Rectangle<float> (tw, 22.0f).withCentre (pos.translated (0.0f, pos.y > 40.0f ? -26.0f : 26.0f));
        box = box.constrainedWithin (bounds.reduced (4.0f));
        g.setColour (Colours::background.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (Colours::text);
        g.drawText (text, box, juce::Justification::centred);
    }

    // legend / status
    g.setFont (juce::FontOptions (11.0f));
    auto legend = juce::Rectangle<float> (w - 210.0f, 6.0f, 200.0f, 14.0f);
    g.setColour (Colours::accent);
    g.drawText ("Reduction", legend, juce::Justification::right);
    legend.translate (0.0f, 14.0f);
    g.setColour (Colours::sensitive);
    g.drawText ("Sensitivity", legend, juce::Justification::right);

    if (s.delta)
    {
        legend.translate (0.0f, 14.0f);
        g.setColour (Colours::deltaColour);
        g.drawText ("Delta (removed signal)", legend, juce::Justification::right);
    }

    if (sidechainMode)
    {
        legend.translate (0.0f, 14.0f);
        const bool connected = processor.isSidechainConnected();
        g.setColour (connected ? Colours::sidechain : juce::Colour (0xffe05a5a));
        g.drawText (connected ? "Sidechain" : "No sidechain signal - check routing",
                    legend.withLeft (w - 300.0f), juce::Justification::right);
    }
}

//==============================================================================
SenseiSmoothLookAndFeel::SenseiSmoothLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId,            juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,               Colours::text);
    setColour (juce::ToggleButton::textColourId,        Colours::text);
    setColour (juce::ToggleButton::tickColourId,        Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::dimText);
    setColour (juce::ComboBox::backgroundColourId,      Colours::panelLight);
    setColour (juce::ComboBox::outlineColourId,         Colours::grid);
    setColour (juce::ComboBox::textColourId,            Colours::text);
    setColour (juce::ComboBox::arrowColourId,           Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId,     Colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panelLight);
    setColour (juce::TooltipWindow::textColourId,       Colours::text);
    setColour (juce::TooltipWindow::outlineColourId,    Colours::grid);
}

juce::Font SenseiSmoothLookAndFeel::getLabelFont (juce::Label& label)
{
    // Slider value boxes take their size from the slider's "fontSize" property.
    if (auto* slider = dynamic_cast<juce::Slider*> (label.getParentComponent()))
        if (slider->getProperties().contains ("fontSize"))
            return juce::FontOptions ((float) slider->getProperties()["fontSize"], juce::Font::bold);

    return LookAndFeel_V4::getLabelFont (label);
}

void SenseiSmoothLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float track = juce::jlimit (2.5f, 7.0f, radius * 0.09f);
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // bipolar parameters draw their arc from the zero position
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                    : startAngle;

    const float arcR = radius - track * 0.5f;
    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::grid);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                         juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
    g.setColour (colour);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius - track * 2.2f;
    juce::ColourGradient grad (Colours::panelLight.brighter (0.25f), centre.x, centre.y - knobR,
                               Colours::panel, centre.x, centre.y + knobR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    const float pw = juce::jmax (2.5f, knobR * 0.08f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pw * 0.5f, -knobR + knobR * 0.12f, pw, knobR * 0.45f, pw * 0.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void SenseiSmoothLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    g.setColour (Colours::panelLight);
    g.fillRoundedRectangle (r, 5.0f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroX = bipolar ? r.getX() + (float) slider.valueToProportionOfLength (0.0) * r.getWidth() : r.getX();
    auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zeroX, sliderPos), r.getY(),
                                                            juce::jmax (zeroX, sliderPos), r.getBottom());

    g.saveState();
    g.reduceClipRegion (fill.toNearestInt());
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (0.35f));
    g.fillRoundedRectangle (r, 5.0f);
    g.restoreState();

    g.setColour (Colours::grid);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

//==============================================================================
SenseiSmoothAudioProcessorEditor::SenseiSmoothAudioProcessorEditor (SenseiSmoothAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), spectrum (p)
{
    addAndMakeVisible (spectrum);

    addControl (amount,    ParamIDs::depth,       "Amount",    Size::large,
                "Maximum reduction per frequency. Start here.");
    addControl (precision, ParamIDs::sharpness,   "Precision", Size::medium,
                "Width of the cuts: low = broad and smooth, high = narrow and surgical.");
    addControl (threshold, ParamIDs::selectivity, "Threshold", Size::medium,
                "How much a resonance must stick out before it is reduced.\n"
                "Sidechain mode: level the sidechain must exceed (dashed line).");
    addControl (attack,    ParamIDs::attack,      "Attack",    Size::small,
                "How fast the reduction reacts.");
    addControl (release,   ParamIDs::release,     "Release",   Size::small,
                "How fast the reduction lets go.");

    addControl (mix,       ParamIDs::mix,         "Mix",       Size::bar,
                "Blend between dry and processed signal.");
    addControl (output,    ParamIDs::output,      "Out",       Size::bar,
                "Output level.");

    addChoice (detect,   ParamIDs::detect,      "Detect",
               "Input: react to the track itself.\nSidechain: make room for the sidechain signal.");
    addChoice (channels, ParamIDs::channelMode, "Channels",
               "How stereo is processed: linked, L/R separately, or Mid/Side.");

    deltaButton.setTooltip ("Listen only to what is being removed.");
    addAndMakeVisible (deltaButton);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getState(), ParamIDs::delta, deltaButton);

    setResizable (true, true);
    setResizeLimits (780, 480, 1800, 1100);
    setSize (960, 580);

    // Set last so the change propagates to all children (sliders rebuild their
    // text boxes with the custom colours and fonts).
    setLookAndFeel (&lookAndFeel);
}

SenseiSmoothAudioProcessorEditor::~SenseiSmoothAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SenseiSmoothAudioProcessorEditor::addControl (Control& c, const char* paramId, const juce::String& name,
                                            Size size, const juce::String& tooltip)
{
    auto& s = c.slider;

    if (size == Size::bar)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 0, 0);
        s.getProperties().set ("fontSize", 13.0f);
    }
    else
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        const float fontSize = size == Size::large ? 18.0f : size == Size::medium ? 14.0f : 12.0f;
        s.getProperties().set ("fontSize", fontSize);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, (int) fontSize + 8);
    }

    s.setColour (juce::Slider::rotarySliderFillColourId, size == Size::small ? Colours::accent.withSaturation (0.35f) : Colours::accent);
    s.setTooltip (tooltip);
    s.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (s);

    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (size == Size::bar ? juce::Justification::centredRight : juce::Justification::centred);
    c.label.setFont (juce::FontOptions (size == Size::large ? 15.0f : size == Size::small ? 11.0f : 13.0f,
                                        size == Size::small ? juce::Font::plain : juce::Font::bold));
    c.label.setColour (juce::Label::textColourId, size == Size::large || size == Size::medium ? Colours::text : Colours::dimText);
    c.label.setTooltip (tooltip);
    addAndMakeVisible (c.label);

    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getState(), paramId, s);
}

void SenseiSmoothAudioProcessorEditor::addChoice (Choice& c, const char* paramId, const juce::String& name, const juce::String& tooltip)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.getState().getParameter (paramId)))
        c.box.addItemList (param->choices, 1);

    c.box.setTooltip (tooltip);
    addAndMakeVisible (c.box);

    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredRight);
    c.label.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (c.label);

    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getState(), paramId, c.box);
}

void SenseiSmoothAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SENSEI SMOOTH", 16, 8, 240, 30, juce::Justification::centredLeft);

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("dynamic resonance suppressor  //  vierminus audio", 196, 8, 300, 30, juce::Justification::centredLeft);

    // side panel
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (panelArea.toFloat(), 8.0f);

    g.setColour (Colours::grid);
    const int sep1 = precision.label.getY() - 8, sep2 = attack.label.getY() - 8;
    g.drawHorizontalLine (sep1, (float) panelArea.getX() + 14.0f, (float) panelArea.getRight() - 14.0f);
    g.drawHorizontalLine (sep2, (float) panelArea.getX() + 14.0f, (float) panelArea.getRight() - 14.0f);

    // bottom bar + usage hint
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (bottomBar.toFloat(), 8.0f);

    g.setColour (Colours::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Drag the points in the display to shape the detection  -  double-click resets",
                bottomBar.withTrimmedLeft (120).withRight (mix.label.getX() - 12), juce::Justification::centredLeft);
}

void SenseiSmoothAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    // header: title (painted) + mode selectors
    auto header = area.removeFromTop (36);
    channels.box.setBounds (header.removeFromRight (130).reduced (0, 4));
    channels.label.setBounds (header.removeFromRight (72));
    header.removeFromRight (10);
    detect.box.setBounds (header.removeFromRight (115).reduced (0, 4));
    detect.label.setBounds (header.removeFromRight (60));
    area.removeFromTop (6);

    // bottom bar: delta, mix, output
    bottomBar = area.removeFromBottom (44);
    area.removeFromBottom (10);
    {
        auto bar = bottomBar.reduced (12, 9);
        output.slider.setBounds (bar.removeFromRight (120));
        output.label.setBounds (bar.removeFromRight (40).withTrimmedRight (6));
        bar.removeFromRight (16);
        mix.slider.setBounds (bar.removeFromRight (150));
        mix.label.setBounds (bar.removeFromRight (40).withTrimmedRight (6));
        deltaButton.setBounds (bar.removeFromLeft (90));
    }

    // side panel with a clear size hierarchy
    panelArea = area.removeFromRight (230);
    area.removeFromRight (10);
    spectrum.setBounds (area);

    auto panel = panelArea.reduced (12, 10);
    const int h = panel.getHeight();

    auto big = panel.removeFromTop ((int) (h * 0.46f));
    amount.label.setBounds (big.removeFromTop (22));
    amount.slider.setBounds (big.reduced (8, 0));

    panel.removeFromTop (14);
    auto mid = panel.removeFromTop ((int) (h * 0.29f));
    {
        auto left = mid.removeFromLeft (mid.getWidth() / 2);
        precision.label.setBounds (left.removeFromTop (20));
        precision.slider.setBounds (left.reduced (4, 0));
        threshold.label.setBounds (mid.removeFromTop (20));
        threshold.slider.setBounds (mid.reduced (4, 0));
    }

    panel.removeFromTop (14);
    {
        auto small = panel;
        auto left = small.removeFromLeft (small.getWidth() / 2);
        attack.label.setBounds (left.removeFromTop (16));
        attack.slider.setBounds (left.reduced (14, 0));
        release.label.setBounds (small.removeFromTop (16));
        release.slider.setBounds (small.reduced (14, 0));
    }
}
