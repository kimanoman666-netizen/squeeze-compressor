#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorDSP.h"

namespace ui
{

namespace col
{
    const juce::Colour bgTop      { 0xff17191e };
    const juce::Colour bgBottom   { 0xff0c0d10 };
    const juce::Colour panel      { 0xff0e1014 };
    const juce::Colour panelEdge  { 0xff252930 };
    const juce::Colour track      { 0xff23262d };
    const juce::Colour text       { 0xffeceef1 };
    const juce::Colour textDim    { 0xff7d8490 };
    const juce::Colour gr         { 0xffff3d71 };
    const juce::Colour clean      { 0xff3fd8ff };
    const juce::Colour fat        { 0xffff6a2b };
    const juce::Colour smooth     { 0xffb57bff };
}

inline juce::Colour accentFor (int mode)
{
    switch (mode)
    {
        case 0:  return col::clean;
        case 2:  return col::smooth;
        default: return col::fat;
    }
}

inline juce::Font font (float h, bool bold = false, float kerning = 0.0f)
{
    auto f = juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    if (kerning > 0.0f)
        f.setExtraKerningFactor (kerning);
    return f;
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff15171b));
        setColour (juce::PopupMenu::textColourId,                  col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff262a31));
        setColour (juce::PopupMenu::highlightedTextColourId,       col::text);
        setColour (juce::ComboBox::textColourId,                   col::text);
        setColour (juce::ComboBox::backgroundColourId,             juce::Colour (0xff15171b));
        setColour (juce::ComboBox::outlineColourId,                col::panelEdge);
        setColour (juce::TextButton::textColourOffId,              col::textDim);
    }

    void setAccent (juce::Colour c)
    {
        accent = c;
        setColour (juce::ComboBox::arrowColourId, c);
        setColour (juce::TextButton::textColourOnId, c);
    }
    juce::Colour getAccent() const { return accent; }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        const bool big = (bool) slider.getProperties().getWithDefault ("big", false);

        const float size = (float) jmin (w, h);
        const auto  c    = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float r    = size * 0.5f - 4.0f;
        const float sw   = r * (big ? 0.085f : 0.10f);
        const float arcR = r - sw;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // track
        Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (col::track);
        g.strokePath (track, PathStrokeType (sw, PathStrokeType::curved, PathStrokeType::rounded));

        // value arc + glow
        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - fromAngle) > 0.01f)
        {
            Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (fromAngle, angle), jmax (fromAngle, angle), true);
            const float glowW[] = { 3.4f, 2.3f, 1.6f };
            const float glowA[] = { 0.05f, 0.09f, 0.16f };
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (accent.withAlpha (glowA[i]));
                g.strokePath (val, PathStrokeType (sw * glowW[i], PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setGradientFill (ColourGradient (accent.brighter (0.35f), c.x - r, c.y + r,
                                               accent, c.x + r, c.y - r, false));
            g.strokePath (val, PathStrokeType (sw, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // body
        const float bodyR = arcR - sw * 1.6f;
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - bodyR - 1.0f, c.y - bodyR + 3.0f, bodyR * 2.0f + 2.0f, bodyR * 2.0f + 2.0f);

        g.setGradientFill (ColourGradient (Colour (0xff30343c), c.x, c.y - bodyR,
                                           Colour (0xff131519), c.x, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), c.x, c.y - bodyR,
                                           Colours::white.withAlpha (0.0f), c.x, c.y + bodyR * 0.3f, false));
        g.drawEllipse (c.x - bodyR + 0.5f, c.y - bodyR + 0.5f, bodyR * 2.0f - 1.0f, bodyR * 2.0f - 1.0f, 1.0f);

        const float faceR = bodyR * 0.80f;
        g.setGradientFill (ColourGradient (Colour (0xff181a1f), c.x, c.y - faceR,
                                           Colour (0xff24272e), c.x, c.y + faceR, false));
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);

        // pointer
        const auto pt = [&] (float rad) { return Point<float> (c.x + rad * std::sin (angle), c.y - rad * std::cos (angle)); };
        Line<float> ptr (pt (bodyR * 0.38f), pt (bodyR * 0.86f));
        const float pw = big ? 3.5f : 2.6f;
        g.setColour (accent.withAlpha (0.25f));
        g.drawLine (ptr, pw * 2.6f);
        g.setColour (accent.brighter (0.2f));
        g.drawLine (ptr, pw);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (juce::Colour (0xff131519));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (box.isMouseOver (true) ? accent.withAlpha (0.6f) : col::panelEdge);
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        juce::Path arrow;
        const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.5f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (accent);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f, true); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return font (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = b.getToggleState();

        g.setColour (on ? accent.withAlpha (down ? 0.28f : 0.16f) : juce::Colour (0xff131519));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? accent.withAlpha (0.85f) : (over ? col::textDim : col::panelEdge));
        g.drawRoundedRectangle (r, rad, 1.0f);

        // LED
        const float d = 6.0f;
        auto led = juce::Rectangle<float> (r.getX() + 11.0f, r.getCentreY() - d * 0.5f, d, d);
        if (on)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (on ? accent : col::track.brighter (0.3f));
        g.fillEllipse (led);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.5f, true, 0.08f); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? accent : col::textDim);
        g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (18), juce::Justification::centred);
    }

private:
    juce::Colour accent { col::fat };
};

//==============================================================================
class Knob : public juce::Slider
{
public:
    Knob (juce::String nameText, bool isBig, juce::String left = {}, juce::String right = {})
        : juce::Slider (RotaryHorizontalVerticalDrag, NoTextBox),
          name (std::move (nameText)), leftLabel (std::move (left)), rightLabel (std::move (right)), big (isBig)
    {
        getProperties().set ("big", big);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                             juce::MathConstants<float>::pi * 2.75f, true);
        setMouseDragSensitivity (big ? 320 : 240);
        setVelocityModeParameters (1.0, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float textH = big ? 40.0f : 34.0f;
        auto textArea = b.removeFromBottom (textH);
        const float side = juce::jmin (b.getWidth(), b.getHeight());
        auto knobArea = b.withSizeKeepingCentre (side, side);

        const auto rp = getRotaryParameters();
        getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                           (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                           (float) valueToProportionOfLength (getValue()),
                                           rp.startAngleRadians, rp.endAngleRadians, *this);

        const auto accent = static_cast<LookAndFeel&> (getLookAndFeel()).getAccent();

        if (leftLabel.isNotEmpty() || rightLabel.isNotEmpty())
        {
            g.setFont (font (9.0f, true, 0.1f));
            g.setColour (col::textDim.withAlpha (0.8f));
            auto lr = juce::Rectangle<float> (0.0f, knobArea.getBottom() - 10.0f, (float) getWidth(), 12.0f);
            g.drawText (leftLabel,  lr, juce::Justification::centredLeft);
            g.drawText (rightLabel, lr, juce::Justification::centredRight);
        }

        g.setFont (font (big ? 13.0f : 11.0f, true, 0.15f));
        g.setColour (big ? accent : col::textDim);
        g.drawText (name, textArea.removeFromTop (textH * 0.45f), juce::Justification::centred);

        g.setFont (font (big ? 16.0f : 13.0f, true));
        g.setColour (col::text);
        g.drawText (getTextFromValue (getValue()), textArea, juce::Justification::centredTop);
    }

private:
    juce::String name, leftLabel, rightLabel;
    bool big = false;
};

//==============================================================================
class ModeSwitch : public juce::Component
{
public:
    ModeSwitch (juce::RangedAudioParameter& param)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
        {
            index = juce::jlimit (0, 2, juce::roundToInt (v));
            repaint();
            if (onChange) onChange (index);
        });
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onChange;
    int getIndex() const { return index; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f)));
        attachment->setValueAsCompleteGesture ((float) i);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.0f)));
        if (i != hover) { hover = i; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff111317));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::panelEdge);
        g.drawRoundedRectangle (b, 12.0f, 1.0f);

        static const char* names[] = { "CLEAN", "FAT", "SMOOTH" };
        static const char* subs[]  = { "transparent", "aggressive  /  saturated", "opto  /  silky" };

        const float segW = b.getWidth() / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            auto seg = juce::Rectangle<float> (b.getX() + segW * (float) i, b.getY(), segW, b.getHeight()).reduced (4.0f);
            const auto ac = accentFor (i);
            const bool sel = i == index;

            if (sel)
            {
                g.setColour (ac.withAlpha (0.18f));
                g.fillRoundedRectangle (seg.expanded (3.0f), 11.0f);
                g.setGradientFill (juce::ColourGradient (ac.brighter (0.25f), seg.getX(), seg.getY(),
                                                         ac.darker (0.15f), seg.getRight(), seg.getBottom(), false));
                g.fillRoundedRectangle (seg, 9.0f);
            }
            else if (i == hover)
            {
                g.setColour (juce::Colours::white.withAlpha (0.04f));
                g.fillRoundedRectangle (seg, 9.0f);
            }

            auto top = seg.withTrimmedBottom (seg.getHeight() * 0.42f);
            auto bot = seg.withTrimmedTop (seg.getHeight() * 0.56f);

            g.setFont (font (14.0f, true, 0.18f));
            g.setColour (sel ? juce::Colour (0xff0c0d10) : col::text.withAlpha (i == hover ? 0.95f : 0.7f));
            g.drawText (names[i], top, juce::Justification::centredBottom);

            g.setFont (font (9.5f));
            g.setColour (sel ? juce::Colour (0xff0c0d10).withAlpha (0.7f) : col::textDim.withAlpha (0.8f));
            g.drawText (subs[i], bot, juce::Justification::centredTop);

            if (! sel)
            {
                g.setColour (ac.withAlpha (0.8f));
                g.fillEllipse (seg.getX() + 10.0f, seg.getCentreY() - 3.0f, 6.0f, 6.0f);
            }
        }
    }

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 1, hover = -1;
};

//==============================================================================
class HistoryDisplay : public juce::Component
{
public:
    static constexpr int N = 320;

    HistoryDisplay()
    {
        std::fill (std::begin (inH),  std::end (inH),  -100.0f);
        std::fill (std::begin (outH), std::end (outH), -100.0f);
        std::fill (std::begin (grH),  std::end (grH),  0.0f);
    }

    void push (float inDb, float outDb, float grDb)
    {
        inH[write] = inDb; outH[write] = outDb; grH[write] = grDb;
        write = (write + 1) % N;
    }

    void setState (float threshold, float grNowDb, juce::Colour ac)
    {
        thr = threshold; grNow = grNowDb; accent = ac;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::panelEdge);
        g.drawRoundedRectangle (b, 12.0f, 1.0f);

        auto plot = b.reduced (12.0f, 12.0f).withTrimmedLeft (22.0f).withTrimmedRight (22.0f);
        const float top = plot.getY(), bottom = plot.getBottom();
        auto dbToY = [&] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, bottom, top); };
        auto grToY = [&] (float gr) { return jmap (jlimit (0.0f, 24.0f, gr), 0.0f, 24.0f, top, bottom); };

        // grid
        g.setFont (font (9.0f));
        for (float db : { -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db);
            g.setColour (Colours::white.withAlpha (0.045f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (col::textDim.withAlpha (0.55f));
            g.drawText (String ((int) db), Rectangle<float> (b.getX() + 4.0f, y - 6.0f, 26.0f, 12.0f), Justification::centredRight);
        }
        for (float gr : { 6.0f, 12.0f, 18.0f })
        {
            g.setColour (col::gr.withAlpha (0.45f));
            g.drawText (String ((int) gr), Rectangle<float> (plot.getRight() + 2.0f, grToY (gr) - 6.0f, 20.0f, 12.0f), Justification::centredLeft);
        }

        auto xAt = [&] (int i) { return plot.getX() + plot.getWidth() * (float) i / (float) (N - 1); };

        auto levelPath = [&] (const float* data, bool closed)
        {
            Path p;
            for (int i = 0; i < N; ++i)
            {
                const float y = dbToY (data[(write + i) % N]);
                if (i == 0) { if (closed) { p.startNewSubPath (xAt (0), bottom); p.lineTo (xAt (0), y); } else p.startNewSubPath (xAt (0), y); }
                else p.lineTo (xAt (i), y);
            }
            if (closed) { p.lineTo (xAt (N - 1), bottom); p.closeSubPath(); }
            return p;
        };

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        // input (ghost)
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillPath (levelPath (inH, true));
        g.setColour (Colours::white.withAlpha (0.18f));
        g.strokePath (levelPath (inH, false), PathStrokeType (1.0f));

        // output
        g.setGradientFill (ColourGradient (accent.withAlpha (0.30f), 0, top, accent.withAlpha (0.02f), 0, bottom, false));
        g.fillPath (levelPath (outH, true));
        g.setColour (accent);
        g.strokePath (levelPath (outH, false), PathStrokeType (1.6f));

        // gain reduction (from the top)
        Path grFill, grLine;
        grFill.startNewSubPath (xAt (0), top);
        for (int i = 0; i < N; ++i)
        {
            const float y = grToY (grH[(write + i) % N]);
            grFill.lineTo (xAt (i), y);
            if (i == 0) grLine.startNewSubPath (xAt (i), y); else grLine.lineTo (xAt (i), y);
        }
        grFill.lineTo (xAt (N - 1), top);
        grFill.closeSubPath();
        g.setGradientFill (ColourGradient (col::gr.withAlpha (0.35f), 0, top, col::gr.withAlpha (0.05f), 0, grToY (12.0f), false));
        g.fillPath (grFill);
        g.setColour (col::gr.withAlpha (0.25f));
        g.strokePath (grLine, PathStrokeType (4.0f));
        g.setColour (col::gr);
        g.strokePath (grLine, PathStrokeType (1.8f));

        // threshold
        if (thr < -0.5f)
        {
            const float y = dbToY (thr);
            Path dash;
            dash.startNewSubPath (plot.getX(), y);
            dash.lineTo (plot.getRight(), y);
            const float pat[] = { 5.0f, 4.0f };
            PathStrokeType (1.0f).createDashedStroke (dash, dash, pat, 2);
            g.setColour (accent.withAlpha (0.6f));
            g.fillPath (dash);
            g.setFont (font (9.5f, true, 0.06f));
            g.drawText ("THR " + String (thr, 1), Rectangle<float> (plot.getRight() - 90.0f, y - 14.0f, 86.0f, 12.0f), Justification::centredRight);
        }
        g.restoreState();

        // readout
        auto ro = Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 4.0f, 160.0f, 34.0f);
        const String grText = grNow > 0.05f ? String::fromUTF8 ("\xe2\x88\x92") + String (grNow, 1) : String ("0.0");
        const float tw = GlyphArrangement::getStringWidth (font (30.0f, true), grText);
        g.setColour (col::panel.withAlpha (0.78f));
        g.fillRoundedRectangle (ro.withWidth (tw + 62.0f).expanded (6.0f, 2.0f), 8.0f);
        g.setFont (font (30.0f, true));
        g.setColour (col::text);
        g.drawText (grText, ro, Justification::centredLeft);
        g.setFont (font (10.0f, true, 0.12f));
        g.setColour (col::gr);
        g.drawText ("dB  GR", Rectangle<float> (ro.getX() + tw + 6.0f, ro.getY() + 13.0f, 60.0f, 14.0f), Justification::centredLeft);
    }

private:
    float inH[N], outH[N], grH[N];
    int write = 0;
    float thr = 0.0f, grNow = 0.0f;
    juce::Colour accent { col::fat };
};

//==============================================================================
class TransferCurve : public juce::Component
{
public:
    void setState (float t, float r, float k, float levelDb, juce::Colour ac)
    {
        thr = t; ratio = r; knee = k; level = levelDb; accent = ac;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (col::panelEdge);
        g.drawRoundedRectangle (b, 12.0f, 1.0f);

        auto plot = b.reduced (16.0f);
        auto mapX = [&] (float db) { return jmap (db, -60.0f, 0.0f, plot.getX(), plot.getRight()); };
        auto mapY = [&] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, plot.getBottom(), plot.getY()); };

        for (float db : { -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (Colours::white.withAlpha (0.045f));
            g.drawHorizontalLine ((int) mapY (db), plot.getX(), plot.getRight());
            g.drawVerticalLine ((int) mapX (db), plot.getY(), plot.getBottom());
        }

        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

        Path curve, area;
        for (int i = 0; i <= 120; ++i)
        {
            const float x = -60.0f + 60.0f * (float) i / 120.0f;
            const float y = x - sqz::gainReduction (x, thr, ratio, knee);
            const Point<float> p (mapX (x), mapY (y));
            if (i == 0) { curve.startNewSubPath (p); area.startNewSubPath (plot.getX(), plot.getBottom()); area.lineTo (p); }
            else { curve.lineTo (p); area.lineTo (p); }
        }
        area.lineTo (plot.getRight(), plot.getBottom());
        area.closeSubPath();

        g.setGradientFill (ColourGradient (accent.withAlpha (0.18f), 0, plot.getY(), accent.withAlpha (0.0f), 0, plot.getBottom(), false));
        g.fillPath (area);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (curve, PathStrokeType (5.0f));
        g.setColour (accent);
        g.strokePath (curve, PathStrokeType (2.0f));

        if (level > -60.0f)
        {
            const float lx = jlimit (-60.0f, 0.0f, level);
            const Point<float> p (mapX (lx), mapY (lx - sqz::gainReduction (lx, thr, ratio, knee)));
            g.setColour (Colours::white.withAlpha (0.15f));
            g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (p));
            g.setColour (Colours::white);
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (p));
        }

        g.setFont (font (10.0f, true, 0.06f));
        g.setColour (col::text.withAlpha (0.85f));
        const String ratioText = ratio < 1.05f ? String ("1:1") : String (ratio, 1) + ":1";
        g.drawText (ratioText, plot.withHeight (14.0f).translated (4.0f, 2.0f), Justification::centredLeft);
        g.setFont (font (8.5f, true, 0.12f));
        g.setColour (col::textDim.withAlpha (0.7f));
        g.drawText ("IN", Rectangle<float> (plot.getRight() - 20.0f, plot.getBottom() - 13.0f, 18.0f, 12.0f), Justification::centredRight);
    }

private:
    float thr = 0.0f, ratio = 1.0f, knee = 6.0f, level = -100.0f;
    juce::Colour accent { col::fat };
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevel (float db)
    {
        level = std::max (db, level - 1.1f);
        if (db >= peak) { peak = db; holdFrames = 50; }
        else if (--holdFrames < 0) peak = std::max (db, peak - 0.6f);
    }

    void setAccent (juce::Colour c) { accent = c; }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat();
        g.setColour (Colour (0xff15171b));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (col::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        auto inner = b.reduced (2.0f);
        auto dbToY = [&] (float db) { return jmap (jlimit (-60.0f, 3.0f, db), -60.0f, 3.0f, inner.getBottom(), inner.getY()); };

        if (level > -60.0f)
        {
            auto fill = inner.withTop (dbToY (level));
            ColourGradient grad (accent.darker (0.3f), 0, inner.getBottom(), Colour (0xffff3030), 0, inner.getY(), false);
            grad.addColour (0.80, accent.brighter (0.2f));
            grad.addColour (0.90, Colour (0xffffd23f));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 2.0f);
        }

        if (peak > -60.0f)
        {
            g.setColour (peak > -0.1f ? Colour (0xffff3030) : Colours::white.withAlpha (0.9f));
            g.fillRect (inner.getX(), dbToY (peak) - 1.0f, inner.getWidth(), 2.0f);
        }
    }

    float getPeak() const { return peak; }

private:
    float level = -100.0f, peak = -100.0f;
    int holdFrames = 0;
    juce::Colour accent { col::fat };
};

} // namespace ui
