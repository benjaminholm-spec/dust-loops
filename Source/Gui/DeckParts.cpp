#include "DeckParts.h"
#include "Art.h"
#include "DustLookAndFeel.h"
#include "Core/Params.h"

namespace deck
{
using namespace juce;

namespace
{
const Colour ledRed = Art::Colours::ledRed;
const Colour freezeYellow { 0xfff2c230 };
const Colour recRed { 0xffa8261b };

float textWidth (const Font& f, const String& t) { return GlyphArrangement::getStringWidth (f, t); }

Font spaced (Font f, float kerning)
{
    f.setExtraKerningFactor (kerning);
    return f;
}

// A soft red glow: a few widening translucent strokes under the lit shape.
void glow (Graphics& g, const Path& p, Colour c, float width)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.07f));
        g.strokePath (p, PathStrokeType (width * (float) i * 1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void drawLedDome (Graphics& g, Point<float> c, float radius, float brightness)
{
    // Bezel
    g.setColour (Colour (0xff050505));
    g.fillEllipse (c.x - radius * 1.35f, c.y - radius * 1.35f, radius * 2.7f, radius * 2.7f);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - radius * 1.35f, c.y - radius * 1.35f, radius * 2.7f, radius * 2.7f, 0.8f);

    if (brightness > 0.01f)
    {
        ColourGradient halo (ledRed.withAlpha (0.45f * brightness), c.x, c.y, ledRed.withAlpha (0.0f), c.x + radius * 3.2f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - radius * 3.2f, c.y - radius * 3.2f, radius * 6.4f, radius * 6.4f);
    }

    const auto body = Colour (0xff3a0a07).interpolatedWith (ledRed, brightness);
    ColourGradient dome (body.brighter (0.6f * brightness + 0.1f), c.x - radius * 0.3f, c.y - radius * 0.4f,
                         body.darker (0.5f), c.x + radius, c.y + radius, true);
    g.setGradientFill (dome);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - radius * 0.5f, c.y - radius * 0.6f, radius * 0.5f, radius * 0.35f);
}

void drawSymbol (Graphics& g, PianoKey::Symbol s, Rectangle<float> a, Colour c)
{
    g.setColour (c);
    const auto cx = a.getCentreX(), cy = a.getCentreY(), h = a.getHeight();
    Path p;
    const auto triangle = [&p, h, cy] (float x, bool right)
    {
        const auto w = h * 0.8f;
        if (right) p.addTriangle (x, cy - h * 0.5f, x, cy + h * 0.5f, x + w, cy);
        else       p.addTriangle (x + w, cy - h * 0.5f, x + w, cy + h * 0.5f, x, cy);
    };
    switch (s)
    {
        case PianoKey::Symbol::rec:         p.addEllipse (cx - h * 0.5f, cy - h * 0.5f, h, h); break;
        case PianoKey::Symbol::play:        triangle (cx - h * 0.35f, true); break;
        case PianoKey::Symbol::rewind:      triangle (cx - h * 0.8f, false); triangle (cx - h * 0.05f, false); break;
        case PianoKey::Symbol::fastForward: triangle (cx - h * 0.75f, true); triangle (cx, true); break;
        case PianoKey::Symbol::stop:        p.addRectangle (cx - h * 0.45f, cy - h * 0.45f, h * 0.9f, h * 0.9f); break;
        case PianoKey::Symbol::pause:
            p.addRectangle (cx - h * 0.42f, cy - h * 0.5f, h * 0.3f, h);
            p.addRectangle (cx + h * 0.12f, cy - h * 0.5f, h * 0.3f, h);
            break;
    }
    g.fillPath (p);
}

// 7-segment encodings: bit 0 = a (top), 1 = b, 2 = c, 3 = d, 4 = e, 5 = f, 6 = g (middle).
uint8 segmentsFor (juce_wchar ch)
{
    switch (ch)
    {
        case '0': case 'O': return 0x3f;  case '1': return 0x06;  case '2': return 0x5b;  case '3': return 0x4f;
        case '4': return 0x66;  case '5': case 'S': case 's': return 0x6d;  case '6': return 0x7d;  case '7': return 0x07;
        case '8': return 0x7f;  case '9': return 0x6f;  case '-': return 0x40;  case 'A': case 'a': return 0x77;
        case 'b': case 'B': return 0x7c;  case 'C': return 0x39;  case 'c': return 0x58;  case 'd': case 'D': return 0x5e;
        case 'E': case 'e': return 0x79;  case 'F': case 'f': return 0x71;  case 'H': return 0x76;  case 'h': return 0x74;
        case 'L': case 'l': return 0x38;  case 'n': case 'N': return 0x54;  case 'o': return 0x5c;  case 'P': case 'p': return 0x73;
        case 'r': case 'R': return 0x50;  case 't': case 'T': return 0x78;  case 'U': return 0x3e;  case 'u': return 0x1c;
        case 'y': case 'Y': return 0x6e;  case 'X': case 'x': return 0x76;  case 'I': case 'i': return 0x06;
        default: return 0;
    }
}

Path segmentPath (int seg, Rectangle<float> cell)
{
    // Slanted segments built on a unit cell, then mapped into place.
    const auto w = cell.getWidth(), h = cell.getHeight(), t = w * 0.17f, slant = w * 0.12f;
    const auto pt = [&] (float x, float y) { return Point<float> (cell.getX() + x + slant * (1.0f - y / h), cell.getY() + y); };
    Path p;
    const auto horiz = [&] (float y)
    {
        p.startNewSubPath (pt (t * 0.9f, y));
        p.lineTo (pt (t * 1.4f, y - t * 0.5f)); p.lineTo (pt (w - t * 1.4f, y - t * 0.5f));
        p.lineTo (pt (w - t * 0.9f, y)); p.lineTo (pt (w - t * 1.4f, y + t * 0.5f)); p.lineTo (pt (t * 1.4f, y + t * 0.5f));
        p.closeSubPath();
    };
    const auto vert = [&] (float x, float y0, float y1)
    {
        p.startNewSubPath (pt (x, y0 + t * 0.4f));
        p.lineTo (pt (x + t * 0.5f, y0 + t * 0.9f)); p.lineTo (pt (x + t * 0.5f, y1 - t * 0.9f));
        p.lineTo (pt (x, y1 - t * 0.4f)); p.lineTo (pt (x - t * 0.5f, y1 - t * 0.9f)); p.lineTo (pt (x - t * 0.5f, y0 + t * 0.9f));
        p.closeSubPath();
    };
    switch (seg)
    {
        case 0: horiz (t * 0.5f); break;
        case 1: vert (w - t * 0.5f, t * 0.5f, h * 0.5f); break;
        case 2: vert (w - t * 0.5f, h * 0.5f, h - t * 0.5f); break;
        case 3: horiz (h - t * 0.5f); break;
        case 4: vert (t * 0.5f, h * 0.5f, h - t * 0.5f); break;
        case 5: vert (t * 0.5f, t * 0.5f, h * 0.5f); break;
        case 6: horiz (h * 0.5f); break;
        default: break;
    }
    return p;
}
} // namespace

//==============================================================================
void drawLed (Graphics& g, Point<float> centre, float radius, float brightness) { drawLedDome (g, centre, radius, brightness); }

void drawDymoLabel (Graphics& g, Point<float> centre, const String& text, float height, int seed)
{
    Random r (seed);
    const auto font = spaced (Art::silk (height * 0.66f), 0.16f);
    const auto upper = text.toUpperCase();
    const auto w = textWidth (font, upper) + height * 0.9f;
    const auto area = Rectangle<float> (w, height).withCentre (centre);

    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation ((r.nextFloat() - 0.5f) * 0.05f, centre.x, centre.y));

    g.setColour (Colours::black.withAlpha (0.5f)); // shadow
    g.fillRoundedRectangle (area.translated (0.6f, 1.2f), height * 0.12f);
    ColourGradient tape (Colour (0xff1e1e1f), area.getX(), area.getY(), Colour (0xff070707), area.getX(), area.getBottom(), false);
    g.setGradientFill (tape);
    g.fillRoundedRectangle (area, height * 0.12f);
    g.setColour (Colours::white.withAlpha (0.12f)); // glossy top edge
    g.drawLine (area.getX() + 2.0f, area.getY() + 0.8f, area.getRight() - 2.0f, area.getY() + 0.8f, 0.6f);

    // Embossed letters: stretched plastic goes white, with a dark lower edge
    g.setFont (font);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawText (upper, area.translated (0.0f, 0.8f), Justification::centred, false);
    g.setColour (Colour (0xffeceae4));
    g.drawText (upper, area, Justification::centred, false);
}

//==============================================================================
Knob::Knob (APVTS& state, const String& paramId, const String& labelText, const String& hint, Size s, int knobSeed)
    : label (labelText), size (s), seed (knobSeed)
{
    param = state.getParameter (paramId);
    jassert (param != nullptr);

    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (s == Size::big ? 420 : 260);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("seed", knobSeed);
    if (s == Size::big)
        slider.getProperties().set ("style", "macro");
    addAndMakeVisible (slider);

    attachment = std::make_unique<SliderParameterAttachment> (*param, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { if (onTurn) onTurn (*this); };
    slider.onDragStart = [this] { if (onTurn) onTurn (*this); };

    setHint (*this, hint);
    setHint (slider, hint);
}

String Knob::valueText() const
{
    return label.toUpperCase() + "   " + param->getCurrentValueAsText();
}

void Knob::resized()
{
    const auto labelHeight = size == Size::big ? 24 : 20;
    auto b = getLocalBounds();
    if (size != Size::tiny)
        b.removeFromBottom (labelHeight + 4);
    const auto side = std::min (b.getWidth(), b.getHeight());
    slider.setBounds (b.withSizeKeepingCentre (side, side));
}

void Knob::paint (Graphics& g)
{
    if (size == Size::tiny)
        return;
    const auto labelHeight = size == Size::big ? 22.0f : 18.0f;
    drawDymoLabel (g, { (float) getWidth() * 0.5f, (float) getHeight() - labelHeight * 0.5f - 1.0f }, label, labelHeight, seed);
}

//==============================================================================
PianoKey::PianoKey (APVTS& state, const String& paramId, Symbol s, const String& hint)
    : param (state.getParameter (paramId)), symbol (s)
{
    jassert (param != nullptr);
    setHint (*this, hint);
}

void PianoKey::refresh()
{
    const auto down = held || (isLatched && isLatched());
    if (down != drawnDown)
    {
        drawnDown = down;
        repaint();
    }
}

void PianoKey::mouseDown (const MouseEvent&)
{
    held = true;
    param->beginChangeGesture();
    param->setValueNotifyingHost (1.0f);
    refresh();
}

void PianoKey::mouseUp (const MouseEvent&)
{
    held = false;
    param->setValueNotifyingHost (0.0f);
    param->endChangeGesture();
    refresh();
}

void PianoKey::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    const auto down = drawnDown;
    const auto isRec = symbol == Symbol::rec;
    const auto base = isRec ? recRed : Colour (0xff2a2a2d);

    // Slot in the key bed
    g.setColour (Colour (0xff030303));
    g.fillRoundedRectangle (b, 3.0f);

    // A raised key shows a tall front lip; a pressed key sinks into the slot and the lip almost disappears.
    const auto travel = down ? 11.0f : 0.0f;
    const auto lipHeight = down ? 3.0f : 13.0f;
    auto top = b.reduced (2.0f, 0.0f).withTrimmedTop (2.0f + travel).withTrimmedBottom (lipHeight + 1.0f);
    const auto lip = Rectangle<float> (top.getX(), top.getBottom(), top.getWidth(), lipHeight);

    const auto faceColour = down ? base.darker (0.35f) : base;
    ColourGradient face (faceColour.brighter (down ? 0.05f : 0.3f), top.getX(), top.getY(), faceColour.darker (0.2f),
                         top.getX(), top.getBottom(), false);
    g.setGradientFill (face);
    g.fillRoundedRectangle (top, 2.5f);
    ColourGradient front (base.darker (0.2f), lip.getX(), lip.getY(), base.darker (0.9f), lip.getX(), lip.getBottom(), false);
    g.setGradientFill (front);
    g.fillRoundedRectangle (lip, 2.0f);
    g.setColour (Colours::white.withAlpha (down ? 0.03f : 0.18f));
    g.drawLine (top.getX() + 3.0f, top.getY() + 1.0f, top.getRight() - 3.0f, top.getY() + 1.0f, 1.2f);
    if (down) // shadow from the neighbouring keys
    {
        ColourGradient shade (Colours::black.withAlpha (0.6f), top.getX(), top.getY(), Colours::black.withAlpha (0.0f), top.getX(), top.getY() + 10.0f, false);
        g.setGradientFill (shade);
        g.fillRect (top.withHeight (10.0f));
    }

    // Wear where fingers land
    Random r ((int) symbol + 31);
    for (int i = 0; i < 26; ++i)
    {
        const auto x = top.getX() + top.getWidth() * (0.25f + 0.5f * r.nextFloat());
        const auto y = top.getY() + top.getHeight() * (0.3f + 0.6f * r.nextFloat());
        g.setColour (Colours::white.withAlpha (0.015f + 0.02f * r.nextFloat()));
        g.fillEllipse (x, y, 2.0f + 3.0f * r.nextFloat(), 1.0f + 2.0f * r.nextFloat());
    }

    const auto symbolArea = top.withSizeKeepingCentre (top.getWidth(), std::min (14.0f, top.getHeight() * 0.36f));
    drawSymbol (g, symbol, symbolArea, isRec ? Colour (0xfff3ddd6) : Colour (0xffc8c5bd).withAlpha (down ? 0.7f : 0.95f));
}

//==============================================================================
ParamControl::ParamControl (APVTS& state, const String& paramId)
    : param (state.getParameter (paramId)),
      attachment (*param, [this] (float v) { current = v; repaint(); }, nullptr)
{
    jassert (param != nullptr);
    attachment.sendInitialUpdate();
}

//==============================================================================
FreezeButton::FreezeButton (APVTS& state, const String& hint) : ParamControl (state, ID::tape::freeze)
{
    setHint (*this, hint);
}

void FreezeButton::mouseDown (const MouseEvent&)
{
    setParam (value() >= 0.5f ? 0.0f : 1.0f);
}

void FreezeButton::paint (Graphics& g)
{
    const auto on = value() >= 0.5f;
    const auto labelH = 18.0f;
    auto area = getLocalBounds().toFloat().withTrimmedBottom (labelH + 6.0f);
    const auto c = area.getCentre();
    const auto rad = std::min (area.getWidth(), area.getHeight()) * 0.5f - 6.0f;

    if (on)
    {
        ColourGradient halo (freezeYellow.withAlpha (0.45f), c.x, c.y, freezeYellow.withAlpha (0.0f), c.x + rad * 1.7f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - rad * 1.7f, c.y - rad * 1.7f, rad * 3.4f, rad * 3.4f);
    }

    // Chrome bezel
    const auto bez = rad * 1.18f;
    ColourGradient ring (Colour (0xff8a8a86), c.x, c.y - bez, Colour (0xff1c1c1c), c.x, c.y + bez, false);
    g.setGradientFill (ring);
    g.fillEllipse (c.x - bez, c.y - bez, bez * 2.0f, bez * 2.0f);
    g.setColour (Colours::black);
    g.fillEllipse (c.x - rad * 1.04f, c.y - rad * 1.04f, rad * 2.08f, rad * 2.08f);

    // Cap: goes in a little when engaged, and glows from within
    const auto capR = on ? rad * 0.94f : rad;
    const auto capColour = on ? freezeYellow.brighter (0.25f) : freezeYellow.darker (0.25f);
    ColourGradient cap (capColour.brighter (0.4f), c.x - capR * 0.3f, c.y - capR * 0.5f, capColour.darker (0.45f),
                        c.x + capR * 0.5f, c.y + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (c.x - capR * 0.55f, c.y - capR * 0.7f, capR * 0.7f, capR * 0.35f);

    drawDymoLabel (g, { c.x, (float) getHeight() - labelH * 0.5f - 1.0f }, "Freeze", labelH, 77);
}

//==============================================================================
ToggleSwitch::ToggleSwitch (APVTS& state, const String& paramId, const String& up, const String& down, const String& hint)
    : ParamControl (state, paramId), upLabel (up), downLabel (down)
{
    setHint (*this, hint);
}

void ToggleSwitch::mouseDown (const MouseEvent&)
{
    setParam (value() >= 0.5f ? 0.0f : 1.0f);
}

void ToggleSwitch::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto isDown = value() >= 0.5f;
    const auto labelH = 16.0f;
    const auto font = spaced (Art::silk (13.0f), 0.12f);
    g.setFont (font);
    g.setColour (isDown ? Art::Colours::silkDim : Art::Colours::silk);
    g.drawText (upLabel, b.withHeight (labelH), Justification::centred);
    g.setColour (isDown ? Art::Colours::silk : Art::Colours::silkDim);
    g.drawText (downLabel, b.withTrimmedTop (b.getHeight() - labelH), Justification::centred);

    const auto c = b.getCentre();
    const auto nut = 13.0f;

    // Hex nut
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto a = MathConstants<float>::pi / 3.0f * (float) i;
        const Point<float> p (c.x + nut * std::cos (a), c.y + nut * std::sin (a));
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    ColourGradient metal (Colour (0xffb9b8b2), c.x - nut, c.y - nut, Colour (0xff4a4946), c.x + nut, c.y + nut, false);
    g.setGradientFill (metal);
    g.fillPath (hex);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.strokePath (hex, PathStrokeType (0.8f));
    g.setColour (Colour (0xff1a1a1a));
    g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);

    // Bat handle, tapering toward the tip
    const auto dir = isDown ? 1.0f : -1.0f;
    const auto tip = Point<float> (c.x, c.y + dir * (b.getHeight() * 0.5f - labelH - 6.0f));
    Path bat;
    bat.startNewSubPath (c.x - 3.0f, c.y);
    bat.lineTo (tip.x - 4.5f, tip.y);
    bat.lineTo (tip.x + 4.5f, tip.y);
    bat.lineTo (c.x + 3.0f, c.y);
    bat.closeSubPath();
    ColourGradient chrome (Colour (0xff505050), c.x - 5.0f, 0.0f, Colour (0xff2a2a2a), c.x + 5.0f, 0.0f, false);
    chrome.addColour (0.4, Colour (0xffe8e8e4));
    g.setGradientFill (chrome);
    g.fillPath (bat);
    g.fillEllipse (tip.x - 5.0f, tip.y - 5.0f, 10.0f, 10.0f);
    g.setColour (Colours::white.withAlpha (0.6f));
    g.fillEllipse (tip.x - 2.5f, tip.y - 3.0f, 3.5f, 2.5f);
}

//==============================================================================
LoopLed::LoopLed (APVTS& state, int loopIndex, const String& hint)
    : ParamControl (state, ID::loop::rec[loopIndex]), index (loopIndex)
{
    setHint (*this, hint);
}

void LoopLed::setActivity (float level)
{
    const auto a = std::min (1.0f, level * 3.0f);
    if (std::abs (a - activity) > 0.02f)
    {
        activity = a;
        repaint();
    }
}

void LoopLed::mouseDown (const MouseEvent&)
{
    setParam (value() >= 0.5f ? 0.0f : 1.0f);
}

void LoopLed::paint (Graphics& g)
{
    const auto armed = value() >= 0.5f;
    const auto c = Point<float> ((float) getWidth() * 0.5f, 14.0f);
    drawLedDome (g, c, 6.0f, armed ? 0.55f + 0.45f * activity : 0.06f * activity);
    drawDymoLabel (g, { c.x, (float) getHeight() - 10.0f }, String::charToString ((juce_wchar) ('A' + index)), 18.0f, 300 + index);
}

//==============================================================================
SmallButton::SmallButton (APVTS& state, const String& paramId, const String& labelText, Mode m, int idx, const String& hint)
    : ParamControl (state, paramId), label (labelText), mode (m), choiceIndex (idx)
{
    setHint (*this, hint);
}

bool SmallButton::isLit() const
{
    switch (mode)
    {
        case Mode::toggle:    return value() >= 0.5f;
        case Mode::momentary: return held;
        case Mode::choice:    return (int) std::round (value()) == choiceIndex;
    }
    return false;
}

void SmallButton::mouseDown (const MouseEvent&)
{
    held = true;
    switch (mode)
    {
        case Mode::toggle:    setParam (value() >= 0.5f ? 0.0f : 1.0f); break;
        case Mode::momentary: param->beginChangeGesture(); param->setValueNotifyingHost (1.0f); break;
        case Mode::choice:    setParam ((float) choiceIndex); break;
    }
    if (onClick) onClick();
    repaint();
}

void SmallButton::mouseUp (const MouseEvent&)
{
    held = false;
    if (mode == Mode::momentary)
    {
        param->setValueNotifyingHost (0.0f);
        param->endChangeGesture();
    }
    repaint();
}

namespace
{
void drawSmallKey (Graphics& g, Rectangle<float> b, const String& label, bool lit, bool pressed)
{
    const auto key = drawKeyCap (g, b, pressed);

    const auto ledC = Point<float> (key.getX() + 9.0f, key.getCentreY());
    g.setColour (lit ? ledRed : Colour (0xff3a0a07));
    g.fillEllipse (ledC.x - 3.0f, ledC.y - 3.0f, 6.0f, 6.0f);
    if (lit)
    {
        g.setColour (ledRed.withAlpha (0.25f));
        g.fillEllipse (ledC.x - 6.0f, ledC.y - 6.0f, 12.0f, 12.0f);
    }

    g.setFont (spaced (Art::silk (12.0f), 0.1f));
    g.setColour (Art::Colours::silk);
    g.drawText (label.toUpperCase(), key.withTrimmedLeft (14.0f).withTrimmedRight (1.0f), Justification::centred, false);
}
} // namespace

Rectangle<float> drawKeyCap (Graphics& g, Rectangle<float> b, bool pressed)
{
    g.setColour (Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (b, 3.0f);
    const auto key = b.reduced (1.5f).translated (0.0f, pressed ? 1.0f : 0.0f);
    ColourGradient face (Colour (0xff2a2a2c), key.getX(), key.getY(), Colour (0xff111112), key.getX(), key.getBottom(), false);
    g.setGradientFill (face);
    g.fillRoundedRectangle (key, 2.5f);
    g.setColour (Colours::white.withAlpha (pressed ? 0.04f : 0.1f));
    g.drawLine (key.getX() + 2.0f, key.getY() + 0.8f, key.getRight() - 2.0f, key.getY() + 0.8f, 0.8f);
    return key;
}

ArrowButton::ArrowButton (bool pointsRight, const String& hint) : right (pointsRight)
{
    setHint (*this, hint);
}

void ArrowButton::mouseDown (const MouseEvent&) { held = true; repaint(); if (onPress) onPress(); }
void ArrowButton::mouseUp (const MouseEvent&)   { held = false; repaint(); }

void ArrowButton::paint (Graphics& g)
{
    const auto key = drawKeyCap (g, getLocalBounds().toFloat(), held);
    const auto c = key.getCentre();
    const auto s = std::min (key.getWidth(), key.getHeight()) * 0.22f;
    Path arrow;
    if (right) arrow.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
    else       arrow.addTriangle (c.x + s * 0.8f, c.y - s, c.x + s * 0.8f, c.y + s, c.x - s, c.y);
    g.setColour (Art::Colours::silk);
    g.fillPath (arrow);
}

void SmallButton::paint (Graphics& g)
{
    drawSmallKey (g, getLocalBounds().toFloat(), label, isLit(), held);
}

ActionButton::ActionButton (const String& labelText, const String& hint) : label (labelText)
{
    setHint (*this, hint);
}

void ActionButton::mouseDown (const MouseEvent&) { held = true; repaint(); if (onPress) onPress(); }
void ActionButton::mouseUp (const MouseEvent&)   { held = false; repaint(); if (onRelease) onRelease(); }
void ActionButton::paint (Graphics& g)           { drawSmallKey (g, getLocalBounds().toFloat(), label, held || (isLit && isLit()), held); }

//==============================================================================
LedReadout::LedReadout (int numDigits, const String& cap) : digits (numDigits), caption (cap)
{
    setInterceptsMouseClicks (false, false);
}

void LedReadout::setText (const String& t)
{
    if (t != text)
    {
        text = t;
        repaint();
    }
}

void LedReadout::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (caption.isNotEmpty())
    {
        g.setFont (spaced (Art::silk (11.0f), 0.14f));
        g.setColour (Art::Colours::silkDim);
        g.drawText (caption.toUpperCase(), b.removeFromTop (14.0f), Justification::centred);
        b.removeFromTop (2.0f);
    }

    Art::drawRecess (g, b, 3.0f);
    g.setColour (Colour (0xff0b0302));
    g.fillRoundedRectangle (b.reduced (2.0f), 2.0f);

    // Split the text into cells; a '.' lights the decimal point of the previous cell.
    std::vector<std::pair<juce_wchar, bool>> cells;
    for (auto ch : text)
    {
        if (ch == '.' && ! cells.empty()) cells.back().second = true;
        else cells.push_back ({ ch, false });
    }
    while ((int) cells.size() < digits) cells.insert (cells.begin(), { ' ', false });

    const auto inner = b.reduced (b.getHeight() * 0.18f, b.getHeight() * 0.16f);
    const auto cellW = inner.getWidth() / (float) digits;
    for (int i = 0; i < digits; ++i)
    {
        const auto cell = Rectangle<float> (inner.getX() + cellW * (float) i, inner.getY(), cellW * 0.8f, inner.getHeight());
        const auto lit = segmentsFor (cells[(size_t) i].first);
        for (int seg = 0; seg < 7; ++seg)
        {
            const auto p = segmentPath (seg, cell);
            if ((lit >> seg) & 1)
            {
                glow (g, p, ledRed, 1.2f);
                g.setColour (ledRed);
            }
            else
                g.setColour (ledRed.withAlpha (0.07f));
            g.fillPath (p);
        }
        const auto dp = Rectangle<float> (cell.getRight() + cellW * 0.02f, cell.getBottom() - cellW * 0.14f, cellW * 0.13f, cellW * 0.13f);
        g.setColour (cells[(size_t) i].second ? ledRed : ledRed.withAlpha (0.07f));
        g.fillEllipse (dp);
    }
}

//==============================================================================
void HintStrip::setText (const String& t)
{
    if (t != text) { text = t; repaint(); }
}

void HintStrip::setRecording (bool r)
{
    if (r != recording) { recording = r; repaint(); }
}

void HintStrip::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    Art::drawRecess (g, b, 4.0f);
    g.setColour (Colour (0xff0c0c0d));
    g.fillRoundedRectangle (b.reduced (2.0f), 3.0f);

    drawLedDome (g, { b.getX() + 18.0f, b.getCentreY() }, 4.0f, recording ? 1.0f : 0.0f);
    g.setFont (Art::silkLight (14.0f));
    g.setColour (Art::Colours::silk.withAlpha (0.92f));
    g.drawFittedText (text, b.withTrimmedLeft (36.0f).withTrimmedRight (12.0f).toNearestInt(), Justification::centredLeft, 1, 0.85f);
}

//==============================================================================
CassetteView::CassetteView (const dust::TapeMachine& t, std::atomic<float>* freezeParam) : tape (t), freeze (freezeParam)
{
    setInterceptsMouseClicks (false, false);
    lastTime = Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void CassetteView::resized()
{
    const auto b = getLocalBounds().toFloat();
    const auto shell = b.reduced (b.getWidth() * 0.035f, b.getHeight() * 0.06f);
    const auto w = shell.getWidth(), h = shell.getHeight();
    rollerRadius = h * 0.075f;
    rollers = { Point<float> (shell.getCentreX(), shell.getY() + h * 0.2f),
                Point<float> (shell.getRight() - w * 0.17f, shell.getBottom() - h * 0.2f),
                Point<float> (shell.getX() + w * 0.17f, shell.getBottom() - h * 0.2f) };

    // The tape wraps the outside of each roller: straight runs between tangent points, arcs round the rollers.
    loopPath.clear();
    const auto r = rollerRadius + 1.0f;
    const auto centroid = (rollers[0] + rollers[1] + rollers[2]) / 3.0f;
    for (size_t i = 0; i < 3; ++i)
    {
        const auto a = rollers[i], next = rollers[(i + 1) % 3];
        const auto dir = (next - a) / a.getDistanceFrom (next);
        auto normal = Point<float> (-dir.y, dir.x);
        if (normal.getDotProduct (a - centroid) < 0.0f) normal = -normal; // outward
        const auto from = a + normal * r, to = next + normal * r;
        if (i == 0) loopPath.startNewSubPath (from); else loopPath.lineTo (from);
        loopPath.lineTo (to);
        // Arc round the next roller to the start of the following run
        const auto after = rollers[(i + 2) % 3];
        const auto dir2 = (after - next) / next.getDistanceFrom (after);
        auto normal2 = Point<float> (-dir2.y, dir2.x);
        if (normal2.getDotProduct (next - centroid) < 0.0f) normal2 = -normal2;
        const auto startAngle = std::atan2 (normal.x, -normal.y), endAngle = std::atan2 (normal2.x, -normal2.y);
        auto sweep = endAngle - startAngle;
        while (sweep < 0.0f) sweep += MathConstants<float>::twoPi;
        loopPath.addCentredArc (next.x, next.y, r, r, 0.0f, startAngle, startAngle + sweep);
    }
    loopPath.closeSubPath();
    loopLength = std::max (1.0f, loopPath.getLength());
}

void CassetteView::timerCallback()
{
    const auto now = Time::getMillisecondCounterHiRes();
    const auto dt = (float) std::min (0.1, (now - lastTime) * 0.001);
    lastTime = now;

    const auto frozen = freeze->load() >= 0.5f;
    const auto speed = frozen ? 0.0f : tape.speedDisplay.load();
    for (size_t i = 0; i < phases.size(); ++i)
        phases[i] = tape.headPhase[i].load();
    travel = std::fmod (travel + speed * dt * 60.0f + loopLength, loopLength); // texture slides at tape speed
    repaint();
}

void CassetteView::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // The deck's window: recessed and dark behind the clear cassette
    Art::drawRecess (g, b, 6.0f);
    g.setColour (Colour (0xff060607));
    g.fillRoundedRectangle (b.reduced (3.0f), 5.0f);

    const auto shell = b.reduced (b.getWidth() * 0.035f, b.getHeight() * 0.06f);
    const auto h = shell.getHeight(), w = shell.getWidth();

    // Clear shell: faint body, moulded edge, inner ribs and screw posts seen through the plastic
    g.setColour (Colour (0xffcfd8e0).withAlpha (0.05f));
    g.fillRoundedRectangle (shell, h * 0.06f);
    g.setColour (Colour (0xffcfd8e0).withAlpha (0.22f));
    g.drawRoundedRectangle (shell, h * 0.06f, 1.2f);
    g.setColour (Colour (0xffcfd8e0).withAlpha (0.08f));
    g.drawRoundedRectangle (shell.reduced (h * 0.05f), h * 0.04f, 1.0f);
    for (auto p : { shell.getTopLeft().translated (h * 0.12f, h * 0.12f), shell.getTopRight().translated (-h * 0.12f, h * 0.12f),
                    shell.getBottomLeft().translated (h * 0.12f, -h * 0.12f), shell.getBottomRight().translated (-h * 0.12f, -h * 0.12f) })
    {
        g.setColour (Colour (0xffcfd8e0).withAlpha (0.12f));
        g.drawEllipse (p.x - h * 0.05f, p.y - h * 0.05f, h * 0.1f, h * 0.1f, 1.0f);
        Art::drawScrew (g, p, 3.0f, 0.4f);
    }

    // Two empty hub wells, the usual cassette landmarks (the loop doesn't use them)
    for (auto x : { shell.getX() + w * 0.29f, shell.getX() + w * 0.71f })
    {
        const Point<float> c (x, shell.getY() + h * 0.48f);
        const auto hubR = h * 0.1f;
        g.setColour (Colour (0xffcfd8e0).withAlpha (0.1f));
        g.drawEllipse (c.x - hubR * 1.5f, c.y - hubR * 1.5f, hubR * 3.0f, hubR * 3.0f, 1.0f);
        g.setColour (Colour (0xffd9d6cf).withAlpha (0.55f));
        g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.6f);
    }

    // The tape loop: dark oxide, seen edge-on, with faint texture sliding along it
    g.setColour (Colour (0xff5a3d28));
    g.strokePath (loopPath, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
    constexpr int marks = 28;
    for (int i = 0; i < marks; ++i)
    {
        const auto d = std::fmod (travel + loopLength * (float) i / (float) marks, loopLength);
        const auto p = loopPath.getPointAlongPath (d);
        g.setColour (Colour (0xff9a7356).withAlpha (i % 3 == 0 ? 0.55f : 0.25f));
        g.fillEllipse (p.x - 1.1f, p.y - 1.1f, 2.2f, 2.2f);
    }

    // Loop markers: a splice tab for each loop at its own head position, lettered on the outside.
    {
        static const Colour markerColours[] { Colour (0xffff4a36), Colour (0xffffb23a), Colour (0xff8fd6ff) };
        const auto centroid = (rollers[0] + rollers[1] + rollers[2]) / 3.0f;
        for (size_t i = 0; i < phases.size(); ++i)
        {
            const auto d = std::fmod (phases[i] * loopLength + loopLength, loopLength);
            const auto p = loopPath.getPointAlongPath (d), q = loopPath.getPointAlongPath (std::fmod (d + 8.0f, loopLength));
            const auto colour = markerColours[i];
            g.setColour (colour.withAlpha (0.3f));
            g.drawLine ({ p, q }, 7.0f);
            g.setColour (colour);
            g.drawLine ({ p, q }, 3.6f);

            const auto mid = (p + q) * 0.5f;
            auto out = mid - centroid;
            out /= std::max (1.0f, out.getDistanceFromOrigin());
            const auto labelCentre = mid + out * 11.0f;
            g.setFont (Art::silk (11.0f));
            g.drawText (String::charToString ((juce_wchar) ('A' + i)),
                        Rectangle<float> (12.0f, 12.0f).withCentre (labelCentre), Justification::centred, false);
        }
    }

    // Guide rollers, turning with the tape
    for (auto c : rollers)
    {
        const auto r = rollerRadius;
        g.setColour (Colour (0xffe6e3dc));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (Colour (0xff2a2a2a));
        g.fillEllipse (c.x - r * 0.35f, c.y - r * 0.35f, r * 0.7f, r * 0.7f);
        const auto angle = travel / r;
        g.setColour (Colours::black.withAlpha (0.35f));
        for (int k = 0; k < 3; ++k)
        {
            const auto a = angle + MathConstants<float>::twoPi * (float) k / 3.0f;
            g.drawLine (c.x + std::sin (a) * r * 0.45f, c.y - std::cos (a) * r * 0.45f,
                        c.x + std::sin (a) * r * 0.9f, c.y - std::cos (a) * r * 0.9f, 1.0f);
        }
    }

    // The head opening along the bottom edge, where the tape meets the deck's head
    Path mouth;
    const auto mw = w * 0.5f, mx = shell.getCentreX();
    mouth.addQuadrilateral (mx - mw * 0.5f, shell.getBottom(), mx - mw * 0.42f, shell.getBottom() - h * 0.1f,
                            mx + mw * 0.42f, shell.getBottom() - h * 0.1f, mx + mw * 0.5f, shell.getBottom());
    g.setColour (Colour (0xffcfd8e0).withAlpha (0.12f));
    g.strokePath (mouth, PathStrokeType (1.0f));
    g.setColour (Art::Colours::bareMetal);
    g.fillRoundedRectangle (mx - w * 0.05f, shell.getBottom() - h * 0.06f, w * 0.1f, h * 0.06f, 2.0f);

    // Glass reflection over everything
    ColourGradient glass (Colours::white.withAlpha (0.07f), b.getX(), b.getY(), Colours::white.withAlpha (0.0f), b.getX() + b.getWidth() * 0.4f, b.getBottom(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (b.reduced (3.0f), 5.0f);
}
} // namespace deck
