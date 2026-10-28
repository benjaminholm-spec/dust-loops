#include "PedalBoardView.h"
#include "Art.h"
#include "Core/Params.h"
#include "Plugin/PluginProcessor.h"

namespace deck
{
using namespace juce;

namespace
{
constexpr int pedalWidth = 150, tapeWidth = 72, itemHeight = 220;
// The grid: 2 columns x 3 rows, with room above each row for the cables.
constexpr int cols = 2, rows = 3, sideMargin = 22, cellWidth = 170, topPad = 12, cableRoom = 26, cellHeight = itemHeight + cableRoom;

struct KnobDef { const char* id; const char* label; const char* hint; };

struct PedalDef
{
    const char* title;
    const char* planetName;
    const char* hint;
    Colour paint;
    std::vector<KnobDef> knobs;
    const char* extraButtonId; const char* extraButtonLabel; const char* extraButtonHint;
    const char* extraKnobId; const char* extraKnobHint;
};

const std::array<PedalDef, 5>& pedalDefs()
{
    using namespace ID::pedal;
    static const std::array<PedalDef, 5> defs { {
        { "Dirt", "Mars", "DIRT (Mars): grit. Tape drive into fuzz into bitcrush, on one BLEND knob. Drag the pedal to move it.", Colour (0xff4a1712),
          { { dirt::drive, "Drive", "DRIVE: how hard it is pushed." },
            { dirt::blend, "Blend", "BLEND: warm tape drive (left), fuzz (middle), crushed lo-fi (right)." },
            { dirt::tone, "Tone", "TONE: dark to bright." },
            { dirt::mix, "Mix", "MIX: clean to fully dirty." } },
          nullptr, nullptr, nullptr, nullptr, nullptr },
        { "Warble", "Neptune", "WARBLE (Neptune): wobbly tape chorus and vibrato, from gentle to seasick. Drag the pedal to move it.", Colour (0xff2c2236),
          { { warble::rate, "Rate", "RATE: how fast it wobbles." },
            { warble::depth, "Depth", "DEPTH: from gentle shimmer to seasick." },
            { warble::mix, "Mix", "MIX: how much wobble." } },
          warble::mode, "Vib", "VIB: lit = vibrato (pure pitch wobble). Off = chorus (wobble mixed with the dry sound).", nullptr, nullptr },
        { "Drift", "Uranus", "DRIFT FILTER (Uranus): a resonant filter that sweeps very slowly on its own. Drag the pedal to move it.", Colour (0xff1d2a3a),
          { { drift::freq, "Freq", "FREQ: where the filter sits." },
            { drift::reso, "Reso", "RESO: resonance. High = whistling, vocal sweeps." },
            { drift::lfoRate, "Drift", "DRIFT: speed of the slow sweep, down to once every two minutes." },
            { drift::lfoDepth, "Sweep", "SWEEP: how far the filter moves." } },
          drift::mode, "BP", "BP: lit = bandpass (thin, focused). Off = lowpass (dark, warm).",
          drift::envAmt, "ENV: the filter follows how loud you play. Right opens it, left closes it." },
        { "Cloud", "Jupiter", "CLOUD (Jupiter): smears the sound into shimmering pitched grains that swell. Drag the pedal to move it.", Colour (0xff3b3d40),
          { { cloud::pitch, "Pitch", "PITCH: octave down, fifth up, octave up, or up and down together." },
            { cloud::smear, "Smear", "SMEAR: longer, more scattered grains." },
            { cloud::density, "Density", "DENSITY: more grains, and the cloud feeds back into itself and swells." },
            { cloud::mix, "Mix", "MIX: how much cloud." } },
          nullptr, nullptr, nullptr, nullptr, nullptr },
        { "Hall", "Saturn", "HALL (Saturn): a huge, dark reverb, up to a minute long. Drag the pedal to move it.", Colour (0xff151515),
          { { hall::decay, "Decay", "DECAY: how long the reverb rings, up to 60 seconds." },
            { hall::tone, "Tone", "TONE: dark to bright." },
            { hall::mix, "Mix", "MIX: how much reverb." } },
          hall::freeze, "Frz", "FREEZE: holds the reverb tail forever, under whatever you play.", nullptr, nullptr },
    } };
    return defs;
}

const Colour cableColours[] { Colour (0xffa3261c), Colour (0xff1a1a1a), Colour (0xff3d3d40), Colour (0xff1a1a1a),
                              Colour (0xffa3261c), Colour (0xff3d3d40), Colour (0xff1a1a1a) };

class PedalLed : public ParamControl
{
public:
    PedalLed (APVTS& state, const String& paramId) : ParamControl (state, paramId) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override { drawLed (g, getLocalBounds().toFloat().getCentre(), 4.5f, value() >= 0.5f ? 1.0f : 0.0f); }
};

void drawJack (Graphics& g, Point<float> c)
{
    ColourGradient nut (Colour (0xffc9c8c2), c.x - 6.0f, c.y - 6.0f, Colour (0xff4b4a46), c.x + 6.0f, c.y + 6.0f, false);
    g.setGradientFill (nut);
    g.fillEllipse (c.x - 6.5f, c.y - 3.0f, 13.0f, 6.0f);
    g.setColour (Colours::black);
    g.fillEllipse (c.x - 3.0f, c.y - 1.5f, 6.0f, 3.0f);
}

void drawEnclosure (Graphics& g, Rectangle<float> b, Colour paint, int seed,
                    const std::function<void (Graphics&, Rectangle<float>)>& surface = {})
{
    // Shadow on the board
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (b.translated (2.0f, 4.0f), 7.0f);

    // Die-cast box under the paint
    g.setColour (Art::Colours::bareMetal.darker (0.3f));
    g.fillRoundedRectangle (b, 7.0f);
    ColourGradient coat (paint.brighter (0.25f), b.getX(), b.getY(), paint.darker (0.4f), b.getX(), b.getBottom(), false);
    g.setGradientFill (coat);
    g.fillRoundedRectangle (b.reduced (1.2f), 6.0f);
    if (surface)
    {
        Graphics::ScopedSaveState save (g);
        Path face;
        face.addRoundedRectangle (b.reduced (1.2f), 6.0f);
        g.reduceClipRegion (face);
        surface (g, b.reduced (1.2f));
    }

    Random r (seed);
    // Paint chipped off along the edges, down to the aluminium
    for (int i = 0; i < 26; ++i)
    {
        const auto onSide = r.nextBool();
        const auto x = onSide ? (r.nextBool() ? b.getX() + 1.5f : b.getRight() - 4.0f) : b.getX() + r.nextFloat() * b.getWidth();
        const auto y = onSide ? b.getY() + r.nextFloat() * b.getHeight() : (r.nextBool() ? b.getY() + 1.0f : b.getBottom() - 4.0f);
        const auto s = 1.0f + r.nextFloat() * 3.5f;
        g.setColour (Art::Colours::bareMetal.withAlpha (0.4f + 0.4f * r.nextFloat()));
        g.fillEllipse (x, y, s, s * (0.5f + r.nextFloat()));
    }
    // Scuffs
    for (int i = 0; i < 14; ++i)
    {
        const auto x = b.getX() + r.nextFloat() * b.getWidth(), y = b.getY() + r.nextFloat() * b.getHeight();
        g.setColour (Colours::white.withAlpha (0.03f + 0.04f * r.nextFloat()));
        g.drawLine (x, y, x + (r.nextFloat() - 0.5f) * 30.0f, y + (r.nextFloat() - 0.5f) * 8.0f, 0.6f);
    }
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (b.reduced (1.0f), 6.0f, 0.8f);

    for (auto p : { Point<float> (b.getX() + 8.0f, b.getY() + 8.0f), Point<float> (b.getRight() - 8.0f, b.getY() + 8.0f),
                    Point<float> (b.getX() + 8.0f, b.getBottom() - 8.0f), Point<float> (b.getRight() - 8.0f, b.getBottom() - 8.0f) })
        Art::drawScrew (g, p, 2.6f, p.x * 0.07f);
}

//==============================================================================
// Planet paint jobs: each pedal's face is a planet's surface, kept dark enough for the controls to read.
enum class Planet { mars, neptune, uranus, jupiter, saturn };

// Horizontal cloud bands with wavy edges.
void drawBands (Graphics& g, Rectangle<float> a, const std::vector<Colour>& colours, int count, float wave, Random& r)
{
    for (int i = 0; i < count; ++i)
    {
        const auto y0 = a.getY() + a.getHeight() * ((float) i + r.nextFloat() * 0.3f) / (float) count;
        const auto thick = a.getHeight() / (float) count * (0.5f + r.nextFloat() * 0.7f);
        const auto phase = r.nextFloat() * 6.28f, freq = 0.03f + r.nextFloat() * 0.05f;
        Path band;
        band.startNewSubPath (a.getX() - 2.0f, y0);
        for (float x = a.getX(); x <= a.getRight() + 4.0f; x += 4.0f)
            band.lineTo (x, y0 + wave * std::sin (x * freq + phase));
        for (float x = a.getRight() + 4.0f; x >= a.getX() - 4.0f; x -= 4.0f)
            band.lineTo (x, y0 + thick + wave * 0.8f * std::sin (x * freq * 1.3f + phase + 1.0f));
        band.closeSubPath();
        g.setColour (colours[(size_t) i % colours.size()].withAlpha (0.55f + 0.3f * r.nextFloat()));
        g.fillPath (band);
    }
}

void drawStorm (Graphics& g, Point<float> c, float w, float h, Colour core, Colour rim)
{
    g.setColour (rim.withAlpha (0.7f));
    g.fillEllipse (c.x - w * 0.6f, c.y - h * 0.6f, w * 1.2f, h * 1.2f);
    ColourGradient eye (core.brighter (0.2f), c.x - w * 0.15f, c.y - h * 0.15f, core.darker (0.3f), c.x + w * 0.5f, c.y + h * 0.5f, true);
    g.setGradientFill (eye);
    g.fillEllipse (c.x - w * 0.5f, c.y - h * 0.5f, w, h);
}

void drawPlanetSurface (Graphics& g, Rectangle<float> a, Planet planet, int seed)
{
    Random r (seed);
    switch (planet)
    {
        case Planet::mars:
        {
            g.fillAll (Colour (0xff7e3320));
            for (int i = 0; i < 12; ++i) // dark plains
            {
                const auto w = 20.0f + 50.0f * r.nextFloat();
                g.setColour (Colour (0xff3e170e).withAlpha (0.35f + 0.25f * r.nextFloat()));
                g.fillEllipse (a.getX() + r.nextFloat() * a.getWidth() - w * 0.3f, a.getY() + r.nextFloat() * a.getHeight(), w, w * (0.3f + 0.4f * r.nextFloat()));
            }
            for (int i = 0; i < 14; ++i) // craters, lit from the top left
            {
                const auto c = Point<float> (a.getX() + r.nextFloat() * a.getWidth(), a.getY() + 20.0f + r.nextFloat() * (a.getHeight() - 20.0f));
                const auto rad = 2.0f + 6.0f * r.nextFloat() * r.nextFloat();
                g.setColour (Colour (0xff2e100a).withAlpha (0.55f));
                g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
                g.setColour (Colour (0xffd28a5e).withAlpha (0.45f));
                g.drawEllipse (c.x - rad + 0.6f, c.y - rad + 0.6f, rad * 2.0f, rad * 2.0f, 0.8f);
            }
            for (int i = 0; i < 90; ++i) // dust
            {
                g.setColour (Colour (0xffe0a070).withAlpha (0.08f + 0.12f * r.nextFloat()));
                g.fillEllipse (a.getX() + r.nextFloat() * a.getWidth(), a.getY() + r.nextFloat() * a.getHeight(), 1.4f, 1.4f);
            }
            g.setColour (Colour (0xffe8e2da).withAlpha (0.55f)); // polar cap
            g.fillEllipse (a.getCentreX() - 34.0f, a.getY() - 14.0f, 68.0f, 24.0f);
            break;
        }
        case Planet::neptune:
            g.fillAll (Colour (0xff17307a));
            drawBands (g, a, { Colour (0xff2348a0), Colour (0xff122661), Colour (0xff1c3b8c), Colour (0xff2b55b0) }, 9, 4.0f, r);
            for (int i = 0; i < 7; ++i) // bright methane streaks
            {
                const auto y = a.getY() + 30.0f + r.nextFloat() * (a.getHeight() - 40.0f);
                const auto x = a.getX() + r.nextFloat() * a.getWidth() * 0.7f;
                g.setColour (Colour (0xffd8e6ff).withAlpha (0.18f + 0.2f * r.nextFloat()));
                g.fillEllipse (x, y, 20.0f + 30.0f * r.nextFloat(), 2.0f + 2.0f * r.nextFloat());
            }
            drawStorm (g, { a.getX() + a.getWidth() * 0.7f, a.getY() + a.getHeight() * 0.62f }, 34.0f, 16.0f,
                       Colour (0xff0b1a48), Colour (0xff3d63c0));
            break;
        case Planet::uranus:
        {
            g.fillAll (Colour (0xff4f8e98));
            drawBands (g, a, { Colour (0xff5c9ea7), Colour (0xff467f89), Colour (0xff63a8b0) }, 7, 1.5f, r);
            // Uranus's rings stand on end
            g.setColour (Colour (0xffd8f0f2).withAlpha (0.28f));
            for (auto k : { 0.0f, 5.0f, 9.0f })
                g.drawEllipse (a.getCentreX() - 18.0f - k, a.getY() - 30.0f - k * 2.0f, 36.0f + k * 2.0f, a.getHeight() + 60.0f + k * 4.0f, 1.0f);
            break;
        }
        case Planet::jupiter:
            g.fillAll (Colour (0xffa47c55));
            drawBands (g, a, { Colour (0xffd3bc95), Colour (0xff8a5e3b), Colour (0xffc9a378), Colour (0xff6e4528), Colour (0xffe0cfae),
                               Colour (0xff9a6a44) }, 12, 3.0f, r);
            drawStorm (g, { a.getX() + a.getWidth() * 0.68f, a.getY() + a.getHeight() * 0.6f }, 44.0f, 22.0f,
                       Colour (0xffa8442a), Colour (0xffd9b48a));
            break;
        case Planet::saturn:
            g.fillAll (Colour (0xffa88d58));
            drawBands (g, a, { Colour (0xffc6ac74), Colour (0xff927645), Colour (0xffd4c08c), Colour (0xffb39a62) }, 10, 1.2f, r);
            {
                // Rings sweeping across the face, with the Cassini gap
                const auto c = a.getCentre().translated (0.0f, 6.0f);
                const auto t = AffineTransform::rotation (-0.32f, c.x, c.y);
                const auto ring = [&] (float rx, float ry, float width, Colour colour)
                {
                    Path p;
                    p.addEllipse (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);
                    g.setColour (colour);
                    g.strokePath (p, PathStrokeType (width), t);
                };
                ring (118.0f, 34.0f, 10.0f, Colour (0xfff0e2bc).withAlpha (0.55f));
                ring (105.0f, 30.0f, 6.0f, Colour (0xffd9c393).withAlpha (0.5f));
                ring (127.5f, 37.0f, 2.5f, Colour (0xff2a200f).withAlpha (0.6f)); // the Cassini gap
                ring (136.0f, 40.0f, 6.0f, Colour (0xffe8d8ae).withAlpha (0.45f));
            }
            break;
    }

    // A sphere's limb: light from the top left, darker towards the edges, then a dim veil so the controls read.
    ColourGradient limb (Colours::white.withAlpha (0.1f), a.getX() + a.getWidth() * 0.3f, a.getY() + a.getHeight() * 0.25f,
                         Colours::black.withAlpha (0.5f), a.getRight() + 10.0f, a.getBottom() + 10.0f, true);
    g.setGradientFill (limb);
    g.fillRect (a);
    g.setColour (Colours::black.withAlpha (0.18f));
    g.fillRect (a);
}

void strokeCable (Graphics& g, const Path& cable, Colour colour);
void drawPlugs (Graphics& g, Point<float> from, Point<float> to, Colour colour);

// Right-angle plug on a top-mounted jack, with the cable leaving upwards.
void drawCable (Graphics& g, Point<float> from, Point<float> to, Colour colour)
{
    const auto lift = 12.0f + std::abs (to.x - from.x) * 0.02f;
    Path cable;
    cable.startNewSubPath (from.translated (0.0f, -10.0f));
    cable.cubicTo (from.x, from.y - 10.0f - lift, to.x, to.y - 10.0f - lift, to.x, to.y - 10.0f);
    strokeCable (g, cable, colour);
    drawPlugs (g, from, to, colour);
}

// From the end of one row down the side of the board to the start of the next.
void drawRowCable (Graphics& g, Point<float> from, Point<float> to, float sideX, Colour colour)
{
    Path route;
    route.startNewSubPath (from.translated (0.0f, -10.0f));
    route.lineTo (from.x, from.y - 19.0f);
    route.lineTo (sideX, from.y - 19.0f);
    route.lineTo (sideX, to.y - 19.0f);
    route.lineTo (to.x, to.y - 19.0f);
    route.lineTo (to.x, to.y - 10.0f);
    strokeCable (g, route.createPathWithRoundedCorners (9.0f), colour);
    drawPlugs (g, from, to, colour);
}

void strokeCable (Graphics& g, const Path& cable, Colour colour)
{
    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (cable, PathStrokeType (6.5f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (1.5f, 3.0f));
    g.setColour (colour.darker (0.5f));
    g.strokePath (cable, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (cable, PathStrokeType (3.8f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white.withAlpha (0.18f));
    g.strokePath (cable, PathStrokeType (1.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-0.8f, -0.8f));
}

void drawPlugs (Graphics& g, Point<float> from, Point<float> to, Colour colour)
{
    for (auto p : { from, to })
    {
        const Rectangle<float> plug (p.x - 4.5f, p.y - 14.0f, 9.0f, 13.0f);
        ColourGradient metal (Colour (0xff6a6a66), plug.getX(), 0.0f, Colour (0xff2c2c2a), plug.getRight(), 0.0f, false);
        metal.addColour (0.35, Colour (0xffdcdcd6));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (plug, 2.0f);
        g.setColour (colour.darker (0.2f));
        g.fillRoundedRectangle (plug.withHeight (4.0f), 1.5f);
    }
}
} // namespace

//==============================================================================
Footswitch::Footswitch (APVTS& state, const String& paramId, const String& hint) : ParamControl (state, paramId)
{
    setHint (*this, hint);
}

void Footswitch::mouseDown (const MouseEvent&)
{
    setParam (value() >= 0.5f ? 0.0f : 1.0f);
}

void Footswitch::paint (Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto r = std::min ((float) getWidth(), (float) getHeight()) * 0.5f - 2.0f;
    // Hex nut, then the domed chrome actuator
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto a = MathConstants<float>::pi / 3.0f * (float) i + 0.3f;
        const Point<float> p (c.x + r * std::cos (a), c.y + r * std::sin (a));
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    ColourGradient nut (Colour (0xffb5b4ae), c.x - r, c.y - r, Colour (0xff3e3d3a), c.x + r, c.y + r, false);
    g.setGradientFill (nut);
    g.fillPath (hex);
    const auto d = r * 0.68f;
    ColourGradient dome (Colour (0xfff2f2ee), c.x - d * 0.35f, c.y - d * 0.45f, Colour (0xff55544f), c.x + d, c.y + d, true);
    g.setGradientFill (dome);
    g.fillEllipse (c.x - d, c.y - d, d * 2.0f, d * 2.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - d, c.y - d, d * 2.0f, d * 2.0f, 0.8f);
}

//==============================================================================
BoardItem::BoardItem (PedalBoardView& b, int s) : slot (s), board (b) {}

void BoardItem::mouseDown (const MouseEvent&) { board.dragStarted (slot); }
void BoardItem::mouseDrag (const MouseEvent& e) { board.dragMoved (slot, e.getOffsetFromDragStart()); }
void BoardItem::mouseUp (const MouseEvent&) { board.dragEnded (slot); }

//==============================================================================
PedalView::PedalView (PedalBoardView& b, APVTS& state, int pedalIndex, std::function<void (Knob&)> onTurn)
    : BoardItem (b, pedalIndex)
{
    const auto& def = pedalDefs()[(size_t) pedalIndex];
    title = def.title;
    planetName = def.planetName;
    planet = pedalIndex;
    paint1 = def.paint;
    setHint (*this, def.hint);

    led = std::make_unique<PedalLed> (state, ID::pedal::id (pedalIndex, ID::pedal::on));
    addAndMakeVisible (*led);

    int seed = 500 + pedalIndex * 10;
    for (const auto& k : def.knobs)
    {
        knobs.push_back (std::make_unique<Knob> (state, k.id, k.label, k.hint, Knob::Size::small, ++seed));
        knobs.back()->onTurn = onTurn;
        addAndMakeVisible (*knobs.back());
    }

    loopButton = std::make_unique<SmallButton> (state, ID::pedal::id (pedalIndex, ID::pedal::loop), "Loop", SmallButton::Mode::toggle, 0,
        "LOOP: puts this pedal inside the tape loop as well, so its effect builds up on every pass. The trim next to it sets how much.");
    addAndMakeVisible (*loopButton);
    loopAmount = std::make_unique<Knob> (state, ID::pedal::id (pedalIndex, ID::pedal::loopAmt), "Loop amount",
        "LOOP AMOUNT: how strongly this pedal works inside the tape loop.", Knob::Size::tiny, ++seed);
    loopAmount->onTurn = onTurn;
    addAndMakeVisible (*loopAmount);

    if (def.extraButtonId != nullptr)
    {
        extraButton = std::make_unique<SmallButton> (state, def.extraButtonId, def.extraButtonLabel, SmallButton::Mode::toggle, 0, def.extraButtonHint);
        addAndMakeVisible (*extraButton);
    }
    if (def.extraKnobId != nullptr)
    {
        extraKnob = std::make_unique<Knob> (state, def.extraKnobId, "Env", def.extraKnobHint, Knob::Size::tiny, ++seed);
        extraKnob->onTurn = onTurn;
        addAndMakeVisible (*extraKnob);
    }

    footswitch = std::make_unique<Footswitch> (state, ID::pedal::id (pedalIndex, ID::pedal::on),
                                               title.toUpperCase() + ": stomp to switch it on or off.");
    addAndMakeVisible (*footswitch);
}

void PedalView::resized()
{
    led->setBounds (getWidth() / 2 - 8, 6, 16, 16);
    const int knobColumns[] { 40, 110 };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto row = (int) i / 2;
        const auto cx = knobs.size() == 3 && i == 2 ? getWidth() / 2 : knobColumns[i % 2];
        knobs[i]->setBounds (cx - 34, 40 + row * 60, 68, 62);
    }
    loopButton->setBounds (8, 164, 52, 22);
    loopAmount->setBounds (61, 163, 24, 24);
    if (extraButton != nullptr)
        extraButton->setBounds (88, 164, extraKnob != nullptr ? 36 : 54, 22);
    if (extraKnob != nullptr)
        extraKnob->setBounds (125, 163, 22, 24);
    footswitch->setBounds (getWidth() / 2 - 20, 188, 40, 30);
}

void PedalView::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        const auto b = getLocalBounds().toFloat().reduced (1.0f, 0.0f).withTrimmedBottom (2.0f);
        drawEnclosure (gi, b, paint1, slot * 77 + 3, [this] (Graphics& gs, Rectangle<float> face)
        {
            drawPlanetSurface (gs, face, (Planet) planet, 4000 + planet * 31);
        });
        drawJack (gi, { b.getX() + 20.0f, b.getY() + 3.0f });
        drawJack (gi, { b.getRight() - 20.0f, b.getY() + 3.0f });
        drawDymoLabel (gi, { b.getCentreX(), 30.0f }, title, 17.0f, slot + 900);

        // The planet's name, printed small beside the footswitch
        gi.setFont (Art::silk (10.0f));
        const Rectangle<float> nameArea (b.getX() + 14.0f, b.getBottom() - 26.0f, 46.0f, 14.0f);
        gi.setColour (Colours::black.withAlpha (0.7f));
        gi.drawText (planetName.toUpperCase(), nameArea.translated (0.6f, 0.8f), Justification::centredLeft, false);
        gi.setColour (Art::Colours::silk.withAlpha (0.85f));
        gi.drawText (planetName.toUpperCase(), nameArea, Justification::centredLeft, false);
    });
}

//==============================================================================
TapeBlockView::TapeBlockView (PedalBoardView& b, const dust::TapeMachine& t) : BoardItem (b, dust::PedalBoard::tapeSlot), tape (t)
{
    setHint (*this, "TAPE: the deck above, in the chain. Pedals left of it go onto the tape; pedals right of it colour what comes off. Drag to move.");
}

void TapeBlockView::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f, 0.0f).withTrimmedBottom (2.0f);
    drawEnclosure (g, b, Colour (0xff0e0e0e), 4242);
    drawJack (g, { b.getX() + 16.0f, b.getY() + 3.0f });
    drawJack (g, { b.getRight() - 16.0f, b.getY() + 3.0f });

    const auto moving = std::abs (tape.speedDisplay.load()) > 0.05f;
    drawLed (g, { b.getCentreX(), 16.0f }, 4.5f, moving ? 1.0f : 0.0f);

    // Little cassette icon
    const auto icon = Rectangle<float> (b.getCentreX() - 22.0f, 40.0f, 44.0f, 28.0f);
    g.setColour (Art::Colours::silk.withAlpha (0.8f));
    g.drawRoundedRectangle (icon, 3.0f, 1.2f);
    g.drawEllipse (icon.getX() + 8.0f, icon.getY() + 10.0f, 8.0f, 8.0f, 1.2f);
    g.drawEllipse (icon.getRight() - 16.0f, icon.getY() + 10.0f, 8.0f, 8.0f, 1.2f);

    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, b.getCentreX(), 140.0f));
    drawDymoLabel (g, { b.getCentreX(), 140.0f }, "Tape deck", 18.0f, 4243);
}

//==============================================================================
PedalBoardView::PedalBoardView (DustLoopsProcessor& p, std::function<void (Knob&)> onTurn) : processor (p)
{
    for (int i = 0; i < 5; ++i)
    {
        items[(size_t) i] = std::make_unique<PedalView> (*this, p.apvts, i, onTurn);
        addAndMakeVisible (*items[(size_t) i]);
    }
    items[5] = std::make_unique<TapeBlockView> (*this, p.getTape());
    addAndMakeVisible (*items[5]);
    startTimerHz (30); // the TAPE block's running light, and cables following animated pedals
}

std::array<int, 6> PedalBoardView::currentOrder() const
{
    return processor.getPedalOrder();
}

void PedalBoardView::resized()
{
    layout (false);
}

Rectangle<int> PedalBoardView::boundsForPosition (int position, int slot) const
{
    const auto row = position / cols;
    auto col = position % cols;
    if (row % 2 == 1)
        col = cols - 1 - col; // odd rows run right to left
    const auto w = slot == dust::PedalBoard::tapeSlot ? tapeWidth : pedalWidth;
    const auto cx = sideMargin + col * cellWidth + cellWidth / 2;
    return { cx - w / 2, topPad + row * cellHeight + cableRoom, w, itemHeight };
}

float PedalBoardView::positionKey (Rectangle<int> b) const
{
    const auto c = b.getCentre().toFloat();
    const auto row = jlimit (0, rows - 1, (int) ((c.y - (float) topPad) / (float) cellHeight));
    const auto col = jlimit (0.0f, (float) cols - 0.001f, (c.x - (float) sideMargin) / (float) cellWidth);
    return (float) (row * cols) + (row % 2 == 1 ? (float) cols - col : col);
}

int PedalBoardView::rowOf (const BoardItem& item) const
{
    return jlimit (0, rows - 1, (int) (positionKey (item.getBounds()) / (float) cols));
}

std::array<BoardItem*, 6> PedalBoardView::itemsInChainOrder() const
{
    std::array<BoardItem*, 6> sorted {};
    for (size_t i = 0; i < sorted.size(); ++i) sorted[i] = items[i].get();
    std::sort (sorted.begin(), sorted.end(), [this] (auto* a, auto* b) { return positionKey (a->getBounds()) < positionKey (b->getBounds()); });
    return sorted;
}

void PedalBoardView::layout (bool animate)
{
    const auto order = currentOrder();
    for (size_t pos = 0; pos < order.size(); ++pos)
    {
        const auto slot = order[pos];
        const auto target = boundsForPosition ((int) pos, slot);
        auto& item = *items[(size_t) slot];
        if (animate)
            Desktop::getInstance().getAnimator().animateComponent (&item, target, 1.0f, 180, false, 0.3, 0.0);
        else
            item.setBounds (target);
    }
    repaint();
}

void PedalBoardView::dragStarted (int slot)
{
    dragging = slot;
    dragStart = items[(size_t) slot]->getPosition();
    items[(size_t) slot]->toFront (false);
}

void PedalBoardView::dragMoved (int slot, Point<int> delta)
{
    if (slot != dragging) return;
    auto& item = *items[(size_t) slot];
    const auto p = dragStart + delta;
    item.setTopLeftPosition (jlimit (0, getWidth() - item.getWidth(), p.x),
                             jlimit (0, getHeight() - item.getHeight(), p.y - 6)); // lifted while held
    repaint();
}

void PedalBoardView::dragEnded (int slot)
{
    if (slot != dragging) return;
    dragging = -1;

    // New order: everything sorted by where it now sits along the snake.
    std::array<int, 6> order {};
    const auto sorted = itemsInChainOrder();
    for (size_t i = 0; i < order.size(); ++i) order[i] = sorted[i]->slot;
    processor.setPedalOrder (order);
    layout (true);
}

void PedalBoardView::timerCallback()
{
    items[5]->repaint (0, 0, items[5]->getWidth(), 30); // the TAPE block's running light

    // Cables follow pedals while they animate into place.
    bool moving = false;
    for (auto& item : items)
        moving = moving || Desktop::getInstance().getAnimator().isAnimating (item.get());
    if (moving || wasMoving)
        repaint();
    wasMoving = moving;
}

void PedalBoardView::paint (Graphics& g)
{
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs (scale - backgroundScale) > 0.01f)
    {
        backgroundScale = scale;
        background = Image (Image::ARGB, (int) std::ceil ((float) getWidth() * scale), (int) std::ceil ((float) getHeight() * scale), true);
        Art::drawBlackPanel (background, getLocalBounds(), scale, 0xb0a2d);
        Graphics gi (background);
        gi.addTransform (AffineTransform::scale (scale));
        const auto b = getLocalBounds().toFloat();
        // A pedalboard: dark ribbed surface between two aluminium rails, standing beside the deck
        gi.setColour (Colour (0xff0b0b0c).withAlpha (0.85f));
        gi.fillRect (b.withTrimmedLeft (6.0f).withTrimmedRight (6.0f));
        for (float y = 4.0f; y < b.getBottom(); y += 4.0f)
        {
            gi.setColour (Colours::white.withAlpha (0.018f));
            gi.drawLine (6.0f, y, b.getRight() - 6.0f, y, 1.0f);
        }
        for (auto x : { 0.0f, b.getRight() - 6.0f })
            Art::drawRail (gi, { x, 0.0f, 6.0f, b.getHeight() }, (uint32) x + 5u);
    }
    g.drawImage (background, getLocalBounds().toFloat());
}

void PedalBoardView::paintOverChildren (Graphics& g)
{
    // Cables between neighbours in their current on-screen order (so they follow a dragged pedal).
    const auto sorted = itemsInChainOrder();
    const auto inset = [] (BoardItem* item) { return item->slot == dust::PedalBoard::tapeSlot ? 16.0f : 20.0f; };
    const auto leftJack = [&] (BoardItem* item) { return Point<float> ((float) item->getX() + inset (item), (float) item->getY() + 3.0f); };
    const auto rightJack = [&] (BoardItem* item) { return Point<float> ((float) item->getRight() - inset (item), (float) item->getY() + 3.0f); };
    const auto goesRight = [&] (BoardItem* item) { return rowOf (*item) % 2 == 0; };
    const auto inJack = [&] (BoardItem* item) { return goesRight (item) ? leftJack (item) : rightJack (item); };
    const auto outJack = [&] (BoardItem* item) { return goesRight (item) ? rightJack (item) : leftJack (item); };
    const auto leftSide = 13.0f, rightSide = (float) getWidth() - 13.0f;

    // IN at the top left; OUT beside wherever the chain ends.
    const auto first = sorted.front(), last = sorted.back();
    const Point<float> inTerminal { leftSide, inJack (first).y + 34.0f }; // low enough for its label to fit above
    const Point<float> outTerminal { goesRight (last) ? rightSide : leftSide, outJack (last).y - 14.0f };
    drawCable (g, inTerminal, inJack (first), cableColours[0]);
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
    {
        const auto colour = cableColours[(i + 1) % std::size (cableColours)];
        if (rowOf (*sorted[i]) == rowOf (*sorted[i + 1]))
            drawCable (g, outJack (sorted[i]), inJack (sorted[i + 1]), colour);
        else
            drawRowCable (g, outJack (sorted[i]), inJack (sorted[i + 1]), goesRight (sorted[i]) ? rightSide : leftSide, colour);
    }
    drawCable (g, outJack (last), outTerminal, cableColours[6]);

    g.setFont (Art::silk (11.0f));
    g.setColour (Art::Colours::silkDim);
    g.drawText ("IN", Rectangle<float> (inTerminal.x - 8.0f, inTerminal.y - 28.0f, 30.0f, 14.0f), Justification::centredLeft);
    const auto outBox = Rectangle<float> (outTerminal.x - 15.0f, outTerminal.y - 28.0f, 30.0f, 14.0f);
    g.drawText ("OUT", outBox.translated (goesRight (last) ? -6.0f : 6.0f, 0.0f), Justification::centred);
}
} // namespace deck
