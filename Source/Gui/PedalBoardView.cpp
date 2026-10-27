#include "PedalBoardView.h"
#include "Art.h"
#include "Core/Params.h"
#include "Plugin/PluginProcessor.h"

namespace deck
{
using namespace juce;

namespace
{
constexpr int pedalWidth = 150, tapeWidth = 72, itemHeight = 220, itemTop = 34;

struct KnobDef { const char* id; const char* label; const char* hint; };

struct PedalDef
{
    const char* title;
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
        { "Dirt", "DIRT: grit. Tape drive into fuzz into bitcrush, on one BLEND knob. Drag the pedal to move it.", Colour (0xff4a1712),
          { { dirt::drive, "Drive", "DRIVE: how hard it is pushed." },
            { dirt::blend, "Blend", "BLEND: warm tape drive (left), fuzz (middle), crushed lo-fi (right)." },
            { dirt::tone, "Tone", "TONE: dark to bright." },
            { dirt::mix, "Mix", "MIX: clean to fully dirty." } },
          nullptr, nullptr, nullptr, nullptr, nullptr },
        { "Warble", "WARBLE: wobbly tape chorus and vibrato, from gentle to seasick. Drag the pedal to move it.", Colour (0xff2c2236),
          { { warble::rate, "Rate", "RATE: how fast it wobbles." },
            { warble::depth, "Depth", "DEPTH: from gentle shimmer to seasick." },
            { warble::mix, "Mix", "MIX: how much wobble." } },
          warble::mode, "Vib", "VIB: lit = vibrato (pure pitch wobble). Off = chorus (wobble mixed with the dry sound).", nullptr, nullptr },
        { "Drift", "DRIFT FILTER: a resonant filter that sweeps very slowly on its own. Drag the pedal to move it.", Colour (0xff1d2a3a),
          { { drift::freq, "Freq", "FREQ: where the filter sits." },
            { drift::reso, "Reso", "RESO: resonance. High = whistling, vocal sweeps." },
            { drift::lfoRate, "Drift", "DRIFT: speed of the slow sweep, down to once every two minutes." },
            { drift::lfoDepth, "Sweep", "SWEEP: how far the filter moves." } },
          drift::mode, "BP", "BP: lit = bandpass (thin, focused). Off = lowpass (dark, warm).",
          drift::envAmt, "ENV: the filter follows how loud you play. Right opens it, left closes it." },
        { "Cloud", "CLOUD: smears the sound into shimmering pitched grains that swell. Drag the pedal to move it.", Colour (0xff3b3d40),
          { { cloud::pitch, "Pitch", "PITCH: octave down, fifth up, octave up, or up and down together." },
            { cloud::smear, "Smear", "SMEAR: longer, more scattered grains." },
            { cloud::density, "Density", "DENSITY: more grains, and the cloud feeds back into itself and swells." },
            { cloud::mix, "Mix", "MIX: how much cloud." } },
          nullptr, nullptr, nullptr, nullptr, nullptr },
        { "Hall", "HALL: a huge, dark reverb, up to a minute long. Drag the pedal to move it.", Colour (0xff151515),
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

void drawEnclosure (Graphics& g, Rectangle<float> b, Colour paint, int seed)
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

// Right-angle plug on a top-mounted jack, with the cable leaving upwards.
void drawCable (Graphics& g, Point<float> from, Point<float> to, Colour colour)
{
    const auto lift = 26.0f + std::abs (to.x - from.x) * 0.04f;
    Path cable;
    cable.startNewSubPath (from.translated (0.0f, -10.0f));
    cable.cubicTo (from.x, from.y - 10.0f - lift, to.x, to.y - 10.0f - lift, to.x, to.y - 10.0f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (cable, PathStrokeType (6.5f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (1.5f, 3.0f));
    g.setColour (colour.darker (0.5f));
    g.strokePath (cable, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (cable, PathStrokeType (3.8f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white.withAlpha (0.18f));
    g.strokePath (cable, PathStrokeType (1.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-0.8f, -0.8f));

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
void BoardItem::mouseDrag (const MouseEvent& e) { board.dragMoved (slot, e.getDistanceFromDragStartX()); }
void BoardItem::mouseUp (const MouseEvent&) { board.dragEnded (slot); }

//==============================================================================
PedalView::PedalView (PedalBoardView& b, APVTS& state, int pedalIndex, std::function<void (Knob&)> onTurn)
    : BoardItem (b, pedalIndex)
{
    const auto& def = pedalDefs()[(size_t) pedalIndex];
    title = def.title;
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
    const int cols[] { 40, 110 };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto row = (int) i / 2;
        const auto cx = knobs.size() == 3 && i == 2 ? getWidth() / 2 : cols[i % 2];
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
    const auto b = getLocalBounds().toFloat().reduced (1.0f, 0.0f).withTrimmedBottom (2.0f);
    drawEnclosure (g, b, paint1, slot * 77 + 3);
    drawJack (g, { b.getX() + 20.0f, b.getY() + 3.0f });
    drawJack (g, { b.getRight() - 20.0f, b.getY() + 3.0f });
    drawDymoLabel (g, { b.getCentreX(), 30.0f }, title, 17.0f, slot + 900);
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

void PedalBoardView::layout (bool animate)
{
    const auto order = currentOrder();
    const auto gap = (getWidth() - 5 * pedalWidth - tapeWidth) / 7;
    int x = gap;
    for (auto slot : order)
    {
        const auto w = slot == dust::PedalBoard::tapeSlot ? tapeWidth : pedalWidth;
        const Rectangle<int> target (x, itemTop, w, itemHeight);
        auto& item = *items[(size_t) slot];
        if (animate)
            Desktop::getInstance().getAnimator().animateComponent (&item, target, 1.0f, 180, false, 0.3, 0.0);
        else
            item.setBounds (target);
        x += w + gap;
    }
    repaint();
}

void PedalBoardView::dragStarted (int slot)
{
    dragging = slot;
    dragStartX = items[(size_t) slot]->getX();
    items[(size_t) slot]->toFront (false);
}

void PedalBoardView::dragMoved (int slot, int deltaX)
{
    if (slot != dragging) return;
    auto& item = *items[(size_t) slot];
    item.setTopLeftPosition (jlimit (0, getWidth() - item.getWidth(), dragStartX + deltaX), itemTop - 6); // lifted while held
    repaint();
}

void PedalBoardView::dragEnded (int slot)
{
    if (slot != dragging) return;
    dragging = -1;

    // New order: everything sorted by where it is now.
    std::array<int, 6> order {};
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int) i;
    std::sort (order.begin(), order.end(), [this] (int a, int b)
               { return items[(size_t) a]->getBounds().getCentreX() < items[(size_t) b]->getBounds().getCentreX(); });
    processor.setPedalOrder (order);
    layout (true);
}

void PedalBoardView::timerCallback()
{
    items[5]->repaint (0, 0, items[5]->getWidth(), 30);
    repaint (0, 0, getWidth(), itemTop); // cables follow animations
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
        // A pedalboard: dark ribbed surface between two aluminium rails
        gi.setColour (Colour (0xff0b0b0c).withAlpha (0.85f));
        gi.fillRect (b.withTrimmedTop (6.0f).withTrimmedBottom (6.0f));
        for (float y = 10.0f; y < b.getBottom() - 8.0f; y += 4.0f)
        {
            gi.setColour (Colours::white.withAlpha (0.018f));
            gi.drawLine (0.0f, y, b.getRight(), y, 1.0f);
        }
        for (auto y : { 0.0f, b.getBottom() - 6.0f })
            Art::drawRail (gi, { 0.0f, y, b.getWidth(), 6.0f }, (uint32) y + 5u);
        gi.setFont (Art::silk (11.0f));
        gi.setColour (Art::Colours::silkDim);
        gi.drawText ("IN", Rectangle<float> (4.0f, 28.0f, 30.0f, 14.0f), Justification::centredLeft);
        gi.drawText ("OUT", Rectangle<float> (b.getRight() - 34.0f, 28.0f, 30.0f, 14.0f), Justification::centredRight);
    }
    g.drawImage (background, getLocalBounds().toFloat());
}

void PedalBoardView::paintOverChildren (Graphics& g)
{
    // Cables between neighbours in their current on-screen order (so they follow a dragged pedal).
    std::array<BoardItem*, 6> sorted {};
    for (size_t i = 0; i < sorted.size(); ++i) sorted[i] = items[i].get();
    std::sort (sorted.begin(), sorted.end(), [] (auto* a, auto* b) { return a->getBounds().getCentreX() < b->getBounds().getCentreX(); });

    const auto inset = [] (BoardItem* item) { return item->slot == dust::PedalBoard::tapeSlot ? 16.0f : 20.0f; };
    const auto inJack = [&] (BoardItem* item) { return Point<float> ((float) item->getX() + inset (item), (float) item->getY() + 3.0f); };
    const auto outJack = [&] (BoardItem* item) { return Point<float> ((float) item->getRight() - inset (item), (float) item->getY() + 3.0f); };

    drawCable (g, { 12.0f, 26.0f }, inJack (sorted.front()), cableColours[0]);
    for (size_t i = 0; i + 1 < sorted.size(); ++i)
        drawCable (g, outJack (sorted[i]), inJack (sorted[i + 1]), cableColours[(i + 1) % std::size (cableColours)]);
    drawCable (g, outJack (sorted.back()), { (float) getWidth() - 12.0f, 26.0f }, cableColours[6]);
}
} // namespace deck
